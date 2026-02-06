#ifndef MAPPING_H
#define MAPPING_H

#include <stdint.h>

#include <cstring>
#include <ctime>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "cJSON.h"
// ============= ENUMS =============
enum class RegType { U16, U32, I32, I16, FLOAT };

enum class ByteOrder { BigEndian, LittleEndian, BigEndianSwap };

// ============= STRUCTS =============

// Register Mapping
struct RegisterMapping {
  uint16_t address;
  std::string name;
  RegType type;
  float gain;
  bool writable;

  RegisterMapping()
      : address(0), type(RegType::U16), gain(1.0f), writable(false) {}

  RegisterMapping(uint16_t addr, const std::string& n, RegType t,
                  float g = 1.0f, bool w = false)
      : address(addr), name(n), type(t), gain(g), writable(w) {}
};

// Device Model Configuration
struct DeviceModel {
  std::string model_name;
  uint16_t start_address;
  uint16_t quantity;
  uint8_t function_code;
  ByteOrder byte_order;
  std::vector<RegisterMapping> registers;

  DeviceModel()
      : start_address(0),
        quantity(0),
        function_code(3),
        byte_order(ByteOrder::BigEndian) {}
};

// Meter Data Structure
struct MeterData {
  time_t timestamp;
  bool valid;

  // Basic measurements
  float voltage;
  float current;
  float power;
  float energy;
  float frequency;
  float power_factor;

  // 3-phase voltage (L-N)
  float voltage_l1;
  float voltage_l2;
  float voltage_l3;

  // 3-phase current
  float current_l1;
  float current_l2;
  float current_l3;

  // 3-phase power
  float power_l1;
  float power_l2;
  float power_l3;

  // Energy counters
  float positive_energy;
  float negative_energy;
  float reactive_energy;

  MeterData() {
    memset(this, 0, sizeof(MeterData));
    timestamp = time(NULL);
    valid = false;
  }

  void print() const;
};

// Inverter Data Structure
struct InverterData {
  time_t timestamp;
  bool valid;

  // Read-only
  float dc_voltage;
  float dc_current;
  float dc_power;

  float ac_voltage;
  float ac_current;
  float ac_power;
  float ac_frequency;

  float daily_energy;
  float total_energy;
  float temperature;

  uint16_t status;
  uint16_t fault_code;

  // Writable control registers
  float target_power_percent;
  float target_power_watt;
  float target_reactive_power;
  bool enable_output;
  bool reset_fault;
  uint16_t operation_mode;

  InverterData() {
    memset(this, 0, sizeof(InverterData));
    timestamp = time(NULL);
    valid = false;
    target_power_percent = 100.0f;
    enable_output = true;
  }
};

// Control Command Structure
struct ControlCommand {
  std::string device_id;
  std::string command_type;
  float value;
  time_t timestamp;
  bool executed;
  std::string result;
  std::string error_msg;
  uint16_t register_address;
  ControlCommand()
      : value(0), timestamp(0), executed(false), result("PENDING") {}
};

class DataMapperP {
 private:
  // map
  using FieldMap = std::map<std::string, float*>;
  // Build field map for Meter
  static FieldMap map_meter(MeterData& data);
  static FieldMap map_inverter_basic(InverterData& data);
  // extend
  static FieldMap map_inverter_enhance(
      InverterData& data);  // them cac truong co the ghi
  static uint16_t swap16(uint16_t val);
  static uint32_t swap32(uint32_t val);

 public:
  static void mapMeter(const uint16_t* raw_buf, const DeviceModel& model,
                       MeterData& data);
  static void mapInverter(const uint16_t* raw_buf, const DeviceModel& model,
                          InverterData& data);
  static float parseRawValue(const uint16_t* raw_buf, int offset, RegType type,
                             ByteOrder byte_order);
  static void unmapInverter(const InverterData& data, const DeviceModel& model,
                            uint16_t* raw_buf);
  static void writeRawValue(uint16_t* raw_buf, int offset, float value,
                            RegType type, ByteOrder byte_order);
};

#endif  // MAPPING_H