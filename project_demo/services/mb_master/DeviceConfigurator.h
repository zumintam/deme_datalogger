#ifndef DEVICE_CONFIGURATOR_H
#define DEVICE_CONFIGURATOR_H

#include <condition_variable>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

// For directory operations (C++14 compatible)
#include <dirent.h>
#include <sys/stat.h>

#include "ModbusDeviceManager.h"
#include "cJSON.h"
#include "device_manager/device.h"
#include "device_manager/mapping.h"

struct ConfigError {
  enum class Type {
    None = 0,  // Thành công
    FileNotFound,
    ParseError,
    ValidationError,
    MissingRequiredField,
    InvalidFormat
  };

  Type type;
  std::string message;
  std::string context;

  ConfigError() : type(Type::None), message(""), context("") {}

  ConfigError(Type t, const std::string& msg, const std::string& ctx = "")
      : type(t), message(msg), context(ctx) {}

  bool hasError() const { return type != Type::None; }

  std::string toString() const {
    if (!hasError()) return "Success";

    const char* type_str[] = {
        "Success",         "FileNotFound",         "ParseError",
        "ValidationError", "MissingRequiredField", "InvalidFormat"};
    return std::string("[") + type_str[static_cast<int>(type)] + "] " +
           message + (context.empty() ? "" : " (Context: " + context + ")");
  }

  // Static factory methods
  static ConfigError success() { return ConfigError(); }

  static ConfigError fileNotFound(const std::string& path) {
    return ConfigError(Type::FileNotFound, "File not found", path);
  }

  static ConfigError parseError(const std::string& msg,
                                const std::string& file) {
    return ConfigError(Type::ParseError, msg, file);
  }

  static ConfigError validationError(const std::string& msg,
                                     const std::string& ctx) {
    return ConfigError(Type::ValidationError, msg, ctx);
  }
};

// ============================================================================
// VALIDATION
// ============================================================================
struct ValidationResult {
  bool valid;
  std::vector<std::string> errors;
  std::vector<std::string> warnings;

  ValidationResult() : valid(true) {}

  void addError(const std::string& msg) {
    errors.push_back(msg);
    valid = false;
  }

  void addWarning(const std::string& msg) { warnings.push_back(msg); }

  bool hasIssues() const { return !errors.empty() || !warnings.empty(); }
};

// ============================================================================
// LOGGING
// ============================================================================
enum class LogLevel { DEBUG = 0, INFO, WARN, ERROR, FATAL };

class Logger {
 public:
  static void setLevel(LogLevel level) { min_level_ = level; }

  static void log(LogLevel level, const std::string& msg,
                  const std::string& context = "") {
    if (level < min_level_) return;

    std::lock_guard<std::mutex> lock(log_mutex_);
    std::ostream& out = (level >= LogLevel::ERROR) ? std::cerr : std::cout;

    const char* level_str[] = {"DEBUG", "INFO", "WARN", "ERROR", "FATAL"};
    out << "[" << level_str[static_cast<int>(level)] << "] " << msg;
    if (!context.empty()) out << " (" << context << ")";
    out << std::endl;
  }

 private:
  static LogLevel min_level_;
  static std::mutex log_mutex_;
};

// ============================================================================
// DEVICE CONFIGURATOR
// ============================================================================
class DeviceConfigurator {
 public:
  struct Config {
    bool validate_schemas;
    bool skip_invalid_files;
    bool allow_duplicate_models;
    LogLevel log_level;

    Config()
        : validate_schemas(true),
          skip_invalid_files(true),
          allow_duplicate_models(false),
          log_level(LogLevel::INFO) {}
  };

  DeviceConfigurator() {}
  ~DeviceConfigurator() {}

  /**
   * @brief Configure the entire device system
   * @return ConfigError (check hasError() to see if successful)
   */
  static ConfigError configure(ModbusDeviceManager& manager,
                               const std::string& devices_file,
                               const std::string& mappings_folder,
                               const Config& config = Config());

  /**
   * @brief Validate a device model for correctness
   */
  static ValidationResult validateDeviceModel(const DeviceModel& model);

 private:
  // Core loading functions
  static ConfigError loadAllModels(ModbusDeviceManager& manager,
                                   const std::string& folder,
                                   const Config& config);

  static ConfigError loadMappingFile(ModbusDeviceManager& manager,
                                     const std::string& filepath,
                                     const Config& config);

  static ConfigError instantiateDevices(ModbusDeviceManager& manager,
                                        const std::string& devices_file,
                                        const Config& config);

  // Validation functions
  static ValidationResult validateMappingJson(const cJSON* json,
                                              const std::string& filepath);
  static ValidationResult validateDevicesJson(const cJSON* json,
                                              const std::string& filepath);

  // Parsing helpers
  static std::string readFile(const std::string& filepath);
  static RegType parseRegType(const std::string& type_str);
  static ByteOrder parseByteOrder(const std::string& order_str);
  static float inferGain(const std::string& register_name);

  // File system helpers (C++14 compatible)
  static bool fileExists(const std::string& path);
  static bool isDirectory(const std::string& path);
  static std::vector<std::string> listJsonFiles(const std::string& folder);

  // Metrics
  struct LoadMetrics {
    size_t models_loaded;
    size_t devices_loaded;
    size_t files_processed;
    size_t errors_encountered;

    LoadMetrics()
        : models_loaded(0),
          devices_loaded(0),
          files_processed(0),
          errors_encountered(0) {}
  };
  static LoadMetrics metrics_;
};

#endif  // DEVICE_CONFIGURATOR_H