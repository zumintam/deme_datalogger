/**
 * @file modbus_port.cpp
 * @brief Thread-safe Modbus RTU port implementation (Optimized)
 */

#include "modbus_port.h"

#include <iostream>

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

bool ModbusPort::logError(const std::string& msg) {
  std::cerr << "[MODBUS] " << msg << "\n";
  total_errors_++;
  return false;
}

bool ModbusPort::validateQuantity(uint16_t qty, uint16_t max_qty,
                                  const std::string& operation) {
  if (qty == 0 || qty > max_qty) {
    return logError(operation + " invalid quantity: " + std::to_string(qty) +
                    " (must be 1-" + std::to_string(max_qty) + ")");
  }
  return true;
}

// ============================================================================
// CONSTRUCTOR & DESTRUCTOR
// ============================================================================

ModbusPort::ModbusPort(const char* com) : port_name_(com) {
  std::cout << "[MODBUS] Created port instance for: " << port_name_ << "\n";
}

ModbusPort::~ModbusPort() {
  if (ctx_) {
    std::cout << "[MODBUS] Closing connection to: " << port_name_ << "\n";
    modbus_close(ctx_.get());
  }
}

// ============================================================================
// INITIALIZATION
// ============================================================================

bool ModbusPort::init(int baud, int timeout_ms) {
  std::lock_guard<std::mutex> lock(bus_mutex_);

  // Create RTU context
  ctx_.reset(modbus_new_rtu(port_name_.c_str(), baud, 'N', 8, 1));
  if (!ctx_) {
    return logError("Failed to create RTU context for " + port_name_);
  }

  // Set timeouts
  int sec = timeout_ms / 1000;
  int usec = (timeout_ms % 1000) * 1000;
  modbus_set_response_timeout(ctx_.get(), sec, usec);
  modbus_set_byte_timeout(ctx_.get(), 0, timeout_ms * 1000);

  // Connect to serial port
  if (modbus_connect(ctx_.get()) == -1) {
    std::string error = "Connection failed: ";
    error += modbus_strerror(errno);
    ctx_.reset();
    return logError(error);
  }

  std::cout << "[MODBUS] ✓ Connected to " << port_name_ << " @ " << baud
            << " baud (timeout: " << timeout_ms << "ms)\n";

  resetStats();
  return true;
}

// ============================================================================
// READ OPERATIONS - Core Implementation
// ============================================================================

bool ModbusPort::readRegisters(uint8_t slave_id, uint16_t addr, uint16_t qty,
                               uint16_t* dest, bool flush, bool verbose) {
  // Validate inputs before locking
  if (!dest) {
    return logError("Null destination buffer");
  }

  if (!validateQuantity(qty, 125, "Read")) {
    return false;
  }

  // Lock and validate connection
  std::lock_guard<std::mutex> lock(bus_mutex_);

  if (!ctx_) {
    return logError("Not connected");
  }

  // Flush if requested (for noisy RS485 lines)
  if (flush) {
    modbus_flush(ctx_.get());
  }

  // Set slave and perform read
  modbus_set_slave(ctx_.get(), slave_id);
  int rc = modbus_read_registers(ctx_.get(), addr, qty, dest);

  // Handle errors
  if (rc == -1) {
    if (verbose) {
      std::cerr << "[MODBUS] Read failed (Slave " << static_cast<int>(slave_id)
                << ", Addr " << addr << ", Qty " << qty
                << "): " << modbus_strerror(errno) << "\n";
    }
    total_errors_++;
    return false;
  }

  if (rc != qty) {
    if (verbose) {
      std::cerr << "[MODBUS] Read mismatch: expected " << qty
                << " registers, got " << rc << "\n";
    }
    total_errors_++;
    return false;
  }

  total_reads_++;
  return true;
}

// Public wrappers for backward compatibility
bool ModbusPort::readHolding(uint8_t slave_id, uint16_t addr, uint16_t qty,
                             uint16_t* dest) {
  return readRegisters(slave_id, addr, qty, dest, false, true);
}

bool ModbusPort::read(uint8_t slave_id, uint16_t addr, uint16_t qty,
                      uint16_t* dest) {
  // Silent mode with flush for polling operations
  return readRegisters(slave_id, addr, qty, dest, true, false);
}

// ============================================================================
// WRITE OPERATIONS
// ============================================================================

bool ModbusPort::writeSingle(uint8_t slave_id, uint16_t addr, uint16_t value) {
  std::lock_guard<std::mutex> lock(bus_mutex_);

  if (!ctx_) {
    return logError("Not connected");
  }

  modbus_set_slave(ctx_.get(), slave_id);
  int rc = modbus_write_register(ctx_.get(), addr, value);

  if (rc == -1) {
    std::cerr << "[MODBUS] Write single failed (Slave "
              << static_cast<int>(slave_id) << ", Addr " << addr << ", Value "
              << value << "): " << modbus_strerror(errno) << "\n";
    total_errors_++;
    return false;
  }

  total_writes_++;
  return true;
}

bool ModbusPort::writeMultiple(uint8_t slave_id, uint16_t addr, uint16_t qty,
                               const uint16_t* data) {
  // Validate inputs before locking
  if (!data) {
    return logError("Null source buffer");
  }

  if (!validateQuantity(qty, 123, "Write")) {
    return false;
  }

  // Lock and validate connection
  std::lock_guard<std::mutex> lock(bus_mutex_);

  if (!ctx_) {
    return logError("Not connected");
  }

  modbus_set_slave(ctx_.get(), slave_id);
  int rc = modbus_write_registers(ctx_.get(), addr, qty, data);

  if (rc == -1) {
    std::cerr << "[MODBUS] Write multiple failed (Slave "
              << static_cast<int>(slave_id) << ", Addr " << addr << ", Qty "
              << qty << "): " << modbus_strerror(errno) << "\n";
    total_errors_++;
    return false;
  }

  if (rc != qty) {
    std::cerr << "[MODBUS] Write mismatch: expected " << qty
              << " registers, wrote " << rc << "\n";
    total_errors_++;
    return false;
  }

  total_writes_++;
  return true;
}