/**
 * @file modbus_port.h
 * @brief Thread-safe Modbus RTU port implementation (Optimized)
 */

#ifndef MODBUS_PORT_H
#define MODBUS_PORT_H

#include <modbus/modbus.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

class ModbusPort {
 public:
  explicit ModbusPort(const char* com);
  ~ModbusPort();

  // Initialization
  bool init(int baud, int timeout_ms = 500);

  // Read operations (backward compatible)
  bool readHolding(uint8_t slave_id, uint16_t addr, uint16_t qty,
                   uint16_t* dest);
  bool read(uint8_t slave_id, uint16_t addr, uint16_t qty, uint16_t* dest);

  // Write operations
  bool writeSingle(uint8_t slave_id, uint16_t addr, uint16_t value);
  bool writeMultiple(uint8_t slave_id, uint16_t addr, uint16_t qty,
                     const uint16_t* data);

  // Statistics
  void resetStats() {
    total_reads_ = 0;
    total_writes_ = 0;
    total_errors_ = 0;
  }

  uint32_t getTotalReads() const { return total_reads_; }
  uint32_t getTotalWrites() const { return total_writes_; }
  uint32_t getTotalErrors() const { return total_errors_; }

 private:
  // Core read implementation with options
  bool readRegisters(uint8_t slave_id, uint16_t addr, uint16_t qty,
                     uint16_t* dest, bool flush, bool verbose);

  // Helper functions
  bool logError(const std::string& msg);
  bool validateQuantity(uint16_t qty, uint16_t max_qty,
                        const std::string& operation);

  // Member variables
  std::string port_name_;
  std::unique_ptr<modbus_t, decltype(&modbus_free)> ctx_{nullptr, modbus_free};
  std::mutex bus_mutex_;

  // Statistics
  std::atomic<uint32_t> total_reads_{0};
  std::atomic<uint32_t> total_writes_{0};
  std::atomic<uint32_t> total_errors_{0};
};

#endif  // MODBUS_PORT_H