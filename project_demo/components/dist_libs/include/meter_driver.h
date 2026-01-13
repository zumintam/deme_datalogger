#pragma once

#include <modbus/modbus.h>

#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "meter_config.h"

using MeterData = std::map<std::string, double>;

class MeterDriver {
 public:
  explicit MeterDriver(const MeterConfig& config);

  MeterData readAllAndScaleData();
  MeterData readRawData();

 private:
  bool establishConnection();
  double readRawRegister(const RegisterMapping& reg);
  std::uint16_t getModbusAddress(std::uint16_t register_address) const;

  MeterConfig config_;
  std::unique_ptr<modbus_t, decltype(&modbus_free)> ctx_{nullptr, modbus_free};
  std::mutex modbus_lock_;
};
