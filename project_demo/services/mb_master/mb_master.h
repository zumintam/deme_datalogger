#pragma once

#include <modbus/modbus.h>
#include <string.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Custom Deleter to automatically close and free context
struct ModbusContextDeleter {
  void operator()(modbus_t* ctx) const {
    if (ctx) {
      modbus_close(ctx);
      modbus_free(ctx);
    }
  }
};

using ModbusContextPtr = std::unique_ptr<modbus_t, ModbusContextDeleter>;

class ModbusMaster {
 private:
  std::mutex bus_mutex_;  // Protects bus access from multiple devices/threads

 public:
  ModbusMaster() = default;
  ~ModbusMaster() = default;

  // Returns smart pointer for automatic lifecycle management
  ModbusContextPtr create_rtu_ctx(const uint8_t port);
  ModbusContextPtr create_tcp_ctx(const std::string& ip, uint16_t port);

  // Use reference or pointer for manipulation
  bool connect(modbus_t* ctx);
  void setSlaveId(modbus_t* ctx, uint8_t slaveId);

  // Read/Write functions using uint16_t* to ensure Modbus memory safety
  // (16-bit)
  int readInputRegisters(modbus_t* ctx, uint16_t addr, uint16_t qty,
                         uint16_t* dest);
  int readHoldingRegisters(modbus_t* ctx, uint16_t addr, uint16_t qty,
                           uint16_t* dest);

  int writeSingleRegister(modbus_t* ctx, uint16_t addr, uint16_t value);
  int writeMultipleRegisters(modbus_t* ctx, uint16_t addr, uint16_t qty,
                             const uint16_t* src);
};