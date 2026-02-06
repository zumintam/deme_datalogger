/**
 * @file device.cpp
 * @brief Device class implementations
 */

#include "device.h"

// ============================================================================
// BASE DEVICE IMPLEMENTATION
// ============================================================================

Device::Device() : slave_id(1), model_ptr(nullptr) {}

// ============================================================================
// METER DEVICE IMPLEMENTATION
// ============================================================================

MeterDevice::MeterDevice() { type = "meter"; }

void MeterDevice::processData(const uint16_t* raw_buffer) {
  if (!model_ptr) {
    return;  // No model configured, cannot process
  }
  // Use DataMapper to convert raw Modbus data to structured format
  DataMapperP::mapMeter(raw_buffer, *model_ptr, storage);
}

bool MeterDevice::handleCommand(ControlCommand& cmd) {
  // Meters are typically read-only devices
  cmd.error_msg = "Meter devices do not support control commands";
  return false;
}

// ============================================================================
// INVERTER DEVICE IMPLEMENTATION
// ============================================================================

InverterDevice::InverterDevice() { type = "inverter"; }

void InverterDevice::processData(const uint16_t* raw_buffer) {
  if (!model_ptr) {
    return;  // No model configured, cannot process
  }

  // Use DataMapper to convert raw Modbus data to structured format
  DataMapperP::mapInverter(raw_buffer, *model_ptr, storage);
}

bool InverterDevice::handleCommand(ControlCommand& cmd) {
  // Handle different command types
  if (cmd.command_type == "set_power_percent") {
    // Validate power percentage range
    if (cmd.value < 0.0f || cmd.value > 100.0f) {
      cmd.error_msg = "Invalid power percent (must be 0-100)";
      return false;
    }
    storage.target_power_percent = cmd.value;
    return true;
  }

  if (cmd.command_type == "set_power_watt") {
    // Set absolute power in watts
    if (cmd.value < 0.0f) {
      cmd.error_msg = "Invalid power value (must be >= 0)";
      return false;
    }
    storage.target_power_watt = cmd.value;
    return true;
  }

  if (cmd.command_type == "set_reactive_power") {
    storage.target_reactive_power = cmd.value;
    return true;
  }

  if (cmd.command_type == "enable") {
    storage.enable_output = true;
    return true;
  }

  if (cmd.command_type == "disable") {
    storage.enable_output = false;
    return true;
  }

  if (cmd.command_type == "reset_fault") {
    storage.reset_fault = true;
    return true;
  }

  if (cmd.command_type == "set_operation_mode") {
    // Operation mode: 0=Off, 1=On, 2=Standby, etc.
    storage.operation_mode = static_cast<uint16_t>(cmd.value);
    return true;
  }

  // Unknown command type
  cmd.error_msg = "Unknown command type for Inverter: " + cmd.command_type;
  return false;
}

// ============================================================================
// DEVICE FACTORY IMPLEMENTATION
// ============================================================================

std::shared_ptr<Device> DeviceFactory::createDevice(const std::string& type) {
  if (type == "meter") {
    return std::make_shared<MeterDevice>();
  }

  if (type == "inverter") {
    return std::make_shared<InverterDevice>();
  }

  // Add more device types here as needed:
  // if (type == "battery") {
  //   return std::make_shared<BatteryDevice>();
  // }
  // if (type == "solar_charger") {
  //   return std::make_shared<SolarChargerDevice>();
  // }

  // Unknown device type
  return nullptr;
}