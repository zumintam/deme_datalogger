// ============================================================================
// FILE: modbus_multi_device.h
// Multi-Device Modbus Configuration with Threading Support
// ============================================================================

#ifndef MODBUS_MULTI_DEVICE_H
#define MODBUS_MULTI_DEVICE_H

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "modbus_ex.h"

// Device types enumeration
enum class DeviceType { METER, INVERTER };

// Structure for individual device configuration
struct DeviceConfig {
  std::string name;
  std::string model;
  int slave_id;
  DeviceType type;
  std::string config_file;

  // Modbus context and config
  ModbusContextPtr ctx;
  Config config;
  ModbusMaster mb;

  // Status
  std::atomic<bool> is_running{false};
  std::atomic<int> consecutive_errors{0};
  std::atomic<uint64_t> read_count{0};

  DeviceConfig(const std::string& n, const std::string& m, int sid,
               DeviceType t, const std::string& cfg)
      : name(n), model(m), slave_id(sid), type(t), config_file(cfg) {}
};

// Thread-safe data collector
struct DataCollector {
  std::mutex mtx;
  std::vector<std::string> json_buffer;

  void addData(const std::string& data) {
    std::lock_guard<std::mutex> lock(mtx);
    json_buffer.push_back(data);
  }

  std::vector<std::string> getAllData() {
    std::lock_guard<std::mutex> lock(mtx);
    std::vector<std::string> data = json_buffer;
    json_buffer.clear();
    return data;
  }
};

// Multi-device manager
class MultiDeviceManager {
 private:
  std::vector<std::shared_ptr<DeviceConfig>> devices;
  std::vector<std::thread> threads;
  DataCollector collector;
  ZmqDealer zmq_dealer;
  std::atomic<bool> should_stop{false};

  const int MAX_CONSECUTIVE_ERRORS = 5;

 public:
  MultiDeviceManager() = default;
  ~MultiDeviceManager() { stopAll(); }

  // Add device to manager
  bool addDevice(const std::string& name, const std::string& model,
                 int slave_id, DeviceType type,
                 const std::string& config_file) {
    auto device = std::make_shared<DeviceConfig>(name, model, slave_id, type,
                                                 config_file);
    devices.push_back(device);
    return true;
  }

  // Initialize all devices
  bool initializeAll(const std::string& modbus_port) {
    std::cout << "Initializing " << devices.size() << " devices..."
              << std::endl;

    for (auto& device : devices) {
      std::cout << "  Initializing " << device->name
                << " (Slave ID: " << device->slave_id << ")..." << std::endl;

      if (!initModbusConfig(device->config, modbus_port.c_str(), device->ctx,
                            device->mb, device->config_file.c_str())) {
        std::cerr << "    ✗ Failed to initialize " << device->name << std::endl;
        return false;
      }

      std::cout << "    ✓ " << device->name << " initialized" << std::endl;
    }

    // Initialize ZMQ
    if (!initZmqDealer(zmq_dealer.ctx, zmq_dealer.dealer, ZMQ_ENDPOINT)) {
      std::cerr << "Failed to initialize ZMQ dealer" << std::endl;
      return false;
    }

    std::cout << "All devices initialized successfully" << std::endl;
    return true;
  }

  // Thread function for reading device data
  void deviceReadThread(std::shared_ptr<DeviceConfig> device) {
    uint16_t raw_data[256] = {0};
    device->is_running = true;

    std::cout << "Thread started for " << device->name << std::endl;

    while (!should_stop &&
           device->consecutive_errors < MAX_CONSECUTIVE_ERRORS) {
      // Clear buffer
      memset(raw_data, 0, sizeof(raw_data));

      // Read Modbus data
      bool read_success = readModbusData(
          device->ctx.get(), device->config.meter.modbus, raw_data, device->mb);

      if (!read_success) {
        std::cerr << "[" << device->name << "] Read failed" << std::endl;
        device->consecutive_errors++;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        continue;
      }

      // Reset error counter
      device->consecutive_errors = 0;
      device->read_count++;

      // Parse to JSON
      std::string json_data = device->config.meter.parse2Json(raw_data);

      // Create envelope with device info
      std::string envelope =
          createDeviceEnvelope(device->name, device->slave_id, json_data);

      // Add to collector
      collector.addData(envelope);

      std::cout << "[" << device->name << "] Read #" << device->read_count
                << " - OK" << std::endl;

      // Sleep based on device type (meters might need faster polling)
      if (device->type == DeviceType::METER) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
      } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
      }
    }

    device->is_running = false;
    std::cout << "Thread stopped for " << device->name << std::endl;
  }

  // Start all device threads
  void startAll() {
    std::cout << "\n=== Starting all device threads ===" << std::endl;
    should_stop = false;

    for (auto& device : devices) {
      threads.emplace_back(&MultiDeviceManager::deviceReadThread, this, device);
    }

    std::cout << "All threads started" << std::endl;
  }

  // Stop all threads
  void stopAll() {
    std::cout << "\n=== Stopping all threads ===" << std::endl;
    should_stop = true;

    for (auto& thread : threads) {
      if (thread.joinable()) {
        thread.join();
      }
    }

    threads.clear();
    std::cout << "All threads stopped" << std::endl;
  }

  // Send collected data via ZMQ
  void sendCollectedData() {
    auto data_list = collector.getAllData();

    if (data_list.empty()) {
      return;
    }

    // Combine all device data into single JSON
    std::string combined_json = combineDeviceData(data_list);

    // Send via ZMQ
    bool success = sendZmqMessage(zmq_dealer.dealer, combined_json);

    if (success) {
      std::cout << "Sent data from " << data_list.size() << " devices via ZMQ"
                << std::endl;
    } else {
      std::cerr << "Failed to send ZMQ message" << std::endl;
    }
  }

  // Print status of all devices
  void printStatus() {
    std::cout << "\n=== Device Status ===" << std::endl;
    for (const auto& device : devices) {
      std::cout << device->name << " (ID:" << device->slave_id << ")"
                << " - Running: " << (device->is_running ? "Yes" : "No")
                << " - Reads: " << device->read_count
                << " - Errors: " << device->consecutive_errors << std::endl;
    }
  }

  // Cleanup
  void cleanup() {
    stopAll();

    for (auto& device : devices) {
      cleanupModbus(device->ctx);
    }

    cleanupZmq(zmq_dealer.ctx, zmq_dealer.dealer);
  }

 private:
  // Helper: Create device envelope JSON
  std::string createDeviceEnvelope(const std::string& device_name, int slave_id,
                                   const std::string& data) {
    std::ostringstream oss;
    oss << "{"
        << "\"device_name\":\"" << device_name << "\","
        << "\"slave_id\":" << slave_id << ","
        << "\"timestamp\":\"" << getCurrentTimestamp() << "\","
        << "\"data\":" << data << "}";
    return oss.str();
  }

  // Helper: Combine multiple device data
  std::string combineDeviceData(const std::vector<std::string>& data_list) {
    std::ostringstream oss;
    oss << "{"
        << "\"timestamp\":\"" << getCurrentTimestamp() << "\","
        << "\"device_count\":" << data_list.size() << ","
        << "\"devices\":[";

    for (size_t i = 0; i < data_list.size(); ++i) {
      oss << data_list[i];
      if (i < data_list.size() - 1) {
        oss << ",";
      }
    }

    oss << "]}";
    return oss.str();
  }

  // Helper: Get current timestamp
  std::string getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&time));
    return std::string(buf);
  }
};

#endif  // MODBUS_MULTI_DEVICE_H

// ============================================================================
// FILE: main_multi_device.cpp
// Main application using multi-device manager
// ============================================================================

#include "modbus_multi_device.h"

int main() {
  MultiDeviceManager manager;

  try {
    std::cout << "=== Multi-Device Modbus Reader ===" << std::endl;

    // ===== ADD METERS =====
    manager.addDevice("Meter_Main_Grid", "DPM380", 1, DeviceType::METER,
                      "DPM380_meter1.json");
    manager.addDevice("Meter_Building_A", "DPM380", 2, DeviceType::METER,
                      "DPM380_meter2.json");
    manager.addDevice("Meter_Building_B", "DPM380", 3, DeviceType::METER,
                      "DPM380_meter3.json");

    // ===== ADD INVERTERS =====
    manager.addDevice("Inverter_Zone_1", "GROWATT", 10, DeviceType::INVERTER,
                      "GROWATT_inv1.json");
    manager.addDevice("Inverter_Zone_2", "GROWATT", 11, DeviceType::INVERTER,
                      "GROWATT_inv2.json");
    manager.addDevice("Inverter_Zone_3", "GROWATT", 12, DeviceType::INVERTER,
                      "GROWATT_inv3.json");
    manager.addDevice("Inverter_Zone_4", "GROWATT", 13, DeviceType::INVERTER,
                      "GROWATT_inv4.json");
    manager.addDevice("Inverter_Zone_5", "GROWATT", 14, DeviceType::INVERTER,
                      "GROWATT_inv5.json");
    manager.addDevice("Inverter_Zone_6", "GROWATT", 15, DeviceType::INVERTER,
                      "GROWATT_inv6.json");
    manager.addDevice("Inverter_Zone_7", "GROWATT", 16, DeviceType::INVERTER,
                      "GROWATT_inv7.json");
    manager.addDevice("Inverter_Zone_8", "GROWATT", 17, DeviceType::INVERTER,
                      "GROWATT_inv8.json");
    manager.addDevice("Inverter_Zone_9", "GROWATT", 18, DeviceType::INVERTER,
                      "GROWATT_inv9.json");
    manager.addDevice("Inverter_Zone_10", "GROWATT", 19, DeviceType::INVERTER,
                      "GROWATT_inv10.json");

    // Initialize all devices
    if (!manager.initializeAll(MODBUS_PORT_S3)) {
      std::cerr << "Failed to initialize devices" << std::endl;
      return EXIT_FAILURE;
    }

    // Start all threads
    manager.startAll();

    // Main loop: collect and send data periodically
    int cycle = 0;
    const int MAX_CYCLES = 100;  // Run for 100 cycles (~100 seconds)

    while (cycle < MAX_CYCLES) {
      std::this_thread::sleep_for(std::chrono::seconds(5));

      // Send collected data
      manager.sendCollectedData();

      // Print status every 10 cycles
      if (cycle % 10 == 0) {
        manager.printStatus();
      }

      cycle++;
    }

    std::cout << "\nReached maximum cycles, shutting down..." << std::endl;

  } catch (const std::exception& e) {
    std::cerr << "Exception: " << e.what() << std::endl;
    manager.cleanup();
    return EXIT_FAILURE;
  }

  // Cleanup
  manager.cleanup();

  std::cout << "Program terminated successfully" << std::endl;
  return EXIT_SUCCESS;
}