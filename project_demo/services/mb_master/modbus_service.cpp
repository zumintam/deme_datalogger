#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <queue>
#include <thread>
#include <vector>

#include "modbus_ex.h"

using json = nlohmann::json;

// ==================== GLOBAL SYNCHRONIZATION ====================
std::mutex zmq_mutex;
std::mutex console_mutex;
std::mutex data_mutex;
std::mutex control_queue_mutex;

std::atomic<bool> stop_flag(false);

// Condition variable cho control commands
std::condition_variable control_cv;

// ==================== DATA STRUCTURES ====================
struct DeviceInfo {
  int slave_id;
  std::string device_name;
  std::string device_type;
};

struct ThreadConfig {
  std::string config_file;
  std::string device_type;
  std::vector<DeviceInfo> devices;
  int poll_interval_ms;
  std::string serial_port;
};

// Dữ liệu real-time từ devices
struct DeviceData {
  std::string device_name;
  double power;        // Công suất (W)
  double voltage;      // Điện áp (V)
  double current;      // Dòng điện (A)
  double frequency;    // Tần số (Hz)
  std::string status;  // "OK", "ERROR", "OFF"
  std::chrono::system_clock::time_point timestamp;
};

// Lệnh điều khiển
enum class ControlCommand {
  TURN_ON,
  TURN_OFF,
  SET_POWER_LIMIT,
  RESET_ALARM,
  EMERGENCY_STOP
};

struct ControlMessage {
  ControlCommand command;
  std::string target_device;  // Tên device cần điều khiển
  double value;               // Giá trị (ví dụ: power limit %)
  std::string params;         // Tham số thêm (JSON string)
};

// Shared data storage
std::map<std::string, DeviceData> device_data_map;
std::queue<ControlMessage> control_queue;

// ==================== UTILITY FUNCTIONS ====================
void safe_print(const std::string& message) {
  std::lock_guard<std::mutex> lock(console_mutex);
  std::cout << "["
            << std::chrono::system_clock::now().time_since_epoch().count()
            << "] " << message << std::endl;
}

bool loadDeviceConfig(const std::string& filename, ThreadConfig& config) {
  std::ifstream file(filename);
  if (!file.is_open()) {
    safe_print("Cannot open config file: " + filename);
    return false;
  }

  try {
    json j;
    file >> j;
    file.close();

    if (j.contains("inverters")) {
      for (const auto& inv : j["inverters"]) {
        DeviceInfo info;
        info.slave_id = inv["slave_id"];
        info.device_name = inv["device_name"];
        info.device_type = "inverter";
        config.devices.push_back(info);
      }
    } else {
      DeviceInfo info;
      info.slave_id = j["slave_id"];
      info.device_name = j.value("device_model", "Unknown");
      info.device_type = "meter";
      config.devices.push_back(info);
    }

    return true;

  } catch (const std::exception& e) {
    safe_print("Error parsing config: " + std::string(e.what()));
    return false;
  }
}

// Parse data và lưu vào shared map
void updateDeviceData(const std::string& device_name, const uint16_t* raw_data,
                      Config& config) {
  std::lock_guard<std::mutex> lock(data_mutex);

  DeviceData data;
  data.device_name = device_name;
  data.timestamp = std::chrono::system_clock::now();

  // Parse các giá trị quan trọng (giả sử từ raw_data)
  // Điều chỉnh theo register map thực tế của bạn
  data.power =
      (raw_data[12] << 16 | raw_data[13]) / 10.0;  // Total_active_power
  data.voltage = (raw_data[34] << 16 | raw_data[35]) / 10.0;    // Voltage_L1
  data.current = (raw_data[20] << 16 | raw_data[21]) / 1000.0;  // Current_L1
  data.frequency = raw_data[19] / 100.0;                        // Frequency
  data.status = "OK";

  device_data_map[device_name] = data;
}

// ==================== MODBUS CONTROL FUNCTIONS ====================
bool sendControlCommand(modbus_t* ctx, int slave_id, ControlCommand cmd,
                        double value) {
  modbus_set_slave(ctx, slave_id);

  switch (cmd) {
    case ControlCommand::TURN_ON:
      // Ghi register để bật inverter (ví dụ register 1000 = 1)
      return modbus_write_register(ctx, 1000, 1) != -1;

    case ControlCommand::TURN_OFF:
      // Ghi register để tắt inverter (ví dụ register 1000 = 0)
      return modbus_write_register(ctx, 1000, 0) != -1;

    case ControlCommand::SET_POWER_LIMIT:
      // Ghi giới hạn công suất (ví dụ register 1001, giá trị %)
      return modbus_write_register(ctx, 1001,
                                   static_cast<uint16_t>(value * 10)) != -1;

    case ControlCommand::RESET_ALARM:
      // Reset alarm (ví dụ register 1002 = 1)
      return modbus_write_register(ctx, 1002, 1) != -1;

    case ControlCommand::EMERGENCY_STOP:
      // Emergency stop (ví dụ register 1003 = 1)
      return modbus_write_register(ctx, 1003, 1) != -1;

    default:
      return false;
  }
}

// ==================== READER THREAD ====================
void deviceReaderThread(ThreadConfig thread_config, zmq::socket_t& zmq_socket,
                        const std::string& thread_name) {
  ModbusMaster mb;
  Config config;
  ModbusContextPtr ctx;

  safe_print("[" + thread_name + "] Starting...");

  if (!initModbusConfig(config, thread_config.serial_port, ctx, mb,
                        thread_config.config_file)) {
    safe_print("[" + thread_name + "] Failed to initialize Modbus");
    return;
  }

  safe_print("[" + thread_name + "] Initialized with " +
             std::to_string(thread_config.devices.size()) + " devices");

  uint16_t raw_data[256] = {0};
  int consecutive_errors = 0;
  const int MAX_CONSECUTIVE_ERRORS = 10;

  while (!stop_flag && consecutive_errors < MAX_CONSECUTIVE_ERRORS) {
    bool any_success = false;

    for (const auto& device : thread_config.devices) {
      if (stop_flag) break;

      memset(raw_data, 0, sizeof(raw_data));

      config.meter.modbus.slave_id = device.slave_id;
      modbus_set_slave(ctx.get(), device.slave_id);

      bool read_success =
          readModbusData(ctx.get(), config.meter.modbus, raw_data, mb);

      if (!read_success) {
        safe_print("[" + thread_name + "] ✗ Failed to read " +
                   device.device_name);

        // Cập nhật status ERROR
        {
          std::lock_guard<std::mutex> lock(data_mutex);
          if (device_data_map.count(device.device_name)) {
            device_data_map[device.device_name].status = "ERROR";
          }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        continue;
      }

      any_success = true;

      // Cập nhật dữ liệu vào shared map
      updateDeviceData(device.device_name, raw_data, config);

      // Parse to JSON và gửi ZMQ
      std::string json_data = config.meter.parse2Json(raw_data);
      std::string envelope =
          createEnvelopeJson(device.device_name, "OK", json_data);

      {
        std::lock_guard<std::mutex> lock(zmq_mutex);
        if (sendZmqMessage(zmq_socket, envelope)) {
          safe_print("[" + thread_name + "] ✓ " + device.device_name + " sent");
        } else {
          safe_print("[" + thread_name + "] ✗ Failed to send " +
                     device.device_name);
        }
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }

    if (any_success) {
      consecutive_errors = 0;
    } else {
      consecutive_errors++;
      safe_print("[" + thread_name + "] No device responded (" +
                 std::to_string(consecutive_errors) + "/" +
                 std::to_string(MAX_CONSECUTIVE_ERRORS) + ")");
    }

    std::this_thread::sleep_for(
        std::chrono::milliseconds(thread_config.poll_interval_ms));
  }

  safe_print("[" + thread_name + "] Thread stopped");
  cleanupModbus(ctx);
}

// ==================== CONTROL LOGIC THREAD ====================
void controlLogicThread(const std::string& serial_port) {
  safe_print("[CONTROL] Starting control logic thread...");

  // Khởi tạo Modbus context riêng cho điều khiển
  modbus_t* control_ctx = modbus_new_rtu(serial_port.c_str(), 9600, 'N', 8, 1);
  if (control_ctx == nullptr) {
    safe_print("[CONTROL] Failed to create Modbus context");
    return;
  }

  if (modbus_connect(control_ctx) == -1) {
    safe_print("[CONTROL] Failed to connect Modbus");
    modbus_free(control_ctx);
    return;
  }

  modbus_set_response_timeout(control_ctx, 1, 0);

  safe_print("[CONTROL] Control logic initialized");

  while (!stop_flag) {
    // ===== LOGIC 1: Kiểm tra giới hạn công suất =====
    {
      std::lock_guard<std::mutex> lock(data_mutex);

      const double MAX_TOTAL_POWER = 50000.0;      // 50kW
      const double MAX_SINGLE_INVERTER = 10000.0;  // 10kW

      double total_power = 0.0;

      for (const auto& [name, data] : device_data_map) {
        if (data.device_type == "inverter" && data.status == "OK") {
          total_power += data.power;

          // Giới hạn công suất từng inverter
          if (data.power > MAX_SINGLE_INVERTER) {
            safe_print("[CONTROL] ⚠ " + name +
                       " over limit: " + std::to_string(data.power) + "W");

            // Thêm lệnh giảm công suất xuống 90%
            ControlMessage msg;
            msg.command = ControlCommand::SET_POWER_LIMIT;
            msg.target_device = name;
            msg.value = 90.0;  // 90%

            std::lock_guard<std::mutex> qlock(control_queue_mutex);
            control_queue.push(msg);
            control_cv.notify_one();
          }
        }
      }

      // Kiểm tra tổng công suất
      if (total_power > MAX_TOTAL_POWER) {
        safe_print("[CONTROL] ⚠⚠ TOTAL POWER EXCEEDED: " +
                   std::to_string(total_power) + "W");

        // Emergency: Giảm tất cả inverter xuống 80%
        for (const auto& [name, data] : device_data_map) {
          if (data.device_type == "inverter") {
            ControlMessage msg;
            msg.command = ControlCommand::SET_POWER_LIMIT;
            msg.target_device = name;
            msg.value = 80.0;

            std::lock_guard<std::mutex> qlock(control_queue_mutex);
            control_queue.push(msg);
          }
        }
        control_cv.notify_one();
      }
    }

    // ===== LOGIC 2: Kiểm tra điện áp =====
    {
      std::lock_guard<std::mutex> lock(data_mutex);

      const double MIN_VOLTAGE = 200.0;
      const double MAX_VOLTAGE = 250.0;

      for (const auto& [name, data] : device_data_map) {
        if (data.voltage < MIN_VOLTAGE || data.voltage > MAX_VOLTAGE) {
          safe_print("[CONTROL] ⚠ Voltage abnormal at " + name + ": " +
                     std::to_string(data.voltage) + "V");

          // Tắt inverter để bảo vệ
          ControlMessage msg;
          msg.command = ControlCommand::TURN_OFF;
          msg.target_device = name;

          std::lock_guard<std::mutex> qlock(control_queue_mutex);
          control_queue.push(msg);
          control_cv.notify_one();
        }
      }
    }

    // ===== LOGIC 3: Xử lý các lệnh trong queue =====
    {
      std::unique_lock<std::mutex> qlock(control_queue_mutex);

      while (!control_queue.empty()) {
        ControlMessage msg = control_queue.front();
        control_queue.pop();
        qlock.unlock();

        // Tìm slave_id của device
        int target_slave_id = -1;
        {
          std::lock_guard<std::mutex> dlock(data_mutex);
          for (const auto& [name, data] : device_data_map) {
            if (name == msg.target_device) {
              // Giả sử slave_id = index + 10
              target_slave_id = 11;  // Cần map thực tế
              break;
            }
          }
        }

        if (target_slave_id != -1) {
          std::string cmd_str;
          switch (msg.command) {
            case ControlCommand::TURN_ON:
              cmd_str = "TURN_ON";
              break;
            case ControlCommand::TURN_OFF:
              cmd_str = "TURN_OFF";
              break;
            case ControlCommand::SET_POWER_LIMIT:
              cmd_str = "SET_POWER_LIMIT(" + std::to_string(msg.value) + "%)";
              break;
            default:
              cmd_str = "UNKNOWN";
              break;
          }

          safe_print("[CONTROL] Executing: " + cmd_str + " on " +
                     msg.target_device);

          bool success = sendControlCommand(control_ctx, target_slave_id,
                                            msg.command, msg.value);

          if (success) {
            safe_print("[CONTROL] ✓ Command executed successfully");
          } else {
            safe_print("[CONTROL] ✗ Command failed");
          }
        }

        qlock.lock();
      }
    }

    std::this_thread::sleep_for(
        std::chrono::milliseconds(500));  // Check mỗi 500ms
  }

  modbus_close(control_ctx);
  modbus_free(control_ctx);
  safe_print("[CONTROL] Thread stopped");
}

// ==================== MONITORING THREAD ====================
void monitoringThread() {
  safe_print("[MONITOR] Starting monitoring thread...");

  while (!stop_flag) {
    std::this_thread::sleep_for(std::chrono::seconds(5));

    std::lock_guard<std::mutex> lock(data_mutex);

    safe_print("\n========== SYSTEM STATUS ==========");

    double total_power = 0.0;
    int online_count = 0;

    for (const auto& [name, data] : device_data_map) {
      auto age = std::chrono::duration_cast<std::chrono::seconds>(
                     std::chrono::system_clock::now() - data.timestamp)
                     .count();

      std::string status_icon = (data.status == "OK" && age < 10) ? "✓" : "✗";

      safe_print(status_icon + " " + name + ": " + std::to_string(data.power) +
                 "W, " + std::to_string(data.voltage) + "V, " + data.status +
                 " (age: " + std::to_string(age) + "s)");

      if (data.status == "OK" && age < 10) {
        total_power += data.power;
        online_count++;
      }
    }

    safe_print("TOTAL: " + std::to_string(total_power) + "W, " +
               std::to_string(online_count) + " devices online");
    safe_print("===================================\n");
  }

  safe_print("[MONITOR] Thread stopped");
}

// ==================== MAIN ====================
int main() {
  ZmqDealer zmq_dealer;

  try {
    if (!initZmqDealer(zmq_dealer.ctx, zmq_dealer.dealer, ZMQ_ENDPOINT)) {
      std::cerr << "Failed to initialize ZMQ dealer" << std::endl;
      return EXIT_FAILURE;
    }

    std::cout << "ZMQ initialized successfully" << std::endl;

    ThreadConfig meter_config, inverter_config;

    meter_config.config_file = "meter_dpm380.json";
    meter_config.device_type = "meter";
    meter_config.poll_interval_ms = 1000;
    meter_config.serial_port = MODBUS_PORT_S3;

    inverter_config.config_file = "inverters_config.json";
    inverter_config.device_type = "inverter";
    inverter_config.poll_interval_ms = 2000;
    inverter_config.serial_port = MODBUS_PORT_S3;

    if (!loadDeviceConfig(meter_config.config_file, meter_config)) {
      std::cerr << "Failed to load meter config" << std::endl;
      return EXIT_FAILURE;
    }

    if (!loadDeviceConfig(inverter_config.config_file, inverter_config)) {
      std::cerr << "Failed to load inverter config" << std::endl;
      return EXIT_FAILURE;
    }

    std::cout << "\n=== Starting Multi-threaded System ===" << std::endl;
    std::cout << "Meter devices: " << meter_config.devices.size() << std::endl;
    std::cout << "Inverter devices: " << inverter_config.devices.size()
              << std::endl;
    std::cout << "Press Ctrl+C to stop...\n" << std::endl;

    std::vector<std::thread> threads;

    // Reader threads
    threads.emplace_back(deviceReaderThread, meter_config,
                         std::ref(zmq_dealer.dealer), "METER");
    threads.emplace_back(deviceReaderThread, inverter_config,
                         std::ref(zmq_dealer.dealer), "INVERTER");

    // Control logic thread
    threads.emplace_back(controlLogicThread, MODBUS_PORT_S3);

    // Monitoring thread
    threads.emplace_back(monitoringThread);

    std::this_thread::sleep_for(std::chrono::hours(24));

    stop_flag = true;
    control_cv.notify_all();
    std::cout << "\nStopping all threads..." << std::endl;

    for (auto& t : threads) {
      if (t.joinable()) {
        t.join();
      }
    }

    std::cout << "All threads stopped" << std::endl;

  } catch (const std::exception& e) {
    std::cerr << "Exception: " << e.what() << std::endl;
    stop_flag = true;
    control_cv.notify_all();
    cleanupZmq(zmq_dealer.ctx, zmq_dealer.dealer);
    return EXIT_FAILURE;
  }

  cleanupZmq(zmq_dealer.ctx, zmq_dealer.dealer);
  std::cout << "Program terminated successfully" << std::endl;

  return EXIT_SUCCESS;
}