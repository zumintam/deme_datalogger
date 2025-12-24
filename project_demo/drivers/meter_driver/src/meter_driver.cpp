#include "meter_driver.h"

#include <chrono>
#include <iostream>
#include <thread>

extern int errno;

MeterDriver::MeterDriver(const MeterConfig& config) : config_(config) {
  if (!establishConnection()) {
    throw std::runtime_error(
        "Khoi tao Driver that bai. Khong the ket noi Modbus.");
  }
  std::cout << "[INFO] Driver " << config_.device_id << " đã sẵn sàng."
            << std::endl;
}

bool MeterDriver::establishConnection() {
  modbus_t* temp_ctx =
      modbus_new_rtu(config_.serial_port.c_str(), config_.baudrate, 'N', 8, 1);

  ctx_.reset(temp_ctx);

  if (!ctx_) {
    std::cerr << "[FAIL] Khong the tao Modbus context: "
              << modbus_strerror(errno) << std::endl;
    return false;
  }

  if (modbus_set_slave(ctx_.get(), config_.slave_id) == -1) {
    std::cerr << "[FAIL] Khong the thiet lap Slave ID " << config_.slave_id
              << ": " << modbus_strerror(errno) << std::endl;
    ctx_.reset(nullptr);
    return false;
  }

  if (modbus_connect(ctx_.get()) == -1) {
    std::cerr << "[FAIL] Khong the ket noi Modbus toi " << config_.serial_port
              << ": " << modbus_strerror(errno) << std::endl;
    ctx_.reset(nullptr);
    return false;
  }

  return true;
}

std::uint16_t MeterDriver::getModbusAddress(
    std::uint16_t register_address) const {
  return register_address;
}

double MeterDriver::readAndScaleRegister(const RegisterConfig& reg) {
  if (!ctx_) return -999.0;

  std::lock_guard<std::mutex> lock(modbus_lock_);

  const int MAX_RETRIES = 3;
  const int num_registers = 100;
  std::uint16_t raw_data[num_registers];

  std::uint16_t modbus_addr = getModbusAddress(reg.address);

  for (int retry = 0; retry < MAX_RETRIES; ++retry) {
    int num_read =
        modbus_read_registers(ctx_.get(), modbus_addr, num_registers, raw_data);

    std::cout << "[INFO] Ham doc (Holding): " << num_read << " registers"
              << std::endl;

    if (num_read == num_registers) {
      std::uint16_t raw_value = raw_data[0];
      double real_value = (double)raw_value * reg.scale;

      std::cout << "[INFO] Thanh ghi Holding " << reg.address
                << " | Raw: " << raw_value << " | Scale: " << reg.scale
                << " | Ket qua: " << real_value << std::endl;

      return real_value;
    }

    std::cerr << "[WARN] Doc thanh ghi " << reg.address << " that bai (Thu #"
              << retry + 1 << ")..." << std::endl;

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  return -999.0;
}

MeterData MeterDriver::readAllAndScaleData() {
  MeterData results;

  std::cout << "\n--- BAT DAU DOC VA SCALE DU LIEU (" << config_.device_id
            << ") ---" << std::endl;

  for (const auto& pair : config_.registers) {
    const RegisterConfig& reg = pair.second;

    double value = readAndScaleRegister(reg);

    if (value > -999.0) {
      results[reg.name] = value;
      std::cout << "[READ SCALED] " << reg.name << ": " << value << std::endl;
    }
  }

  return results;
}

// rewrite constructor line
MeterData MeterDriver::readRawData() {
  MeterData results;

  std::cout << "\n--- BAT DAU DOC DU LIEU THO NGUYEN (" << config_.device_id
            << ") ---" << std::endl;

  for (const auto& pair : config_.registers) {
    const RegisterConfig& reg = pair.second;

    double value = readRawRegister(reg);

    if (value > -999.0) {
      results[reg.name] = value;
      std::cout << "[READ RAW] " << reg.name << ": " << value << std::endl;
    }
  }

  return results;
}
