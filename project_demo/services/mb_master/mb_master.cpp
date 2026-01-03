#include "mb_master.h"

ModbusContextPtr ModbusMaster::create_rtu_ctx(const uint8_t port) {
  modbus_t* ctx = modbus_new_rtu("/dev/ttyS3", 9600, 'N', 8, 1);

  return ModbusContextPtr(ctx);
}

ModbusContextPtr ModbusMaster::create_tcp_ctx(const std::string& ip,
                                              uint16_t port) {
  modbus_t* ctx = modbus_new_tcp(ip.c_str(), port);
  return ModbusContextPtr(ctx);
}

bool ModbusMaster::connect(modbus_t* ctx) {
  if (!ctx) return false;
  return modbus_connect(ctx) == 0;
}

void ModbusMaster::setSlaveId(modbus_t* ctx, uint8_t slaveId) {
  if (ctx) modbus_set_slave(ctx, slaveId);
}

// ---------------- Read ----------------

int ModbusMaster::readInputRegisters(modbus_t* ctx, uint16_t addr, uint16_t qty,
                                     uint16_t* dest) {
  if (!ctx || !dest) return -1;

  std::lock_guard<std::mutex> lock(bus_mutex_);
  return modbus_read_input_registers(ctx, addr, qty, dest);
}

int ModbusMaster::readHoldingRegisters(modbus_t* ctx, uint16_t addr,
                                       uint16_t qty, uint16_t* dest) {
  if (!ctx || !dest) return -1;  // kiem tra ctx va dest NULL?

  std::lock_guard<std::mutex> lock(bus_mutex_);
  return modbus_read_registers(ctx, addr, qty, dest);
}

// ---------------- Write ----------------

int ModbusMaster::writeSingleRegister(modbus_t* ctx, uint16_t addr,
                                      uint16_t value) {
  if (!ctx) return -1;

  std::lock_guard<std::mutex> lock(bus_mutex_);
  return modbus_write_register(ctx, addr, value);
}

int ModbusMaster::writeMultipleRegisters(modbus_t* ctx, uint16_t addr,
                                         uint16_t qty, const uint16_t* src) {
  if (!ctx || !src) return -1;

  std::lock_guard<std::mutex> lock(bus_mutex_);
  return modbus_write_registers(ctx, addr, qty, src);
}
