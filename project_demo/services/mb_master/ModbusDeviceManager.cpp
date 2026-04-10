#include "ModbusDeviceManager.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <sstream>

// ============================================================================
// CONSTRUCTOR & DESTRUCTOR
// ============================================================================

ModbusDeviceManager::ModbusDeviceManager()
    : response_callback_(nullptr), stats_() {}

ModbusDeviceManager::~ModbusDeviceManager() {
  // Cleanup sẽ tự động thông qua RAII
  // Mutex và condition_variable sẽ tự cleanup
}

// ============================================================================
// REGISTRY API
// ============================================================================

void ModbusDeviceManager::addModel(const std::string& name,
                                   std::shared_ptr<DeviceModel> model) {
  if (!model || name.empty()) {
    return;
  }

  std::lock_guard<std::mutex> lock(models_mutex_);
  device_models_[name] = model;

  // Update statistics
  {
    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
    stats_.total_models = device_models_.size();
  }
}

void ModbusDeviceManager::addDevice(std::shared_ptr<Device> device) {
  if (!device || device->id.empty()) {
    return;
  }

  std::lock_guard<std::mutex> lock(devices_mutex_);
  device_map_[device->id] = device;

  // Update statistics
  {
    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
    stats_.total_devices = device_map_.size();
  }
}

std::shared_ptr<DeviceModel> ModbusDeviceManager::findMatchingModel(
    const std::string& device_id) {
  std::lock_guard<std::mutex> lock(models_mutex_);

  // Strategy 1: Exact match
  std::shared_ptr<DeviceModel> exact = findExactMatch(device_id);
  if (exact) {
    return exact;
  }

  // Strategy 2: Suffix match
  std::shared_ptr<DeviceModel> suffix = findSuffixMatch(device_id);
  if (suffix) {
    return suffix;
  }

  // Strategy 3: Fallback
  return findFallbackMatch();
}

void ModbusDeviceManager::clear() {
  // Clear devices
  {
    std::lock_guard<std::mutex> lock(devices_mutex_);
    device_map_.clear();
  }

  // Clear models
  {
    std::lock_guard<std::mutex> lock(models_mutex_);
    device_models_.clear();
  }

  // Clear command queue
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    std::queue<ControlCommand> empty;
    std::swap(command_queue_, empty);
  }

  // Reset statistics
  resetStatistics();
}

// ============================================================================
// RUNTIME API
// ============================================================================

std::vector<std::shared_ptr<Device>> ModbusDeviceManager::getDevices() const {
  std::lock_guard<std::mutex> lock(devices_mutex_);

  std::vector<std::shared_ptr<Device>> devices;
  devices.reserve(device_map_.size());

  for (std::unordered_map<std::string, std::shared_ptr<Device>>::const_iterator
           it = device_map_.begin();
       it != device_map_.end(); ++it) {
    devices.push_back(it->second);
  }

  return devices;
}

std::shared_ptr<Device> ModbusDeviceManager::getDevice(
    const std::string& device_id) const {
  std::lock_guard<std::mutex> lock(devices_mutex_);

  std::unordered_map<std::string, std::shared_ptr<Device>>::const_iterator it =
      device_map_.find(device_id);

  if (it != device_map_.end()) {
    return it->second;
  }

  return std::shared_ptr<Device>();  // Return empty shared_ptr (nullptr)
}

bool ModbusDeviceManager::pollDevice(const std::string& device_id,
                                     const uint16_t* raw_buffer) {
  if (!raw_buffer) {
    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
    stats_.errors_count++;
    return false;
  }

  std::shared_ptr<Device> device = getDevice(device_id);
  if (!device) {
    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
    stats_.errors_count++;
    return false;
  }

  // Lock device data để update
  std::lock_guard<std::mutex> lock(device->data_mutex);
  device->processData(raw_buffer);

  // Update active devices count
  {
    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
    // Đơn giản hóa: tạm thời set = total (trong thực tế nên track riêng)
    stats_.active_devices = stats_.total_devices;
  }

  return true;
}

size_t ModbusDeviceManager::getDeviceCount() const {
  std::lock_guard<std::mutex> lock(devices_mutex_);
  return device_map_.size();
}

size_t ModbusDeviceManager::getModelCount() const {
  std::lock_guard<std::mutex> lock(models_mutex_);
  return device_models_.size();
}

bool ModbusDeviceManager::hasDevice(const std::string& device_id) const {
  std::lock_guard<std::mutex> lock(devices_mutex_);
  return device_map_.find(device_id) != device_map_.end();
}

bool ModbusDeviceManager::hasModel(const std::string& model_name) const {
  std::lock_guard<std::mutex> lock(models_mutex_);
  return device_models_.find(model_name) != device_models_.end();
}

// ============================================================================
// CONTROL COMMAND QUEUE
// ============================================================================

void ModbusDeviceManager::enqueueCommand(const ControlCommand& cmd) {
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    command_queue_.push(cmd);
  }

  // Update statistics
  {
    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
    stats_.commands_pending = command_queue_.size();
  }

  queue_cv_.notify_one();
}

bool ModbusDeviceManager::processNextCommand(int timeout_ms) {
  std::unique_lock<std::mutex> lock(queue_mutex_);

  if (timeout_ms > 0) {
    std::chrono::milliseconds timeout(timeout_ms);
    if (!queue_cv_.wait_for(lock, timeout,
                            [this]() { return !command_queue_.empty(); })) {
      return false;  // Timeout
    }
  } else if (command_queue_.empty()) {
    return false;  // No wait, queue empty
  }

  ControlCommand cmd = command_queue_.front();
  command_queue_.pop();
  lock.unlock();

  // Execute command
  executeCommand(cmd);

  // Update statistics
  {
    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
    stats_.commands_processed++;
    stats_.commands_pending = command_queue_.size();
  }

  return true;
}

size_t ModbusDeviceManager::getCommandQueueSize() const {
  std::lock_guard<std::mutex> lock(queue_mutex_);
  return command_queue_.size();
}

void ModbusDeviceManager::clearCommandQueue() {
  std::lock_guard<std::mutex> lock(queue_mutex_);
  std::queue<ControlCommand> empty;
  std::swap(command_queue_, empty);

  // Update statistics
  {
    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
    stats_.commands_pending = 0;
  }
}

void ModbusDeviceManager::setResponseCallback(ResponseCallback callback) {
  std::lock_guard<std::mutex> lock(callback_mutex_);
  response_callback_ = callback;
}

// ============================================================================
// DATA RETRIEVAL
// ============================================================================

bool ModbusDeviceManager::getMeterData(const std::string& device_id,
                                       MeterData& data) const {
  std::shared_ptr<Device> device = getDevice(device_id);
  if (!device) {
    return false;
  }

  std::lock_guard<std::mutex> lock(device->data_mutex);

  // Dynamic cast to check type
  MeterDevice* meter = dynamic_cast<MeterDevice*>(device.get());
  if (!meter) {
    return false;
  }

  data = meter->storage;
  return true;
}

bool ModbusDeviceManager::getInverterData(const std::string& device_id,
                                          InverterData& data) const {
  std::shared_ptr<Device> device = getDevice(device_id);
  if (!device) {
    return false;
  }

  std::lock_guard<std::mutex> lock(device->data_mutex);

  // Dynamic cast to check type
  InverterDevice* inverter = dynamic_cast<InverterDevice*>(device.get());
  if (!inverter) {
    return false;
  }

  data = inverter->storage;
  return true;
}

bool ModbusDeviceManager::setRegisterValue(const std::string& device_id,
                                           const std::string& register_name,
                                           float value) {
  std::shared_ptr<Device> device = getDevice(device_id);
  if (!device || !device->model_ptr) {
    return false;
  }

  std::lock_guard<std::mutex> lock(device->data_mutex);

  // Tìm register và check writable
  const std::vector<RegisterMapping>& registers = device->model_ptr->registers;
  for (size_t i = 0; i < registers.size(); i++) {
    if (registers[i].name == register_name) {
      if (!registers[i].writable) {
        return false;  // Register không writable
      }

      // Tạo command để write
      ControlCommand cmd;
      cmd.device_id = device_id;
      cmd.register_address = registers[i].address;
      cmd.value = static_cast<uint16_t>(value / registers[i].gain);

      enqueueCommand(cmd);
      return true;
    }
  }

  return false;
}

// ============================================================================
// STATISTICS & MONITORING
// ============================================================================

ModbusDeviceManager::Statistics ModbusDeviceManager::getStatistics() const {
  std::lock_guard<std::mutex> lock(stats_mutex_);
  return stats_;  // Return copy
}

void ModbusDeviceManager::resetStatistics() {
  std::lock_guard<std::mutex> lock(stats_mutex_);
  stats_.commands_processed = 0;
  stats_.errors_count = 0;
  // Keep total_devices, total_models, commands_pending
}

void ModbusDeviceManager::printDebugInfo() const {
  std::cout << "\n========================================" << std::endl;
  std::cout << "  ModbusDeviceManager Debug Info" << std::endl;
  std::cout << "========================================" << std::endl;

  Statistics stats = getStatistics();
  std::cout << "Total Devices:       " << stats.total_devices << std::endl;
  std::cout << "Total Models:        " << stats.total_models << std::endl;
  std::cout << "Active Devices:      " << stats.active_devices << std::endl;
  std::cout << "Commands Processed:  " << stats.commands_processed << std::endl;
  std::cout << "Commands Pending:    " << stats.commands_pending << std::endl;
  std::cout << "Errors Count:        " << stats.errors_count << std::endl;

  std::cout << "\n--- Registered Devices ---" << std::endl;
  std::vector<std::shared_ptr<Device>> devices = getDevices();
  for (size_t i = 0; i < devices.size(); i++) {
    std::cout << "  [" << i << "] ID: " << devices[i]->id
              << ", Name: " << devices[i]->name
              << ", Slave: " << static_cast<int>(devices[i]->slave_id)
              << std::endl;
  }

  std::cout << "\n--- Registered Models ---" << std::endl;
  {
    std::lock_guard<std::mutex> lock(models_mutex_);
    int idx = 0;
    for (std::map<std::string, std::shared_ptr<DeviceModel>>::const_iterator
             it = device_models_.begin();
         it != device_models_.end(); ++it) {
      std::cout << "  [" << idx++ << "] " << it->first << " ("
                << it->second->registers.size() << " registers)" << std::endl;
    }
  }

  std::cout << "========================================\n" << std::endl;
}

std::shared_ptr<DeviceModel> ModbusDeviceManager::findExactMatch(
    const std::string& device_id) {
  std::map<std::string, std::shared_ptr<DeviceModel>>::iterator it =
      device_models_.find(device_id);

  if (it != device_models_.end()) {
    return it->second;
  }

  return std::shared_ptr<DeviceModel>();  // nullptr
}

std::shared_ptr<DeviceModel> ModbusDeviceManager::findSuffixMatch(
    const std::string& device_id) {
  const char* suffixes[] = {"_mapping", "_model", "_config", "_device"};
  const int num_suffixes = 4;

  std::string id_base = device_id;

  for (int i = 0; i < num_suffixes; i++) {
    size_t pos = id_base.find(suffixes[i]);
    if (pos != std::string::npos) {
      id_base = id_base.substr(0, pos);

      std::map<std::string, std::shared_ptr<DeviceModel>>::iterator it =
          device_models_.find(id_base);
      if (it != device_models_.end()) {
        return it->second;
      }
    }
  }

  return std::shared_ptr<DeviceModel>();  // nullptr
}

std::shared_ptr<DeviceModel> ModbusDeviceManager::findFallbackMatch() {
  if (device_models_.empty()) {
    return std::shared_ptr<DeviceModel>();  // nullptr
  }

  return device_models_.begin()->second;
}

void ModbusDeviceManager::executeCommand(const ControlCommand& cmd) {
  // Implementation phụ thuộc vào cấu trúc ControlCommand
  // Đây là skeleton implementation

  std::ostringstream response;
  response << "Command executed - Device: " << cmd.device_id
           << ", Address: " << cmd.register_address << ", Value: " << cmd.value;

  sendResponse(response.str());
}

void ModbusDeviceManager::sendResponse(const std::string& response) {
  std::lock_guard<std::mutex> lock(callback_mutex_);
  if (response_callback_) {
    response_callback_(response);
  }
}
