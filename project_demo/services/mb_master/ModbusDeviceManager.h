#ifndef MODBUS_DEVICE_MANAGER_H
#define MODBUS_DEVICE_MANAGER_H

#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

#include "device_manager/device.h"
#include "device_manager/mapping.h"

// Forward declarations
struct ControlCommand;
struct MeterData;
struct InverterData;

/**
 * @brief Thread-safe manager for Modbus devices and their models
 *
 * Quản lý runtime state của tất cả Modbus devices và cung cấp
 * thread-safe access đến device data và control operations.
 */
class ModbusDeviceManager {
 public:
  // Callback function type cho responses
  typedef void (*ResponseCallback)(const std::string&);

  ModbusDeviceManager();
  ~ModbusDeviceManager();

  ModbusDeviceManager(const ModbusDeviceManager&) = delete;
  ModbusDeviceManager& operator=(const ModbusDeviceManager&) = delete;

  // ========================================================================
  // REGISTRY API - Dành cho DeviceConfigurator nạp dữ liệu
  // ========================================================================

  /**
   * @brief Thêm device model vào registry
   * @param name Model identifier
   * @param model Shared pointer to the model
   * @thread_safety Thread-safe (có mutex protection)
   */
  void addModel(const std::string& name, std::shared_ptr<DeviceModel> model);

  /**
   * @brief Thêm device instance vào registry
   * @param device Shared pointer to the device
   * @thread_safety Thread-safe (có mutex protection)
   */
  void addDevice(std::shared_ptr<Device> device);

  void removeDevice(const std::string& name) {
    std::lock_guard<std::mutex> lock(devices_mutex_);
    // Remove device if it exists
    auto it = device_map_.find(name);
    if (it != device_map_.end()) {
      device_map_.erase(it);
    }

    // update statistics
    {
      std::lock_guard<std::mutex> stats_lock(stats_mutex_);
      stats_.total_devices = device_map_.size();
    }
  };
  /**
   * @brief Tìm matching model cho device ID
   * @param device_id Device identifier
   * @return Shared pointer to model, hoặc nullptr nếu không tìm thấy
   * @thread_safety Thread-safe (read lock)
   */
  std::shared_ptr<DeviceModel> findMatchingModel(const std::string& device_id);

  /**
   * @brief Xóa tất cả models và devices
   * @thread_safety Thread-safe
   */
  void clear();

  // ========================================================================
  // RUNTIME API - Dành cho operation threads
  // ========================================================================

  /**
   * @brief Lấy danh sách tất cả devices (thread-safe copy)
   * @return Vector chứa shared pointers đến devices
   * @thread_safety Thread-safe (trả về copy)
   */
  std::vector<std::shared_ptr<Device>> getDevices() const;

  /**
   * @brief Lấy một device cụ thể theo ID
   * @param device_id Device identifier
   * @return Shared pointer to device, hoặc nullptr nếu không tìm thấy
   * @thread_safety Thread-safe
   */
  std::shared_ptr<Device> getDevice(const std::string& device_id) const;

  /**
   * @brief Poll và update device với raw Modbus data
   * @param device_id Device identifier
   * @param raw_buffer Buffer chứa raw register values
   * @return true nếu thành công, false nếu không tìm thấy device
   * @thread_safety Thread-safe
   */
  bool pollDevice(const std::string& device_id, const uint16_t* raw_buffer);

  /**
   * @brief Lấy số lượng devices đã đăng ký
   * @thread_safety Thread-safe
   */
  size_t getDeviceCount() const;

  /**
   * @brief Lấy số lượng models đã đăng ký
   * @thread_safety Thread-safe
   */
  size_t getModelCount() const;

  /**
   * @brief Kiểm tra device có tồn tại không
   * @param device_id Device identifier
   * @return true nếu device tồn tại
   * @thread_safety Thread-safe
   */
  bool hasDevice(const std::string& device_id) const;

  /**
   * @brief Kiểm tra model có tồn tại không
   * @param model_name Model name
   * @return true nếu model tồn tại
   * @thread_safety Thread-safe
   */
  bool hasModel(const std::string& model_name) const;

  // ========================================================================
  // CONTROL COMMAND QUEUE
  // ========================================================================

  /**
   * @brief Đưa control command vào queue để xử lý
   * @param cmd Control command cần thực thi
   * @thread_safety Thread-safe
   */
  void enqueueCommand(const ControlCommand& cmd);

  /**
   * @brief Dequeue và xử lý command tiếp theo
   * @param timeout_ms Thời gian chờ tối đa (ms), 0 = không chờ
   * @return true nếu đã xử lý command, false nếu queue rỗng
   * @thread_safety Thread-safe
   */
  bool processNextCommand(int timeout_ms = 0);

  /**
   * @brief Lấy số lượng commands đang chờ
   * @thread_safety Thread-safe
   */
  size_t getCommandQueueSize() const;

  /**
   * @brief Xóa tất cả commands trong queue
   * @thread_safety Thread-safe
   */
  void clearCommandQueue();

  /**
   * @brief Set callback function cho command responses
   * @param callback Function pointer hoặc nullptr để disable
   * @thread_safety Thread-safe
   */
  void setResponseCallback(ResponseCallback callback);

  // ========================================================================
  // DATA RETRIEVAL - Type-specific convenience methods
  // ========================================================================

  /**
   * @brief Lấy meter-specific data
   * @param device_id Device identifier
   * @param[out] data Output meter data structure
   * @return true nếu thành công, false nếu device không phải meter
   * @thread_safety Thread-safe
   */
  bool getMeterData(const std::string& device_id, MeterData& data) const;

  /**
   * @brief Lấy inverter-specific data
   * @param device_id Device identifier
   * @param[out] data Output inverter data structure
   * @return true nếu thành công, false nếu device không phải inverter
   * @thread_safety Thread-safe
   */
  bool getInverterData(const std::string& device_id, InverterData& data) const;

  /**
   * @brief Lấy generic register value từ device
   * @param device_id Device identifier
   * @param register_name Tên register
   * @param[out] value Output value
   * @return true nếu thành công
   * @thread_safety Thread-safe
   */
  bool getRegisterValue(const std::string& device_id,
                        const std::string& register_name, float& value) const;

  /**
   * @brief Set register value (nếu writable)
   * @param device_id Device identifier
   * @param register_name Tên register
   * @param value Giá trị cần set
   * @return true nếu thành công
   * @thread_safety Thread-safe
   */
  bool setRegisterValue(const std::string& device_id,
                        const std::string& register_name, float value);

  // ========================================================================
  // STATISTICS & MONITORING
  // ========================================================================

  /**
   * @brief Thống kê hệ thống
   */
  struct Statistics {
    size_t total_devices;
    size_t total_models;
    size_t active_devices;
    size_t commands_processed;
    size_t commands_pending;
    size_t errors_count;

    Statistics()
        : total_devices(0),
          total_models(0),
          active_devices(0),
          commands_processed(0),
          commands_pending(0),
          errors_count(0) {}
  };

  /**
   * @brief Lấy statistics của hệ thống
   * @thread_safety Thread-safe
   */
  Statistics getStatistics() const;

  /**
   * @brief Reset statistics counters
   * @thread_safety Thread-safe
   */
  void resetStatistics();

  /**
   * @brief In thông tin debug ra console
   */
  void printDebugInfo() const;

 private:
  // ========================================================================
  // PRIVATE HELPER METHODS
  // ========================================================================

  // Model matching strategies
  std::shared_ptr<DeviceModel> findExactMatch(const std::string& device_id);
  std::shared_ptr<DeviceModel> findSuffixMatch(const std::string& device_id);
  std::shared_ptr<DeviceModel> findFallbackMatch();

  // Command processing
  void executeCommand(const ControlCommand& cmd);
  void sendResponse(const std::string& response);

  // ========================================================================
  // PRIVATE DATA MEMBERS
  // ========================================================================

  // Device và model storage
  std::unordered_map<std::string, std::shared_ptr<Device>> device_map_;
  std::map<std::string, std::shared_ptr<DeviceModel>> device_models_;

  // Thread synchronization
  mutable std::mutex devices_mutex_;
  mutable std::mutex models_mutex_;

  // Command queue
  std::queue<ControlCommand> command_queue_;
  mutable std::mutex queue_mutex_;
  std::condition_variable queue_cv_;

  // Callback và statistics
  ResponseCallback response_callback_;
  mutable std::mutex callback_mutex_;

  Statistics stats_;
  mutable std::mutex stats_mutex_;
};

#endif  // MODBUS_DEVICE_MANAGER_H