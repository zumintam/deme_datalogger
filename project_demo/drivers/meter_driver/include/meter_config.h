#pragma once

#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

extern "C" {
#include "../../../components/libcjson/cJSON.h"
}

// Cấu trúc cho mảng mapping
struct RegisterMapping {
  std::string name;
  std::uint16_t address;
  std::string type;
};

// Cấu trúc cho thông số Modbus
struct ModbusConfig {
  uint16_t start_address;
  uint16_t quantity;
  int function_code;
  std::string byte_order;
};

class MeterConfig {
 public:
  std::string device_model;
  uint8_t slave_id = 1;
  ModbusConfig modbus;
  std::vector<RegisterMapping> mappings;

  bool loadFromJson(const std::string& json_content);

  bool validate() const;

  bool parseRaw2Map(const uint16_t* raw_data,
                    std::map<std::string, double>& output_map) const;
  std::string parse2Json(const std::uint16_t* raw_data) const;
};

std::string readFileToString(const std::string& filename);
