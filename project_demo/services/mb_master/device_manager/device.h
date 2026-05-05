/**
 * @file device.h
 * @brief Device class declarations and interfaces
 * @version 1.0
 */

#ifndef DEVICE_H_
#define DEVICE_H_

#include <cstdint>
#include <ctime>
#include <memory>
#include <mutex>
#include <string>

#include "mapping.h"

// ============================================================================
// BASE DEVICE INTERFACE
// ============================================================================

/**
 * @brief Abstract base class for all device types
 *
 * Provides common interface and data members for all device implementations.
 * Derived classes must implement processData() and handleCommand().
 */
class Device {
 public:
  // Public data members (accessible by manager)
  std::string id;          ///< Unique device identifier
  std::string name;        ///< Human-readable device name
  std::string type;        ///< Device type ("meter", "inverter", etc.)
  uint8_t slave_id;        ///< Modbus slave ID (1-247)
  DeviceModel* model_ptr;  ///< Pointer to device model configuration
  std::mutex data_mutex;   ///< Mutex for thread-safe data access

  /**
   * @brief Constructor
   */
  Device();

  /**
   * @brief Virtual destructor
   */
  virtual ~Device() = default;

  /**
   * @brief Process raw Modbus data into structured format
   * @param raw_buffer Pointer to raw uint16_t register buffer
   */
  virtual void processData(const uint16_t* raw_buffer) = 0;

  /**
   * @brief Handle control command
   * @param cmd Control command to execute
   * @return true if command accepted, false otherwise
   */
  virtual bool handleCommand(ControlCommand& cmd) = 0;
};

// ============================================================================
// METER DEVICE
// ============================================================================

/**
 * @brief Energy meter device implementation
 *
 * Represents an energy meter that measures voltage, current, power, etc.
 * Typically read-only (does not accept control commands).
 */
class MeterDevice : public Device {
 public:
  MeterData storage;  ///< Meter data storage

  /**
   * @brief Constructor
   */
  MeterDevice();

  /**
   * @brief Process raw Modbus data
   * @param raw_buffer Raw register buffer from Modbus read
   */
  void processData(const uint16_t* raw_buffer) override;

  /**
   * @brief Handle control command (meters typically reject commands)
   * @param cmd Control command
   * @return false (meters are read-only)
   */
  bool handleCommand(ControlCommand& cmd) override;
};

// ============================================================================
// INVERTER DEVICE
// ============================================================================

/**
 * @brief Solar inverter device implementation
 *
 * Represents a solar inverter with both monitoring and control capabilities.
 * Supports commands like set_power_percent, enable/disable, etc.
 */
class InverterDevice : public Device {
 public:
  InverterData storage;  ///< Inverter data storage

  /**
   * @brief Constructor
   */
  InverterDevice();

  /**
   * @brief Process raw Modbus data
   * @param raw_buffer Raw register buffer from Modbus read
   */
  void processData(const uint16_t* raw_buffer) override;

  /**
   * @brief Handle control command
   * @param cmd Control command to execute
   * @return true if command valid and executed
   *
   * Supported commands:
   * - set_power_percent: Set target power output (0-100%)
   * - set_power_watt: Set target power in watts
   * - enable: Enable inverter output
   * - disable: Disable inverter output
   * - reset_fault: Clear fault condition
   */
  bool handleCommand(ControlCommand& cmd) override;
};

// ============================================================================
// DEVICE FACTORY
// ============================================================================

/**
 * @brief Factory class for creating device instances
 *
 * Implements the Factory Pattern for device instantiation.
 * Supports runtime creation of different device types based on string type.
 */
class DeviceFactory {
 public:
  /**
   * @brief Create device instance by type string
   * @param type Device type ("meter", "inverter", etc.)
   * @return Shared pointer to device, or nullptr if type unknown
   *
   * Example:
   * @code
   * auto meter = DeviceFactory::createDevice("meter");
   * auto inverter = DeviceFactory::createDevice("inverter");
   * @endcode
   */
  static std::shared_ptr<Device> createDevice(const std::string& type);
};

#endif  // DEVICE_H_