/**
 * @file real_modbus_polling.cpp
 * @brief Modbus RTU polling with auto-discovery and device removal
 * @version 2.0
 * @date 2026-02-04
 *
 * Features:
 * - Auto-discovery: Probe devices to find which port they're on
 * - Auto-removal: Remove unresponsive devices after failures
 * - Thread-safe: Proper mutex protection for console output
 * - Multi-port: Support multiple serial ports simultaneously
 */

#include <signal.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#include "DeviceConfigurator.h"
#include "ModbusDeviceManager.h"
#include "modbus_port.h"

// ============================================================================
// CONFIGURATION CONSTANTS
// ============================================================================
namespace {
constexpr int POLL_DELAY_MS = 50;     // Delay between device polls
constexpr int CYCLE_DELAY_MS = 100;   // Delay between poll cycles
constexpr int MAX_FAILURES = 3;       // Failures before device removal
constexpr int INIT_TIMEOUT_MS = 500;  // Modbus initialization timeout
}  // namespace

// ============================================================================
// GLOBAL VARIABLES
// ============================================================================
static std::atomic<bool> g_running{true};
static std::mutex g_display_mutex;

// ============================================================================
// SIGNAL HANDLER
// ============================================================================
void signalHandler(int signum) {
  std::lock_guard<std::mutex> lock(g_display_mutex);
  std::cout << "\n[SIGNAL] Shutdown requested (Ctrl+C)\n";
  g_running = false;
}

// ============================================================================
// CALLBACK FOR CONTROL RESPONSES
// ============================================================================
void responseHandler(const std::string& response) {
  std::lock_guard<std::mutex> lock(g_display_mutex);
  std::cout << "[RESPONSE] " << response << "\n";
}

// ============================================================================
// DISPLAY FUNCTIONS
// ============================================================================
void displayMeterData(const std::string& device_id, const MeterData& data) {
  std::lock_guard<std::mutex> lock(g_display_mutex);

  std::cout << "\n┌─────────────────────────────────────────┐\n";
  std::cout << "│ METER: " << device_id
            << std::string(30 - device_id.length(), ' ') << "│\n";
  std::cout << "├─────────────────────────────────────────┤\n";
  std::cout << "│ Timestamp:      " << data.timestamp << " s\n";
  std::cout << "│ Voltage L1:     " << data.voltage_l1 << " V\n";
  std::cout << "│ Voltage L2:     " << data.voltage_l2 << " V\n";
  std::cout << "│ Voltage L3:     " << data.voltage_l3 << " V\n";
  std::cout << "│ Current L1:     " << data.current_l1 << " A\n";
  std::cout << "│ Current L2:     " << data.current_l2 << " A\n";
  std::cout << "│ Current L3:     " << data.current_l3 << " A\n";
  std::cout << "│ Power L1:       " << data.power_l1 << " W\n";
  std::cout << "│ Power L2:       " << data.power_l2 << " W\n";
  std::cout << "│ Power L3:       " << data.power_l3 << " W\n";
  std::cout << "│ Total Power:    " << data.power << " W\n";
  std::cout << "│ Frequency:      " << data.frequency << " Hz\n";
  std::cout << "└─────────────────────────────────────────┘\n";
}

void displayInverterData(const std::string& device_id,
                         const InverterData& data) {
  std::lock_guard<std::mutex> lock(g_display_mutex);

  std::cout << "\n┌─────────────────────────────────────────┐\n";
  std::cout << "│ INVERTER: " << device_id
            << std::string(27 - device_id.length(), ' ') << "│\n";
  std::cout << "├─────────────────────────────────────────┤\n";
  std::cout << "│ Target Power:   " << data.target_power_percent << " %\n";
  std::cout << "│ Daily Energy:   " << data.daily_energy << " kWh\n";
  std::cout << "│ Total Energy:   " << data.total_energy << " kWh\n";
  std::cout << "└─────────────────────────────────────────┘\n";
}

// ============================================================================
// DEVICE STATUS TRACKING
// ============================================================================
struct DeviceStatus {
  std::shared_ptr<Device> device;
  int consecutive_failures;

  explicit DeviceStatus(std::shared_ptr<Device> dev)
      : device(std::move(dev)), consecutive_failures(0) {}
};

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================
void logInfo(const std::string& port, const std::string& msg) {
  std::lock_guard<std::mutex> lock(g_display_mutex);
  std::cout << "[PORT " << port << "] " << msg << "\n";
}

void logError(const std::string& port, const std::string& msg) {
  std::lock_guard<std::mutex> lock(g_display_mutex);
  std::cerr << "[PORT " << port << "] " << msg << "\n";
}

// ============================================================================
// AUTO-DISCOVERY: Probe devices to find which ones respond on this port
// ============================================================================
std::vector<DeviceStatus> discoverDevicesOnPort(
    ModbusPort& port, const std::string& port_name,
    const std::vector<std::shared_ptr<Device>>& all_devices) {
  logInfo(port_name, "Starting device discovery...");

  std::vector<DeviceStatus> discovered;

  for (const auto& device : all_devices) {
    if (!device->model_ptr) {
      continue;
    }

    // Try to read from device
    std::vector<uint16_t> test_buffer(device->model_ptr->quantity);
    bool success = port.read(device->slave_id, device->model_ptr->start_address,
                             device->model_ptr->quantity, test_buffer.data());

    if (success) {
      discovered.emplace_back(device);
      logInfo(port_name, "Discovered device: " + device->id + " (Slave " +
                             std::to_string(device->slave_id) + ")");
    }

    // Small delay between probes
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  if (discovered.empty()) {
    logInfo(port_name, "No devices discovered on this port");
  } else {
    logInfo(port_name,
            "Discovery complete: " + std::to_string(discovered.size()) +
                " device(s) found");
  }

  return discovered;
}

void listSystemStatus(ModbusDeviceManager* manager) {
  auto stats = manager->getStatistics();

  std::cout << "=== GATEWAY REPORT ===" << std::endl;
  std::cout << "Total Devices: " << stats.total_devices << std::endl;
  std::cout << "Online: " << stats.active_devices
            << " | Offline: " << (stats.total_devices - stats.active_devices)
            << std::endl;
  std::cout << "Error Rate: " << stats.errors_count << " failure(s)"
            << std::endl;
  std::cout << "-----------------------" << std::endl;
}
// ============================================================================
// POLLING THREAD
// ============================================================================
void realModbusPollingThread(ModbusDeviceManager* manager,
                             const std::string& serial_port, int baud_rate) {
  try {
    // ====================================================================
    // STEP 1: Initialize Modbus Port
    // ====================================================================
    ModbusPort port(serial_port.c_str());

    if (!port.init(baud_rate, INIT_TIMEOUT_MS)) {
      logError(serial_port, "Failed to initialize port");
      return;
    }

    logInfo(serial_port,
            "Port initialized @ " + std::to_string(baud_rate) + " baud");

    // ====================================================================
    // STEP 2: Auto-Discovery
    // ====================================================================
    auto all_devices = manager->getDevices();
    auto device_statuses =
        discoverDevicesOnPort(port, serial_port, all_devices);

    if (device_statuses.empty()) {
      logInfo(serial_port, "No devices to poll. Thread exiting.");
      return;
    }

    // ====================================================================
    // STEP 3: Main Polling Loop
    // ====================================================================
    int cycle_count = 0;

    while (g_running && !device_statuses.empty()) {
            cycle_count++;

      // Poll each device
      for (auto it = device_statuses.begin(); it != device_statuses.end();) {
        if (!g_running) break;

        auto& status = *it;
        auto& device = status.device;

        // Prepare read buffer
        std::vector<uint16_t> buffer(device->model_ptr->quantity);

        // Attempt to read from device
        bool success =
            port.read(device->slave_id, device->model_ptr->start_address,
                      device->model_ptr->quantity, buffer.data());

        if (success) {
          // Reset failure counter
          status.consecutive_failures = 0;

          // Process data through manager
          if (manager->pollDevice(device->id, buffer.data())) {
            // Display data based on device type
            if (device->type == "meter") {
              MeterData data;
              if (manager->getMeterData(device->id, data)) {
                displayMeterData(device->id, data);
              }
            } else if (device->type == "inverter") {
              InverterData data;
              if (manager->getInverterData(device->id, data)) {
                displayInverterData(device->id, data);
              }
            }
          }

          ++it;  // Move to next device

        } else {
          // Increment failure counter
          status.consecutive_failures++;

          if (status.consecutive_failures >= MAX_FAILURES) {
            // Remove device after max failures
            logError(serial_port, "REMOVING device " + device->id + " after " +
                                      std::to_string(MAX_FAILURES) +
                                      " consecutive failures");

            it = device_statuses.erase(it);  // Remove from local list

          } else {
            // Log warning but keep trying
            logError(serial_port,
                     "Read failed for " + device->id + " (attempt " +
                         std::to_string(status.consecutive_failures) + "/" +
                         std::to_string(MAX_FAILURES) + ")");
            ++it;
          }
        }

        // Delay between device polls
        std::this_thread::sleep_for(std::chrono::milliseconds(POLL_DELAY_MS));
      }

      // Check if all devices removed
      if (device_statuses.empty()) {
        logError(serial_port, "All devices removed. Thread exiting.");
        break;
      }
      listSystemStatus(manager);

      // Delay between poll cycles
      std::this_thread::sleep_for(std::chrono::milliseconds(CYCLE_DELAY_MS));
    }

    // ====================================================================
    // STEP 4: Cleanup and Statistics
    // ====================================================================
    logInfo(serial_port,
            "Thread stopped. Cycles: " + std::to_string(cycle_count) +
                ", Reads: " + std::to_string(port.getTotalReads()) +
                ", Errors: " + std::to_string(port.getTotalErrors()));

  } catch (const std::exception& e) {
    logError(serial_port, std::string("FATAL: ") + e.what());
    g_running = false;
  }
}

// ============================================================================
// MAIN FUNCTION
// ============================================================================
int main(int argc, char** argv) {
  // Setup signal handlers
  signal(SIGINT, signalHandler);
  signal(SIGTERM, signalHandler);

  // Default configuration
  std::string devices_file = "devices.json";
  std::string mappings_folder = "./mappings";
  std::string serial_port1 = "/dev/ttyS3";
  std::string serial_port2 = "/dev/ttyS1";
  int baud_rate = 115200;

  // Parse command line arguments
  if (argc > 1) devices_file = argv[1];
  if (argc > 2) mappings_folder = argv[2];
  if (argc > 3) serial_port1 = argv[3];
  if (argc > 4) serial_port2 = argv[4];
  if (argc > 5) baud_rate = std::atoi(argv[5]);

  std::cout << "\n╔═══════════════════════════════════════════╗\n";
  std::cout << "║   Modbus RTU Polling with Auto-Discovery  ║\n";
  std::cout << "╚═══════════════════════════════════════════╝\n";

  try {
    // ====================================================================
    // STEP 1: Create Device Manager
    // ====================================================================
    ModbusDeviceManager manager;
    manager.setResponseCallback(responseHandler);

    // ====================================================================
    // STEP 2: Load Configuration
    // ====================================================================
    std::cout << "\n[CONFIG] Loading configuration...\n";
    std::cout << "  Devices file:    " << devices_file << "\n";
    std::cout << "  Mappings folder: " << mappings_folder << "\n";

    DeviceConfigurator::Config config;
    config.validate_schemas = true;
    config.skip_invalid_files = true;
    config.log_level = LogLevel::INFO;

    ConfigError result = DeviceConfigurator::configure(manager, devices_file,
                                                       mappings_folder, config);

    if (result.hasError()) {
      std::cerr << "\n[ERROR] Configuration failed!\n";
      std::cerr << result.toString() << "\n";

      switch (result.type) {
        case ConfigError::Type::FileNotFound:
          std::cerr << "Hint: Check if files exist\n";
          break;
        case ConfigError::Type::ParseError:
          std::cerr << "Hint: Check JSON syntax\n";
          break;
        case ConfigError::Type::ValidationError:
          std::cerr << "Hint: Check JSON schema\n";
          break;
        default:
          break;
      }
      return 1;
    }

    std::cout << "  ✓ Configuration loaded successfully\n";

    // ====================================================================
    // STEP 3: Display System Information
    // ====================================================================
    manager.printDebugInfo();

    // ====================================================================
    // STEP 4: Start Polling Threads
    // ====================================================================
    std::cout << "\n[POLLING] Starting polling threads...\n";
    std::cout << "  Port 1: " << serial_port1 << " @ " << baud_rate
              << " baud\n";

    std::thread polling_thread1(realModbusPollingThread, &manager, serial_port1,
                                baud_rate);

    // Uncomment to enable second port

    std::thread polling_thread2(realModbusPollingThread, &manager, serial_port2,
                                baud_rate);

    std::cout << "\n╔═══════════════════════════════════════════╗\n";
    std::cout << "║  System running. Press Ctrl+C to stop.    ║\n";
    std::cout << "╚═══════════════════════════════════════════╝\n";

    // ====================================================================
    // STEP 5: Wait for Threads
    // ====================================================================
    polling_thread1.join();
    polling_thread2.join();

    std::cout << "\n[SHUTDOWN] All threads stopped\n";

  } catch (const std::exception& e) {
    std::cerr << "\n[FATAL] " << e.what() << "\n";
    return 1;
  }

  return 0;
}