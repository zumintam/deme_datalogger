#include "modbus_manager.h"
class ModbusManager {
 private:
  ModbusMaster mb_;
  ModbusContextPtr ctx_;
  Config config_;
  bool is_connected_;

 public:
  ModbusManager() : is_connected_(false) {}

  ~ModbusManager() { cleanup(); }

  bool init(int port, const std::string& config_file = "DPM380.json") {
    // Load config
    std::string fileContent = readFileToString(config_file);
    if (fileContent.empty()) {
      std::cerr << "ERROR: Config file is empty or not found" << std::endl;
      return false;
    }

    if (!config_.meter.loadFromJson(fileContent) || !config_.meter.validate()) {
      std::cerr << "ERROR: Failed to load/validate config" << std::endl;
      return false;
    }

    // Create context
    ctx_ = mb_.create_rtu_ctx(port);
    if (!ctx_) {
      std::cerr << "ERROR: Failed to create Modbus context" << std::endl;
      return false;
    }

    // Set timeouts
    modbus_set_response_timeout(ctx_.get(), 1, 0);
    modbus_set_byte_timeout(ctx_.get(), 0, 500000);

    // Connect
    if (!mb_.connect(ctx_.get())) {
      std::cerr << "ERROR: Failed to connect to Modbus device" << std::endl;
      return false;
    }

    is_connected_ = true;
    std::cout << "INFO: Modbus initialized successfully" << std::endl;
    return true;
  }

  bool readData(uint16_t* raw_data) {
    if (!is_connected_ || !ctx_) {
      std::cerr << "ERROR: Modbus not connected" << std::endl;
      return false;
    }

    if (!raw_data) {
      std::cerr << "ERROR: raw_data buffer is null" << std::endl;
      return false;
    }

    int rc = mb_.readInputRegisters(ctx_.get(), config_.meter.start_address,
                                    config_.meter.quantity, raw_data);

    if (rc == -1) {
      std::cerr << "ERROR: Modbus read failed: " << modbus_strerror(errno)
                << std::endl;
      return false;
    }

    if (rc != config_.meter.quantity) {
      std::cerr << "WARNING: Expected " << config_.meter.quantity
                << " registers, got " << rc << std::endl;
      return false;
    }

    return true;
  }

  void cleanup() {
    if (ctx_) {
      try {
        modbus_close(ctx_.get());
        modbus_free(ctx_.get());
        ctx_.reset();
        is_connected_ = false;
        std::cout << "INFO: Modbus connection closed" << std::endl;
      } catch (const std::exception& e) {
        std::cerr << "ERROR: Exception during cleanup: " << e.what()
                  << std::endl;
      }
    }
  }

  bool isConnected() const { return is_connected_; }
  const ModbusConfig& getConfig() const { return config_.meter; }
};

// Sử dụng:
// main.cpp
int main() {
  ModbusManager modbus;

  if (!modbus.init(3, "DPM380.json")) {  // port 3
    return -1;
  }

  uint16_t data[200];
  if (!modbus.readData(data)) {
    return -1;
  }

  // modbus tự động cleanup khi out of scope
  return 0;
}