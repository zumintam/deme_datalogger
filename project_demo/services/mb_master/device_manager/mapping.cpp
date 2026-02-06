#include "mapping.h"
using FieldMap = std::map<std::string, float*>;
FieldMap DataMapperP::map_meter(MeterData& data) {
  FieldMap map;
  map["Voltage_L1"] = &data.voltage_l1;
  map["Voltage_L2"] = &data.voltage_l2;
  map["Voltage_L3"] = &data.voltage_l3;
  map["Current_L1"] = &data.current_l1;
  map["Current_L2"] = &data.current_l2;
  map["Current_L3"] = &data.current_l3;
  map["Total_active_power"] = &data.power;
  map["Real_Power_L1"] = &data.power_l1;
  map["Real_Power_L2"] = &data.power_l2;
  map["Real_Power_L3"] = &data.power_l3;
  map["Positive_Real_Energy"] = &data.positive_energy;
  map["Negative_Real_Energy"] = &data.negative_energy;
  map["Positive_Reactive_Energy"] = &data.reactive_energy;
  map["Frequency"] = &data.frequency;
  map["Total_power_factor"] = &data.power_factor;
  return map;
}
FieldMap DataMapperP::map_inverter_basic(InverterData& data) {
  FieldMap map_inv;
  map_inv["DC_Voltage"] = &data.dc_voltage;
  map_inv["DC_Current"] = &data.dc_current;
  map_inv["DC_Power"] = &data.dc_power;
  map_inv["AC_Voltage"] = &data.ac_voltage;
  map_inv["AC_Current"] = &data.ac_current;
  map_inv["AC_Power"] = &data.ac_power;
  map_inv["AC_Frequency"] = &data.ac_frequency;
  map_inv["Daily_Energy"] = &data.daily_energy;
  map_inv["Total_Energy"] = &data.total_energy;
  map_inv["Temperature"] = &data.temperature;
  map_inv["Target_Power_Percent"] = &data.target_power_percent;
  map_inv["Target_Power_Watt"] = &data.target_power_watt;
  map_inv["Target_Reactive_Power"] = &data.target_reactive_power;
  return map_inv;
}

FieldMap DataMapperP::map_inverter_enhance(InverterData& data) {
  FieldMap map = map_inverter_basic(data);
  map["Enable_Output"] = reinterpret_cast<float*>(&data.enable_output);
  map["Reset_Fault"] = reinterpret_cast<float*>(&data.reset_fault);
  map["Operation_Mode"] = reinterpret_cast<float*>(&data.operation_mode);
  return map;
}

// Swap functions
uint16_t DataMapperP::swap16(uint16_t val) { return (val << 8) | (val >> 8); }
uint32_t DataMapperP::swap32(uint32_t val) {
  return ((val & 0xFF000000) >> 24) | ((val & 0x00FF0000) >> 8) |
         ((val & 0x0000FF00) << 8) | ((val & 0x000000FF) << 24);
}

// Map Meter Data
void DataMapperP::mapMeter(const uint16_t* raw_buf, const DeviceModel& model,
                           MeterData& data) {
  data.timestamp = time(NULL);
  data.valid = true;

  FieldMap field_map = map_meter(data);

  for (size_t i = 0; i < model.registers.size(); i++) {
    const RegisterMapping& reg = model.registers[i];
    int offset = reg.address - model.start_address;

    float value = parseRawValue(raw_buf, offset, reg.type, model.byte_order);
    value *= reg.gain;

    FieldMap::iterator it = field_map.find(reg.name);
    if (it != field_map.end()) {
      *(it->second) = value;
    }
  }
}

// Map Inverter Data
void DataMapperP::mapInverter(const uint16_t* raw_buf, const DeviceModel& model,
                              InverterData& data) {
  data.timestamp = time(NULL);
  data.valid = true;

  FieldMap field_map = map_inverter_enhance(data);

  for (size_t i = 0; i < model.registers.size(); i++) {
    const RegisterMapping& reg = model.registers[i];
    int offset = reg.address - model.start_address;

    float value = parseRawValue(raw_buf, offset, reg.type, model.byte_order);
    value *= reg.gain;

    FieldMap::iterator it = field_map.find(reg.name);
    if (it != field_map.end()) {
      *(it->second) = value;
    }
  }
}
float DataMapperP::parseRawValue(const uint16_t* raw_buf, int offset,
                                 RegType type, ByteOrder byte_order) {
  if (type == RegType::U16) {
    return static_cast<float>(raw_buf[offset]);
  }

  if (type == RegType::U32 || type == RegType::I32) {
    uint16_t high, low;

    // BigEndian ở đây hiểu là thứ tự thanh ghi Cao đứng trước (ABCD)
    if (byte_order == ByteOrder::BigEndian) {
      high = raw_buf[offset];
      low = raw_buf[offset + 1];
    }
    // BigEndianSwap ở đây thường là kiểu CDAB (Low Word đứng trước)
    else if (byte_order == ByteOrder::BigEndianSwap) {
      low = raw_buf[offset];       // Thanh ghi đầu chứa phần thấp
      high = raw_buf[offset + 1];  // Thanh ghi sau chứa phần cao
    } else {  // LittleEndian (thực tế Modbus rất ít dùng kiểu này cho 32-bit)
      low = raw_buf[offset];
      high = raw_buf[offset + 1];
    }

    uint32_t val = (static_cast<uint32_t>(high) << 16) | low;

    if (type == RegType::U32) return static_cast<float>(val);
    return static_cast<float>(static_cast<int32_t>(val));
  }

  return 0.0f;
}
// Unmap Inverter (for writing back)
void DataMapperP::unmapInverter(const InverterData& data,
                                const DeviceModel& model, uint16_t* raw_buf) {
  for (size_t i = 0; i < model.registers.size(); i++) {
    const RegisterMapping& reg = model.registers[i];

    if (!reg.writable) continue;

    int offset = reg.address - model.start_address;
    float value = 0.0f;

    if (reg.name == "Target_Power_Percent")
      value = data.target_power_percent;
    else if (reg.name == "Target_Power_Watt")
      value = data.target_power_watt;
    else if (reg.name == "Target_Reactive_Power")
      value = data.target_reactive_power;
    else if (reg.name == "Enable_Output")
      value = data.enable_output ? 1.0f : 0.0f;
    else if (reg.name == "Reset_Fault")
      value = data.reset_fault ? 1.0f : 0.0f;
    else if (reg.name == "Operation_Mode")
      value = static_cast<float>(data.operation_mode);

    value /= reg.gain;
    writeRawValue(raw_buf, offset, value, reg.type, model.byte_order);
  }
}

void DataMapperP::writeRawValue(uint16_t* raw_buf, int offset, float value,
                                RegType type, ByteOrder byte_order) {
  if (type == RegType::U16) {
    uint16_t val = static_cast<uint16_t>(value);
    raw_buf[offset] = (byte_order == ByteOrder::BigEndian) ? swap16(val) : val;
  } else if (type == RegType::U32 || type == RegType::I32) {
    uint32_t val = (type == RegType::U32)
                       ? static_cast<uint32_t>(value)
                       : static_cast<uint32_t>(static_cast<int32_t>(value));

    uint16_t high = (val >> 16) & 0xFFFF;
    uint16_t low = val & 0xFFFF;

    if (byte_order == ByteOrder::BigEndian) {
      raw_buf[offset] = swap16(high);
      raw_buf[offset + 1] = swap16(low);
    } else if (byte_order == ByteOrder::BigEndianSwap) {
      raw_buf[offset] = swap16(low);
      raw_buf[offset + 1] = swap16(high);
    } else {
      raw_buf[offset] = low;
      raw_buf[offset + 1] = high;
    }
  }
}