#pragma once
#include <map>
#include <string>

using MeterData = std::map<std::string, float>;

class Driver {
 public:
  virtual ~Driver() = default;
  virtual bool connect() = 0;
  virtual MeterData readData() = 0;
  virtual std::string getDeviceID() = 0;
};