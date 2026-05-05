/**
 * @file main.cpp
 * @brief Professional Modbus Device Manager with Multi-threaded Architecture
 * @version 2.1
 * @date 2026-01-27
 *
 * Features:
 * - Thread-safe device management
 * - Dynamic configuration loading using device ID for model matching
 * - Priority-based control/polling coordination
 * - Comprehensive error handling
 * - Professional logging system
 */

#include <dirent.h>
#include <modbus/modbus.h>
#include <signal.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <vector>

#include "DeviceConfigurator.h"
#include "cJSON.h"
#include "device_manager/device.h"
#include "device_manager/mapping.h"
// ============================================================================
// GLOBAL CONFIGURATION
// ============================================================================
namespace Config {
constexpr const char* SERIAL_PORT = "/dev/ttyS3";
constexpr int BAUD_RATE = 115200;
constexpr int MODBUS_TIMEOUT_MS = 500;
constexpr int POLLING_INTERVAL_MS = 50;
constexpr int DEVICE_DELAY_MS = 10;
}  // namespace Config

// ============================================================================
// THREAD SYNCHRONIZATION MANAGER
// ============================================================================
class ThreadSyncManager {
 public:
  std::mutex m_port;                    // Port access control
  std::mutex m_queue;                   // Command queue control
  std::condition_variable cv_port;      // Port priority coordination
  std::condition_variable cv_shutdown;  // Shutdown notification
  std::atomic<bool> is_writing{false};  // Write operation flag
  std::atomic<bool> is_running{true};   // Global shutdown flag

  void requestWriteAccess() {
    std::lock_guard<std::mutex> lock(m_port);
    is_writing = true;
  }

  void releaseWriteAccess() {
    std::lock_guard<std::mutex> lock(m_port);
    is_writing = false;
    cv_port.notify_all();
  }

  void shutdown() {
    is_running = false;
    cv_port.notify_all();
    cv_shutdown.notify_all();
  }

  bool isRunning() const { return is_running.load(); }
};

// Global instance
static ThreadSyncManager g_sync;
static std::atomic<bool> g_shutdown_requested{false};

// Signal handler for graceful shutdown
void signalHandler(int signum) {
  if (g_shutdown_requested) {
    std::cout << "\n[SYSTEM] Force shutdown!" << std::endl;
    exit(signum);
  }

  std::cout << "\n[SYSTEM] Shutdown signal received (Ctrl+C)" << std::endl;
  std::cout << "[SYSTEM] Waiting for threads to finish..." << std::endl;

  g_shutdown_requested = true;
  g_sync.shutdown();
}

// ============================================================================
// MODBUS CONTEXT WRAPPER (RAII)
// ============================================================================
class ModbusContext {
 private:
  modbus_t* ctx_;
  bool connected_;

 public:
  explicit ModbusContext(const char* port, int baudrate)
      : ctx_(nullptr), connected_(false) {
    ctx_ = modbus_new_rtu(port, baudrate, 'N', 8, 1);
    if (!ctx_) {
      throw std::runtime_error("Failed to create Modbus context");
    }

    modbus_set_response_timeout(ctx_, 0, Config::MODBUS_TIMEOUT_MS * 1000);

    if (modbus_connect(ctx_) == -1) {
      std::string error = modbus_strerror(errno);
      modbus_free(ctx_);
      throw std::runtime_error("Modbus connection failed: " + error);
    }

    connected_ = true;
  }

  ~ModbusContext() {
    if (ctx_) {
      if (connected_) {
        modbus_close(ctx_);
      }
      modbus_free(ctx_);
    }
  }

  ModbusContext(const ModbusContext&) = delete;
  ModbusContext& operator=(const ModbusContext&) = delete;

  modbus_t* get() { return ctx_; }
  bool isConnected() const { return connected_; }
};

// ============================================================================
// MODBUS DEVICE MANAGER (Core Component)
// ============================================================================
class ModbusDeviceManager {
 private:
  std::unordered_map<std::string, std::shared_ptr<Device>> device_map_;
  std::map<std::string, std::shared_ptr<DeviceModel>> device_models_;

  std::queue<ControlCommand> command_queue_;
  std::mutex queue_mutex_;
  std::condition_variable queue_cv_;

  using ResponseCallback = void (*)(const std::string&);
  ResponseCallback response_callback_;

  // ========================================================================
  // HELPER METHODS
  // ========================================================================

  std::string readFile(const std::string& filename) const {
    std::ifstream file(filename);
    if (!file.is_open()) {
      throw std::runtime_error("Cannot open file: " + filename);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
  }

  RegType parseRegType(const std::string& type_str) const {
    if (type_str == "u32") return RegType::U32;
    if (type_str == "i32") return RegType::I32;
    if (type_str == "u16") return RegType::U16;
    std::cerr << "[WARN] Unknown register type: " << type_str << std::endl;
    return RegType::U16;
  }

  ByteOrder parseByteOrder(const std::string& order_str) const {
    if (order_str == "big_endian") return ByteOrder::BigEndian;
    if (order_str == "big_endian_swap") return ByteOrder::BigEndianSwap;
    if (order_str == "little_endian") return ByteOrder::LittleEndian;
    std::cerr << "[WARN] Unknown byte order: " << order_str << std::endl;
    return ByteOrder::BigEndian;
  }

  float inferGain(const std::string& register_name) const {
    if (register_name.find("Voltage") != std::string::npos) return 0.01f;
    if (register_name.find("Current") != std::string::npos) return 0.001f;
    if (register_name.find("Frequency") != std::string::npos) return 0.01f;
    if (register_name.find("power_factor") != std::string::npos) return 0.001f;
    if (register_name.find("Power") != std::string::npos) return 1.0f;
    if (register_name.find("Energy") != std::string::npos) return 0.01f;
    return 1.0f;
  }

  // ========================================================================
  // CONFIGURATION LOADING
  // ========================================================================

  bool loadMappingFile(const std::string& filepath) {
    try {
      std::cout << "[CONFIG] Loading: " << filepath << std::endl;

      std::string content = readFile(filepath);
      cJSON* json = cJSON_Parse(content.c_str());

      if (!json) {
        std::cerr << "[ERROR] Invalid JSON in: " << filepath << std::endl;
        return false;
      }

      struct JsonDeleter {
        cJSON* ptr;
        ~JsonDeleter() {
          if (ptr) cJSON_Delete(ptr);
        }
      } json_guard{json};

      auto model = std::make_shared<DeviceModel>();

      cJSON* model_name = cJSON_GetObjectItem(json, "device_model");
      if (!model_name || !model_name->valuestring) {
        std::cerr << "[ERROR] Missing 'device_model' in: " << filepath
                  << std::endl;
        return false;
      }
      model->model_name = model_name->valuestring;

      cJSON* modbus_cfg = cJSON_GetObjectItem(json, "modbus_config");
      if (!modbus_cfg) {
        std::cerr << "[ERROR] Missing 'modbus_config' in: " << filepath
                  << std::endl;
        return false;
      }

      auto getInt = [](cJSON* obj, const char* key, int default_val) {
        cJSON* item = cJSON_GetObjectItem(obj, key);
        return item ? item->valueint : default_val;
      };

      model->start_address = getInt(modbus_cfg, "start_address", 0);
      model->quantity = getInt(modbus_cfg, "quantity", 0);
      model->function_code = getInt(modbus_cfg, "function_code", 3);

      cJSON* byte_order = cJSON_GetObjectItem(modbus_cfg, "byte_order");
      if (byte_order && byte_order->valuestring) {
        model->byte_order = parseByteOrder(byte_order->valuestring);
      }

      cJSON* mapping_array = cJSON_GetObjectItem(json, "mapping");
      if (!mapping_array) {
        std::cerr << "[ERROR] Missing 'mapping' in: " << filepath << std::endl;
        return false;
      }

      int count = cJSON_GetArraySize(mapping_array);
      for (int i = 0; i < count; i++) {
        cJSON* map_item = cJSON_GetArrayItem(mapping_array, i);

        RegisterMapping reg;

        cJSON* addr = cJSON_GetObjectItem(map_item, "address");
        if (!addr) continue;
        reg.address = addr->valueint;

        cJSON* name = cJSON_GetObjectItem(map_item, "name");
        if (!name || !name->valuestring) continue;
        reg.name = name->valuestring;

        cJSON* type = cJSON_GetObjectItem(map_item, "type");
        reg.type = type && type->valuestring ? parseRegType(type->valuestring)
                                             : RegType::U16;

        cJSON* gain = cJSON_GetObjectItem(map_item, "gain");
        reg.gain = gain ? gain->valuedouble : inferGain(reg.name);

        cJSON* writable = cJSON_GetObjectItem(map_item, "writable");
        reg.writable = writable && cJSON_IsTrue(writable);

        model->registers.push_back(reg);
      }

      device_models_[model->model_name] = model;

      std::cout << "[CONFIG] ✓ Loaded model: " << model->model_name << " ("
                << model->registers.size() << " registers)" << std::endl;

      return true;

    } catch (const std::exception& e) {
      std::cerr << "[ERROR] Exception loading " << filepath << ": " << e.what()
                << std::endl;
      return false;
    }
  }

  void loadMappingsFromFolder(const std::string& folder_path) {
    std::cout << "[CONFIG] Scanning folder: " << folder_path << std::endl;

    DIR* dir = opendir(folder_path.c_str());
    if (!dir) {
      std::cerr << "[WARN] Cannot open mappings folder: " << folder_path
                << std::endl;
      return;
    }

    struct dirent* entry;
    int loaded_count = 0;

    while ((entry = readdir(dir)) != nullptr) {
      std::string filename = entry->d_name;

      if (filename == "." || filename == "..") continue;
      if (filename.size() < 5 ||
          filename.substr(filename.size() - 5) != ".json")
        continue;
      if (filename == "devices.json") continue;

      std::string filepath = folder_path + "/" + filename;
      if (loadMappingFile(filepath)) {
        loaded_count++;
      }
    }

    closedir(dir);
    std::cout << "[CONFIG] Loaded " << loaded_count << " mapping file(s)"
              << std::endl;
  }

  std::shared_ptr<DeviceModel> findMatchingModel(const std::string& device_id) {
    // STRATEGY 1: EXACT MATCH
    auto it = device_models_.find(device_id);
    if (it != device_models_.end()) {
      std::cout << "[CONFIG] Device '" << device_id
                << "' EXACT matched to model: " << it->first << std::endl;
      return it->second;
    }

    // STRATEGY 2: CLEAN ID & RETRY (remove suffix)
    std::string id_base = device_id;
    const std::vector<std::string> suffixes = {"_mapping", "_model", "_config",
                                               "_device", "_map"};

    for (const auto& suffix : suffixes) {
      size_t pos = id_base.find(suffix);
      if (pos != std::string::npos) {
        id_base = id_base.substr(0, pos);
        break;
      }
    }

    if (id_base != device_id) {
      it = device_models_.find(id_base);
      if (it != device_models_.end()) {
        std::cout << "[CONFIG] Device '" << device_id
                  << "' matched to model: " << it->first
                  << " (cleaned: " << id_base << ")" << std::endl;
        return it->second;
      }
    }

    // STRATEGY 3: CASE-INSENSITIVE MATCH
    std::string id_lower = id_base;
    std::transform(id_lower.begin(), id_lower.end(), id_lower.begin(),
                   ::tolower);

    for (auto& pair : device_models_) {
      std::string model_lower = pair.first;
      std::transform(model_lower.begin(), model_lower.end(),
                     model_lower.begin(), ::tolower);

      if (id_lower == model_lower) {
        std::cout << "[CONFIG] Device '" << device_id
                  << "' case-insensitive matched to model: " << pair.first
                  << std::endl;
        return pair.second;
      }
    }

    // STRATEGY 4: SUBSTRING MATCH
    for (auto& pair : device_models_) {
      std::string model_lower = pair.first;
      std::transform(model_lower.begin(), model_lower.end(),
                     model_lower.begin(), ::tolower);

      if (id_lower.find(model_lower) != std::string::npos ||
          model_lower.find(id_lower) != std::string::npos) {
        std::cout << "[CONFIG] Device '" << device_id
                  << "' substring matched to model: " << pair.first
                  << std::endl;
        return pair.second;
      }
    }

    // STRATEGY 5: FALLBACK
    if (!device_models_.empty()) {
      auto model = device_models_.begin()->second;
      std::cerr << "[WARN] Device '" << device_id
                << "' NO MATCH, using fallback model: " << model->model_name
                << std::endl;
      return model;
    }

    return nullptr;
  }

 public:
  ModbusDeviceManager() : response_callback_(nullptr) {}

  void setResponseCallback(ResponseCallback callback) {
    response_callback_ = callback;
  }

  // ========================================================================
  // PUBLIC API - Configuration
  // ========================================================================

  bool loadConfig(const std::string& devices_file,
                  const std::string& mappings_folder = "") {
    try {
      std::cout << "\n========================================" << std::endl;
      std::cout << "   LOADING CONFIGURATION" << std::endl;
      std::cout << "========================================" << std::endl;

      if (!mappings_folder.empty()) {
        loadMappingsFromFolder(mappings_folder);
      }

      if (device_models_.empty()) {
        std::cerr << "[ERROR] No device models loaded!" << std::endl;
        return false;
      }

      std::cout << "[CONFIG] Loading devices: " << devices_file << std::endl;

      std::string content = readFile(devices_file);
      cJSON* json = cJSON_Parse(content.c_str());

      if (!json) {
        std::cerr << "[ERROR] Invalid JSON in devices file" << std::endl;
        return false;
      }

      struct JsonDeleter {
        cJSON* ptr;
        ~JsonDeleter() {
          if (ptr) cJSON_Delete(ptr);
        }
      } json_guard{json};

      cJSON* devices_array = cJSON_GetObjectItem(json, "devices");
      if (!devices_array) {
        std::cerr << "[ERROR] Missing 'devices' array" << std::endl;
        return false;
      }

      int count = cJSON_GetArraySize(devices_array);
      std::cout << "[CONFIG] Found " << count << " device(s)" << std::endl;

      for (int i = 0; i < count; i++) {
        cJSON* dev_item = cJSON_GetArrayItem(devices_array, i);

        cJSON* id = cJSON_GetObjectItem(dev_item, "id");
        if (!id || !id->valuestring) {
          std::cerr << "[WARN] Device " << i << " missing 'id', skipping"
                    << std::endl;
          continue;
        }
        std::string device_id = id->valuestring;

        cJSON* name_json = cJSON_GetObjectItem(dev_item, "name");
        std::string device_name = (name_json && name_json->valuestring)
                                      ? name_json->valuestring
                                      : device_id;

        cJSON* type_json = cJSON_GetObjectItem(dev_item, "type");
        std::string device_type = (type_json && type_json->valuestring)
                                      ? type_json->valuestring
                                      : "meter";

        cJSON* slave_id_json = cJSON_GetObjectItem(dev_item, "slave_id");
        uint8_t slave_id = slave_id_json ? (uint8_t)slave_id_json->valueint : 1;

        // Find model using device ID
        auto model = findMatchingModel(device_id);
        if (!model) {
          std::cerr << "[ERROR] No model found for device: " << device_id
                    << std::endl;
          continue;
        }

        auto device = DeviceFactory::createDevice(device_type);
        if (!device) {
          std::cerr << "[ERROR] Unknown device type: " << device_type
                    << std::endl;
          continue;
        }

        device->id = device_id;
        device->name = device_name;
        device->type = device_type;
        device->slave_id = slave_id;
        device->model_ptr = model.get();

        device_map_[device_id] = device;

        std::cout << "[CONFIG] ✓ Created: " << device_id << " [" << device_type
                  << "] -> " << model->model_name << " (Slave " << (int)slave_id
                  << ")" << std::endl;
      }

      std::cout << "========================================" << std::endl;
      std::cout << "  CONFIGURATION COMPLETE" << std::endl;
      std::cout << "  Models:  " << device_models_.size() << std::endl;
      std::cout << "  Devices: " << device_map_.size() << std::endl;
      std::cout << "========================================\n" << std::endl;

      return !device_map_.empty();

    } catch (const std::exception& e) {
      std::cerr << "[FATAL] Configuration error: " << e.what() << std::endl;
      return false;
    }
  }

  void printDevices() const {
    std::cout << "\n=== REGISTERED DEVICES ===" << std::endl;
    for (const auto& pair : device_map_) {
      const auto& dev = pair.second;
      std::cout << "  • " << dev->id << " (" << dev->name << ")" << std::endl;
      std::cout << "    Type:  " << dev->type << std::endl;
      std::cout << "    Slave: " << (int)dev->slave_id << std::endl;
      if (dev->model_ptr) {
        std::cout << "    Model: " << dev->model_ptr->model_name << std::endl;
      }
    }
    std::cout << "==========================\n" << std::endl;
  }

  // ========================================================================
  // PUBLIC API - Data Access
  // ========================================================================

  bool pollDevice(const std::string& device_id, const uint16_t* raw_buffer) {
    auto it = device_map_.find(device_id);
    if (it == device_map_.end()) {
      std::cerr << "[ERROR] Device not found: " << device_id << std::endl;
      return false;
    }

    std::shared_ptr<Device> device = it->second;
    std::lock_guard<std::mutex> lock(device->data_mutex);
    device->processData(raw_buffer);

    return true;
  }

  bool getMeterData(const std::string& device_id, MeterData& out_data) {
    auto it = device_map_.find(device_id);
    if (it == device_map_.end()) return false;

    auto meter = std::dynamic_pointer_cast<MeterDevice>(it->second);
    if (!meter) return false;

    std::lock_guard<std::mutex> lock(meter->data_mutex);
    out_data = meter->storage;
    return true;
  }

  bool getInverterData(const std::string& device_id, InverterData& out_data) {
    auto it = device_map_.find(device_id);
    if (it == device_map_.end()) return false;

    auto inverter = std::dynamic_pointer_cast<InverterDevice>(it->second);
    if (!inverter) return false;

    std::lock_guard<std::mutex> lock(inverter->data_mutex);
    out_data = inverter->storage;
    return true;
  }

  const std::unordered_map<std::string, std::shared_ptr<Device>>& getDevices()
      const {
    return device_map_;
  }

  // ========================================================================
  // PUBLIC API - Control Commands
  // ========================================================================

  void pushControlCommand(const ControlCommand& cmd) {
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      command_queue_.push(cmd);
    }
    queue_cv_.notify_one();

    std::cout << "[CONTROL] Command queued: " << cmd.command_type
              << " for device " << cmd.device_id << std::endl;
  }

  void processControlCommands(modbus_t* ctx) {
    std::unique_lock<std::mutex> lock(queue_mutex_);

    bool has_command =
        queue_cv_.wait_for(lock, std::chrono::milliseconds(200), [this] {
          return !command_queue_.empty() || !g_sync.isRunning() ||
                 g_shutdown_requested;
        });

    if (!g_sync.isRunning() || g_shutdown_requested) {
      return;
    }

    while (!command_queue_.empty() && g_sync.isRunning() &&
           !g_shutdown_requested) {
      ControlCommand cmd = command_queue_.front();
      command_queue_.pop();

      lock.unlock();

      bool success = executeCommand(cmd, ctx);
      sendCommandResponse(cmd, success);

      lock.lock();
    }
  }

  bool executeCommand(ControlCommand& cmd, modbus_t* ctx) {
    auto it = device_map_.find(cmd.device_id);
    if (it == device_map_.end()) {
      cmd.result = "NACK";
      cmd.error_msg = "Device not found";
      return false;
    }

    if (!ctx) {
      cmd.result = "NACK";
      cmd.error_msg = "Modbus context not available";
      return false;
    }

    std::shared_ptr<Device> dev = it->second;

    {
      std::lock_guard<std::mutex> dev_lock(dev->data_mutex);
      if (!dev->handleCommand(cmd)) {
        cmd.result = "NACK";
        return false;
      }
    }

    if (dev->type == "inverter") {
      auto inverter = std::dynamic_pointer_cast<InverterDevice>(dev);
      if (!inverter) {
        cmd.result = "NACK";
        cmd.error_msg = "Invalid device type";
        return false;
      }

      std::vector<uint16_t> write_buffer(dev->model_ptr->quantity, 0);
      DataMapperP::unmapInverter(inverter->storage, *inverter->model_ptr,
                                 write_buffer.data());

      g_sync.requestWriteAccess();

      {
        std::lock_guard<std::mutex> lock(g_sync.m_port);

        modbus_set_slave(ctx, dev->slave_id);
        int rc = modbus_write_registers(ctx, dev->model_ptr->start_address,
                                        dev->model_ptr->quantity,
                                        write_buffer.data());

        if (rc == -1) {
          cmd.result = "NACK";
          cmd.error_msg = modbus_strerror(errno);
          g_sync.releaseWriteAccess();
          return false;
        }
      }

      g_sync.releaseWriteAccess();
    }

    std::cout << "[CONTROL] ✓ Executed: " << cmd.command_type << " = "
              << cmd.value << " for " << cmd.device_id << std::endl;

    cmd.result = "ACK";
    cmd.executed = true;
    return true;
  }

  void sendCommandResponse(const ControlCommand& cmd, bool success) {
    cJSON* resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "device_id", cmd.device_id.c_str());
    cJSON_AddStringToObject(resp, "command", cmd.command_type.c_str());
    cJSON_AddNumberToObject(resp, "value", cmd.value);
    cJSON_AddStringToObject(resp, "result", cmd.result.c_str());
    cJSON_AddNumberToObject(resp, "timestamp", cmd.timestamp);

    if (!success && !cmd.error_msg.empty()) {
      cJSON_AddStringToObject(resp, "error", cmd.error_msg.c_str());
    }

    char* json_str = cJSON_PrintUnformatted(resp);

    if (response_callback_) {
      response_callback_(json_str);
    } else {
      std::cout << "[RESPONSE] " << json_str << std::endl;
    }

    free(json_str);
    cJSON_Delete(resp);
  }

  void handleExternalControl(const std::string& json_cmd) {
    cJSON* cmd_json = cJSON_Parse(json_cmd.c_str());
    if (!cmd_json) {
      std::cerr << "[ERROR] Invalid control JSON" << std::endl;
      return;
    }

    struct JsonDeleter {
      cJSON* ptr;
      ~JsonDeleter() {
        if (ptr) cJSON_Delete(ptr);
      }
    } json_guard{cmd_json};

    ControlCommand cmd;

    cJSON* device_id = cJSON_GetObjectItem(cmd_json, "device_id");
    if (device_id && device_id->valuestring) {
      cmd.device_id = device_id->valuestring;
    }

    cJSON* command_type = cJSON_GetObjectItem(cmd_json, "command");
    if (command_type && command_type->valuestring) {
      cmd.command_type = command_type->valuestring;
    }

    cJSON* value = cJSON_GetObjectItem(cmd_json, "value");
    if (value) {
      cmd.value = value->valuedouble;
    }

    cmd.timestamp = time(nullptr);
    cmd.executed = false;

    pushControlCommand(cmd);
  }
};

// ============================================================================
// THREAD FUNCTIONS
// ============================================================================
#include "modbus_port.h"
void pollingThread(ModbusDeviceManager* manager) {
  try {
    ModbusPort port_1(Config::SERIAL_PORT);

    if (!port_1.init(Config::BAUD_RATE, Config::MODBUS_TIMEOUT_MS)) {
      std::cerr << "[POLLING] Failed to initialize Modbus port!" << std::endl;
      g_sync.shutdown();
      return;
    }
    while (g_sync.isRunning() && !g_shutdown_requested) {
      const auto& devices = manager->getDevices();

      if (devices.empty()) {
        std::cerr << "[POLLING] WARNING: No devices configured!" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(5));
        continue;
      }

      for (const auto& pair : devices) {
        if (!g_sync.isRunning() || g_shutdown_requested) break;

        const auto& dev = pair.second;

        if (!dev->model_ptr) {
          std::cerr << "[POLLING] WARNING: Device " << dev->id
                    << " has no model!" << std::endl;
          continue;
        }

        std::vector<uint16_t> buffer(dev->model_ptr->quantity, 0);

        bool success = false;
        {
          std::unique_lock<std::mutex> lock(g_sync.m_port);

          bool can_proceed =
              g_sync.cv_port.wait_for(lock, std::chrono::milliseconds(50), [] {
                return !g_sync.is_writing || !g_sync.isRunning() ||
                       g_shutdown_requested;
              });

          if (!g_sync.isRunning() || g_shutdown_requested) break;
          if (!can_proceed) continue;

          success = port_1.read(dev->slave_id, dev->model_ptr->start_address,
                                dev->model_ptr->quantity, buffer.data());
        }

        if (success) {
          manager->pollDevice(dev->id, buffer.data());

          if (dev->type == "meter") {
            MeterData m_data;

            if (manager->getMeterData(dev->id, m_data)) {
              time_t timestamp = time(nullptr);
              std::cout << "Timestamp: " << timestamp << std::endl;
              std::cout << "[Meter " << dev->id << "] "
                        << "V1=" << m_data.voltage_l1 << "V, "
                        << "V2=" << m_data.voltage_l2 << "V, "
                        << "V3=" << m_data.voltage_l3 << "V, "
                        << "I1=" << m_data.current_l1 << "A, "
                        << "P1=" << m_data.power_l1 << "W, "
                        << "P2=" << m_data.power_l2 << "W, "
                        << "P3=" << m_data.power_l3 << "W, "
                        << "I1=" << m_data.frequency << "Hz, "
                        << "P=" << m_data.power << "W" << std::endl;
            }
          } else if (dev->type == "inverter") {
            InverterData inv_data;
            if (manager->getInverterData(dev->id, inv_data)) {
              std::cout << "[Inverter " << dev->id << "] "
                        << "Target=" << inv_data.target_power_percent
                        << "%, "
                        // << "Output=" << inv_data.output_power << "W, "
                        << "Energy=" << inv_data.total_energy << "kWh"
                        << std::endl;
            }
            // } else if (dev->type == "battery") {
            //   BatteryData bat_data;
            //   if (manager->getBatteryData(dev->id, bat_data)) {
            //     std::cout << "[Battery " << dev->id << "] "
            //               << "SOC=" << bat_data.soc << "%, "
            //               << "V=" << bat_data.voltage << "V, "
            //               << "I=" << bat_data.current << "A" << std::endl;
            // }
          }
        } else {
          std::cerr << "[POLLING] " << dev->id << std::endl;
        }

        for (int i = 0; i < Config::DEVICE_DELAY_MS / 10; ++i) {
          if (!g_sync.isRunning() || g_shutdown_requested) break;
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
      }

      for (int i = 0; i < Config::POLLING_INTERVAL_MS / 10; ++i) {
        if (!g_sync.isRunning() || g_shutdown_requested) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    }

    std::cout << "[POLLING] Thread stopped gracefully" << std::endl;

  } catch (const std::exception& e) {
    std::cerr << "[POLLING] FATAL: " << e.what() << std::endl;
    g_sync.shutdown();
  }
}

void controlThread(ModbusDeviceManager* manager) {
  try {
    ModbusContext ctx(Config::SERIAL_PORT, Config::BAUD_RATE);
    std::cout << "[CONTROL] Thread started" << std::endl;

    while (g_sync.isRunning() && !g_shutdown_requested) {
      manager->processControlCommands(ctx.get());
    }

    std::cout << "[CONTROL] Thread stopped gracefully" << std::endl;

  } catch (const std::exception& e) {
    std::cerr << "[CONTROL] FATAL: " << e.what() << std::endl;
    g_sync.shutdown();
  }
}

// ============================================================================
// RESPONSE HANDLER
// ============================================================================

void responseHandler(const std::string& json) {
  std::cout << "[RESPONSE] " << json << std::endl;
}

// ============================================================================
// MAIN FUNCTION
// ============================================================================

#include "modbus_port.h"

int main(int argc, char** argv) {
  signal(SIGINT, signalHandler);
  signal(SIGTERM, signalHandler);

  std::cout << R"(
========================================
   MODBUS DEVICE MANAGER v2.1
========================================
)" << std::endl;

  std::string devices_file = "devices.json";
  std::string mappings_folder = "./mappings";

  if (argc > 1) devices_file = argv[1];
  if (argc > 2) mappings_folder = argv[2];

  try {
    ModbusDeviceManager manager;

    manager.setResponseCallback(responseHandler);

    if (!manager.loadConfig(devices_file, mappings_folder)) {
      std::cerr << "[FATAL] Configuration loading failed" << std::endl;
      return 1;
    }

    manager.printDevices();

    std::cout << "[SYSTEM] Starting worker threads..." << std::endl;
    std::thread polling_worker(pollingThread, &manager);
    std::thread control_worker(controlThread, &manager);

    std::this_thread::sleep_for(std::chrono::seconds(1));

    std::cout << "\n========================================" << std::endl;
    std::cout << "   SYSTEM RUNNING" << std::endl;
    std::cout << "   Press Ctrl+C to stop" << std::endl;
    std::cout << "========================================\n" << std::endl;

    polling_worker.join();
    control_worker.join();

    std::cout << "\n========================================" << std::endl;
    std::cout << "   SHUTDOWN COMPLETE" << std::endl;
    std::cout << "========================================" << std::endl;

  } catch (const std::exception& e) {
    std::cerr << "\n[FATAL] " << e.what() << std::endl;
    g_sync.shutdown();
    return 1;
  }

  return 0;
}
