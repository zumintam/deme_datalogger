#pragma once

// ⚡ LƯU Ý: Đổi <meter_config.h> thành "meter_config.h" nếu nó cùng thư mục
// hoặc dùng cú pháp tương đối
#include <modbus.h>

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>

#include "meter_config.h"

// Kết quả đọc dữ liệu cuối cùng: [Tên thanh ghi, Giá trị thực]
using MeterData = std::map<std::string, double>;

// 1. Custom Deleter cho modbus_t*
struct ModbusDeleter {
  void operator()(modbus_t* ctx) const {
    if (ctx) {
      modbus_close(ctx);
      modbus_free(ctx);
      std::cout << "[INFO] Modbus context da dong va giai phong." << std::endl;
    }
  }
};
// 2. Alias cho unique_ptr an toan
using ModbusContextPtr = std::unique_ptr<modbus_t, ModbusDeleter>;

class MeterDriver {
 public:
  // Constructor sử dụng Dependency Injection
  MeterDriver(const MeterConfig& config);
  // Destructor mac dinh tu lo viec giai phong tai nguyen

  MeterData readAllAndScaleData();

 private:
  ModbusContextPtr ctx_;
  MeterConfig config_;
  std::mutex modbus_lock_;  // Mutex cho Thread Safety

  bool establishConnection();

  // Đọc một thanh ghi với (Retry)
  double readAndScaleRegister(const RegisterConfig& reg);

  // Xử lý chuyển đổi địa chỉ
  std::uint16_t getModbusAddress(std::uint16_t register_address) const;
  // ham khoi tao modbus context theo kieu tcp hoac udp
  bool creatCtx(uint8_t port);
  bool creatCtx(uint16_t port, uint32_t ip);

  // ham tuong tac
  int begin(void);
  void setSlaveId(uint8_t slaveId);
  void close();
  // ham tuong tac voi thanh ghi
  
};