#include "DeviceConfigurator.h"

#include <algorithm>
#include <cctype>
#include <cstring>

// ============================================================================
// STATIC INITIALIZATION
// ============================================================================
LogLevel Logger::min_level_ = LogLevel::INFO;
std::mutex Logger::log_mutex_;
DeviceConfigurator::LoadMetrics DeviceConfigurator::metrics_;

// ============================================================================
// RAII GUARDS
// ============================================================================
namespace detail {

// RAII wrapper for cJSON
struct JsonGuard {
  cJSON* ptr;
  explicit JsonGuard(cJSON* p) : ptr(p) {}
  ~JsonGuard() {
    if (ptr) cJSON_Delete(ptr);
  }

  JsonGuard(const JsonGuard&) = delete;
  JsonGuard& operator=(const JsonGuard&) = delete;

  operator bool() const { return ptr != nullptr; }
  cJSON* get() const { return ptr; }
};

// RAII wrapper for DIR
struct DirGuard {
  DIR* ptr;
  explicit DirGuard(const char* path) : ptr(opendir(path)) {}
  ~DirGuard() {
    if (ptr) closedir(ptr);
  }

  DirGuard(const DirGuard&) = delete;
  DirGuard& operator=(const DirGuard&) = delete;

  operator bool() const { return ptr != nullptr; }
  DIR* get() const { return ptr; }
};

}  // namespace detail

// ============================================================================
// JSON UTILITIES (with safety checks)
// ============================================================================
struct JsonUtils {
  static bool hasKey(const cJSON* obj, const char* key) {
    return cJSON_GetObjectItemCaseSensitive(obj, key) != nullptr;
  }

  static std::string getString(const cJSON* obj, const char* key,
                               const std::string& def) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (item && cJSON_IsString(item) && item->valuestring) {
      return std::string(item->valuestring);
    }
    return def;
  }

  static int getInt(const cJSON* obj, const char* key, int def) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (item && cJSON_IsNumber(item)) {
      return item->valueint;
    }
    return def;
  }

  static double getDouble(const cJSON* obj, const char* key, double def) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (item && cJSON_IsNumber(item)) {
      return item->valuedouble;
    }
    return def;
  }

  static bool getBool(const cJSON* obj, const char* key, bool def) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (item && cJSON_IsBool(item)) {
      return cJSON_IsTrue(item);
    }
    return def;
  }
};

// ============================================================================
// FILE SYSTEM HELPERS (C++14 compatible)
// ============================================================================
bool DeviceConfigurator::fileExists(const std::string& path) {
  struct stat buffer;
  return (stat(path.c_str(), &buffer) == 0);
}

bool DeviceConfigurator::isDirectory(const std::string& path) {
  struct stat buffer;
  if (stat(path.c_str(), &buffer) != 0) return false;
  return S_ISDIR(buffer.st_mode);
}

std::vector<std::string> DeviceConfigurator::listJsonFiles(
    const std::string& folder) {
  std::vector<std::string> files;

  detail::DirGuard dir(folder.c_str());
  if (!dir) return files;

  struct dirent* entry;
  while ((entry = readdir(dir.get())) != nullptr) {
    std::string filename = entry->d_name;

    // Skip . and ..
    if (filename == "." || filename == "..") continue;

    // Check for .json extension
    if (filename.size() < 5) continue;
    if (filename.substr(filename.size() - 5) != ".json") continue;

    files.push_back(folder + "/" + filename);
  }

  return files;
}

// ============================================================================
// MAIN CONFIGURATION
// ============================================================================
ConfigError DeviceConfigurator::configure(ModbusDeviceManager& manager,
                                          const std::string& devices_file,
                                          const std::string& mappings_folder,
                                          const Config& config) {
  Logger::setLevel(config.log_level);
  metrics_ = LoadMetrics();  // Reset metrics

  Logger::log(LogLevel::INFO, "Starting configuration sequence");

  // Load all device models
  ConfigError models_result = loadAllModels(manager, mappings_folder, config);
  if (models_result.hasError()) {
    Logger::log(LogLevel::FATAL, "Model loading failed");
    return models_result;
  }

  // Instantiate devices
  ConfigError devices_result =
      instantiateDevices(manager, devices_file, config);
  if (devices_result.hasError()) {
    Logger::log(LogLevel::FATAL, "Device instantiation failed");
    return devices_result;
  }

  Logger::log(LogLevel::INFO, "Configuration completed successfully");

  return ConfigError::success();
}

// ============================================================================
// MODEL LOADING
// ============================================================================
ConfigError DeviceConfigurator::loadAllModels(ModbusDeviceManager& manager,
                                              const std::string& folder,
                                              const Config& config) {
  if (!fileExists(folder)) {
    return ConfigError::fileNotFound(folder);
  }

  if (!isDirectory(folder)) {
    return ConfigError(ConfigError::Type::InvalidFormat,
                       "Path is not a directory", folder);
  }

  std::vector<std::string> json_files = listJsonFiles(folder);

  for (const auto& filepath : json_files) {
    metrics_.files_processed++;

    ConfigError result = loadMappingFile(manager, filepath, config);
    if (result.hasError()) {
      metrics_.errors_encountered++;

      if (!config.skip_invalid_files) {
        return result;
      }

      Logger::log(LogLevel::WARN,
                  "Skipping invalid file: " + result.toString());
    }
  }

  if (metrics_.models_loaded == 0) {
    Logger::log(LogLevel::WARN, "No models loaded from folder", folder);
  }

  return ConfigError::success();
}

ConfigError DeviceConfigurator::loadMappingFile(ModbusDeviceManager& manager,
                                                const std::string& filepath,
                                                const Config& config) {
  Logger::log(LogLevel::DEBUG, "Loading mapping file", filepath);

  // Read file
  std::string content;
  try {
    content = readFile(filepath);
  } catch (const std::exception& e) {
    return ConfigError::fileNotFound(filepath);
  }

  if (content.empty()) {
    return ConfigError(ConfigError::Type::InvalidFormat, "Empty file",
                       filepath);
  }

  // Parse JSON
  detail::JsonGuard json_guard(cJSON_Parse(content.c_str()));
  if (!json_guard) {
    const char* error_ptr = cJSON_GetErrorPtr();
    std::string error_msg = error_ptr ? error_ptr : "Unknown parse error";
    return ConfigError::parseError(error_msg, filepath);
  }

  cJSON* json = json_guard.get();

  // Validate JSON structure
  if (config.validate_schemas) {
    ValidationResult validation = validateMappingJson(json, filepath);
    if (!validation.valid) {
      std::string errors;
      for (size_t i = 0; i < validation.errors.size(); i++) {
        errors += validation.errors[i];
        if (i < validation.errors.size() - 1) errors += "; ";
      }
      return ConfigError::validationError(errors, filepath);
    }

    // Log warnings
    for (const auto& warn : validation.warnings) {
      Logger::log(LogLevel::WARN, warn, filepath);
    }
  }

  // Create device model
  std::shared_ptr<DeviceModel> model = std::make_shared<DeviceModel>();
  model->model_name = JsonUtils::getString(json, "device_model", "Unknown");

  // Parse modbus configuration
  cJSON* modbus_cfg = cJSON_GetObjectItem(json, "modbus_config");
  if (modbus_cfg && cJSON_IsObject(modbus_cfg)) {
    model->start_address = JsonUtils::getInt(modbus_cfg, "start_address", 0);
    model->quantity = JsonUtils::getInt(modbus_cfg, "quantity", 0);
    model->byte_order = parseByteOrder(
        JsonUtils::getString(modbus_cfg, "byte_order", "big_endian"));
  }

  // Parse register mappings
  cJSON* mapping_array = cJSON_GetObjectItem(json, "mapping");
  if (mapping_array && cJSON_IsArray(mapping_array)) {
    int count = cJSON_GetArraySize(mapping_array);

    for (int i = 0; i < count; i++) {
      cJSON* item = cJSON_GetArrayItem(mapping_array, i);
      if (!item || !cJSON_IsObject(item)) continue;

      RegisterMapping reg;
      reg.address = JsonUtils::getInt(item, "address", 0);
      reg.name = JsonUtils::getString(item, "name", "");
      reg.type = parseRegType(JsonUtils::getString(item, "type", "u16"));
      reg.writable = JsonUtils::getBool(item, "writable", false);

      // Handle gain
      if (JsonUtils::hasKey(item, "gain")) {
        reg.gain = static_cast<float>(JsonUtils::getDouble(item, "gain", 1.0));
      } else {
        reg.gain = inferGain(reg.name);
      }

      if (!reg.name.empty()) {
        model->registers.push_back(reg);
      }
    }
  }

  // Validate the model
  if (config.validate_schemas) {
    ValidationResult validation = validateDeviceModel(*model);
    if (!validation.valid) {
      std::string errors;
      for (size_t i = 0; i < validation.errors.size(); i++) {
        errors += validation.errors[i];
        if (i < validation.errors.size() - 1) errors += "; ";
      }
      return ConfigError::validationError(errors, filepath);
    }
  }

  // Add model to manager
  manager.addModel(model->model_name, model);
  metrics_.models_loaded++;

  Logger::log(LogLevel::INFO, "Loaded model '" + model->model_name + "'");

  return ConfigError::success();
}

// ============================================================================
// DEVICE INSTANTIATION
// ============================================================================
ConfigError DeviceConfigurator::instantiateDevices(
    ModbusDeviceManager& manager, const std::string& devices_file,
    const Config& config) {
  Logger::log(LogLevel::DEBUG, "Loading devices file", devices_file);

  // Read file
  std::string content;
  try {
    content = readFile(devices_file);
  } catch (const std::exception& e) {
    return ConfigError::fileNotFound(devices_file);
  }

  // Parse JSON
  detail::JsonGuard json_guard(cJSON_Parse(content.c_str()));
  if (!json_guard) {
    const char* error_ptr = cJSON_GetErrorPtr();
    std::string error_msg = error_ptr ? error_ptr : "Unknown parse error";
    return ConfigError::parseError(error_msg, devices_file);
  }

  cJSON* json = json_guard.get();

  // Validate structure
  if (config.validate_schemas) {
    ValidationResult validation = validateDevicesJson(json, devices_file);
    if (!validation.valid) {
      std::string errors;
      for (size_t i = 0; i < validation.errors.size(); i++) {
        errors += validation.errors[i];
        if (i < validation.errors.size() - 1) errors += "; ";
      }
      return ConfigError::validationError(errors, devices_file);
    }
  }

  // Parse devices array
  cJSON* devices_array = cJSON_GetObjectItem(json, "devices");
  if (!devices_array || !cJSON_IsArray(devices_array)) {
    return ConfigError(ConfigError::Type::MissingRequiredField,
                       "Missing or invalid 'devices' array", devices_file);
  }

  int count = cJSON_GetArraySize(devices_array);

  for (int i = 0; i < count; i++) {
    cJSON* item = cJSON_GetArrayItem(devices_array, i);
    if (!item || !cJSON_IsObject(item)) continue;

    std::string id = JsonUtils::getString(item, "id", "");
    if (id.empty()) {
      Logger::log(LogLevel::WARN, "Skipping device: missing ID");
      continue;
    }

    std::string type = JsonUtils::getString(item, "type", "meter");
    uint8_t slave_id =
        static_cast<uint8_t>(JsonUtils::getInt(item, "slave_id", 1));

    // Create device
    std::shared_ptr<Device> device = DeviceFactory::createDevice(type);
    if (!device) {
      Logger::log(LogLevel::WARN, "Unknown device type: " + type, id);
      continue;
    }

    device->id = id;
    device->name = JsonUtils::getString(item, "name", id);
    device->slave_id = slave_id;

    // Match model
    std::shared_ptr<DeviceModel> model = manager.findMatchingModel(id);
    if (model) {
      device->model_ptr = model.get();
      manager.addDevice(device);
      metrics_.devices_loaded++;

      Logger::log(LogLevel::INFO, "Instantiated device '" + id + "'");
    } else {
      Logger::log(LogLevel::WARN, "No matching model found for device: " + id);
    }
  }

  return ConfigError::success();
}

// ============================================================================
// VALIDATION
// ============================================================================
ValidationResult DeviceConfigurator::validateMappingJson(
    const cJSON* json, const std::string& filepath) {
  ValidationResult result;

  if (!JsonUtils::hasKey(json, "device_model")) {
    result.addError("Missing required field: 'device_model'");
  }

  if (!JsonUtils::hasKey(json, "mapping")) {
    result.addError("Missing required field: 'mapping'");
  } else {
    const cJSON* mapping = cJSON_GetObjectItem(json, "mapping");
    if (!cJSON_IsArray(mapping)) {
      result.addError("Field 'mapping' must be an array");
    } else if (cJSON_GetArraySize(mapping) == 0) {
      result.addWarning("Mapping array is empty");
    }
  }

  if (!JsonUtils::hasKey(json, "modbus_config")) {
    result.addWarning("Missing optional field: 'modbus_config'");
  }

  return result;
}

ValidationResult DeviceConfigurator::validateDevicesJson(
    const cJSON* json, const std::string& filepath) {
  ValidationResult result;

  if (!JsonUtils::hasKey(json, "devices")) {
    result.addError("Missing required field: 'devices'");
  } else {
    const cJSON* devices = cJSON_GetObjectItem(json, "devices");
    if (!cJSON_IsArray(devices)) {
      result.addError("Field 'devices' must be an array");
    } else if (cJSON_GetArraySize(devices) == 0) {
      result.addWarning("Devices array is empty");
    }
  }

  return result;
}

ValidationResult DeviceConfigurator::validateDeviceModel(
    const DeviceModel& model) {
  ValidationResult result;

  if (model.model_name.empty()) {
    result.addError("Model name is empty");
  }

  if (model.registers.empty()) {
    result.addError("Model has no registers defined");
  }

  if (model.start_address + model.quantity > 65535) {
    result.addError("Modbus address range exceeds maximum (65535)");
  }

  if (model.quantity == 0 && !model.registers.empty()) {
    result.addWarning("Quantity is 0 but registers are defined");
  }

  // Check for duplicate register addresses
  std::unordered_map<int, std::string> address_map;
  for (const auto& reg : model.registers) {
    if (address_map.count(reg.address)) {
      result.addWarning("Duplicate register address found");
    } else {
      address_map[reg.address] = reg.name;
    }
  }

  return result;
}

// ============================================================================
// PARSING HELPERS
// ============================================================================
std::string DeviceConfigurator::readFile(const std::string& filepath) {
  std::ifstream file(filepath.c_str(), std::ios::binary);
  if (!file.is_open()) {
    throw std::runtime_error("Cannot open file: " + filepath);
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

RegType DeviceConfigurator::parseRegType(const std::string& type_str) {
  if (type_str == "u32") return RegType::U32;
  if (type_str == "i32") return RegType::I32;
  if (type_str == "u16") return RegType::U16;
  if (type_str == "i16") return RegType::I16;
  if (type_str == "float") return RegType::FLOAT;

  Logger::log(LogLevel::WARN,
              "Unknown register type: " + type_str + ", defaulting to U16");
  return RegType::U16;
}

ByteOrder DeviceConfigurator::parseByteOrder(const std::string& order_str) {
  if (order_str == "little_endian") return ByteOrder::LittleEndian;
  if (order_str == "big_endian_swap") return ByteOrder::BigEndianSwap;
  if (order_str == "big_endian") return ByteOrder::BigEndian;

  Logger::log(LogLevel::WARN,
              "Unknown byte order: " + order_str + ", defaulting to BigEndian");
  return ByteOrder::BigEndian;
}

float DeviceConfigurator::inferGain(const std::string& name) {
  // Case-insensitive search
  std::string lower_name = name;
  std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                 ::tolower);

  if (lower_name.find("voltage") != std::string::npos) return 0.1f;
  if (lower_name.find("current") != std::string::npos) return 0.01f;
  if (lower_name.find("power") != std::string::npos) return 1.0f;
  if (lower_name.find("energy") != std::string::npos) return 1.0f;
  if (lower_name.find("frequency") != std::string::npos) return 0.01f;
  if (lower_name.find("temperature") != std::string::npos) return 0.1f;

  return 1.0f;  // Default gain
}