#pragma once

#include <cstdint>
#include <iostream>
#include <map>
#include <string>

extern "C" {
#include "../../../components/libcjson/cJSON.h"
}

struct RegisterConfig {
  std::string name;
  std::uint16_t address;
  double scale;
  // viet them cac truong can thiet o day, neu muon cau hinh them tham so cho
  // moi register
};

class MeterConfig {
 public:
  std::string device_id;
  std::string serial_port;
  int baudrate;
  int slave_id;
  int poll_interval_ms;
  std::map<std::string, RegisterConfig> registers;

  bool loadFromJson(const std::string& filename);

  bool validate() const {
    if (slave_id < 1 || slave_id > 247) {
      std::cerr << "[VALIDATION FAIL] Slave ID (" << slave_id
                << ") phai trong khoang [1, 247]." << std::endl;
      return false;
    }
    if (baudrate <= 0) {
      std::cerr << "[VALIDATION FAIL] Baudrate phai la so duong." << std::endl;
      return false;
    }
    if (registers.empty()) {
      std::cerr << "[VALIDATION FAIL] Khong co thanh ghi nao duoc cau hinh."
                << std::endl;
      return false;
    }
    return true;
  }
};

std::string readFileToString(const std::string& filename);