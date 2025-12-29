#include "modbus_ex.h"

bool initModbusConfig(Config& config, int port, ModbusContextPtr& ctx,
                      ModbusMaster& mb, const std::string& config_file) {
  // Load config
  std::string fileContent = readFileToString(config_file);
  if (fileContent.empty()) {
    std::cerr << "ERROR: Config file is empty or not found: " << config_file
              << std::endl;
    return false;
  }

  if (!config.meter.loadFromJson(fileContent)) {
    std::cerr << "ERROR: Failed to parse JSON config" << std::endl;
    return false;
  }

  if (!config.meter.validate()) {
    std::cerr << "ERROR: Config validation failed" << std::endl;
    return false;
  }

  std::cout << "INFO: Modbus config loaded from " << config_file << std::endl;

  // Create Modbus RTU context
  ctx = mb.create_rtu_ctx(port);
  if (!ctx) {
    std::cerr << "ERROR: Failed to create Modbus RTU context" << std::endl;
    return false;
  }
  // set slave ID
  mb.setSlaveId(ctx.get(), 1);

  // Set timeouts
  modbus_set_response_timeout(ctx.get(), 3, 0);
  modbus_set_byte_timeout(ctx.get(), 1, 0);

  // Connect
  if (!mb.connect(ctx.get())) {
    std::cerr << "ERROR: Failed to connect to Modbus device on port " << port
              << std::endl;
    return false;
  }

  std::cout << "INFO: Connected to Modbus RTU on port " << port << std::endl;
  return true;
}

bool readModbusData(modbus_t* ctx, const ModbusConfig& modbus_config,
                    uint16_t* raw_data, ModbusMaster& mb) {
  std::cout << "INFO: Reading " << modbus_config.quantity
            << " registers starting at address " << modbus_config.start_address
            << std::endl;

  if (!ctx) {
    std::cerr << "ERROR: Modbus context is null" << std::endl;
    return false;
  }

  if (!raw_data) {
    std::cerr << "ERROR: raw_data buffer is null" << std::endl;
    return false;
  }

  // Read registers
  int rc = mb.readHoldingRegisters(ctx, modbus_config.start_address,
                                   modbus_config.quantity, raw_data);

  if (rc == -1) {
    std::cerr << "ERROR: Modbus read failed: " << modbus_strerror(errno)
              << std::endl;
    return false;
  }

  if (rc != modbus_config.quantity) {
    std::cerr << "WARNING: Expected " << modbus_config.quantity
              << " registers, got " << rc << std::endl;
    return false;
  }

  return true;
}

void cleanupModbus(ModbusContextPtr& ctx) {
  if (ctx) {
    try {
      // Close the connection before destroying
      modbus_close(ctx.get());
      std::cout << "INFO: Modbus connection closed" << std::endl;
    } catch (const std::exception& e) {
      std::cerr << "ERROR: Exception during modbus_close: " << e.what()
                << std::endl;
    } catch (...) {
      std::cerr << "ERROR: Unknown exception during modbus_close" << std::endl;
    }

    // Reset the smart pointer (calls modbus_free automatically)
    ctx.reset();
    std::cout << "INFO: Modbus context cleaned up" << std::endl;
  } else {
    std::cout << "INFO: Modbus context already null, no cleanup needed"
              << std::endl;
  }
}

/* ===================== ZMQ ======================= */
// khoi tao ZMQ Dealer va context
// tranh de xay ra leak memory thi de context va dealer ngoai ham
bool initZmqDealer(void*& context, void*& dealer,
                   const std::string& zmq_endpoint) {
  if (zmq_endpoint.empty()) {
    std::cerr << "ERROR: ZMQ endpoint is empty" << std::endl;
    return false;
  }

  context = zmq_ctx_new();
  if (!context) {
    std::cerr << "ERROR: Failed to create ZMQ context" << std::endl;
    return false;
  }

  dealer = zmq_socket(context, ZMQ_DEALER);
  if (!dealer) {
    std::cerr << "ERROR: Failed to create ZMQ dealer socket" << std::endl;
    zmq_ctx_destroy(context);
    context = nullptr;
    return false;
  }

  int rc = zmq_connect(dealer, zmq_endpoint.c_str());
  if (rc != 0) {
    std::cerr << "ERROR: ZMQ connect failed to " << zmq_endpoint << ": "
              << zmq_strerror(errno) << std::endl;
    cleanupZmq(context, dealer);
    return false;
  }

  std::cout << "INFO: ZMQ dealer connected to " << zmq_endpoint << std::endl;
  return true;
}

bool sendZmqMessage(void* dealer, const std::string& message) {
  if (!dealer) {
    std::cerr << "ERROR: ZMQ dealer socket is null" << std::endl;
    return false;
  }

  if (message.empty()) {
    std::cerr << "WARNING: Attempting to send empty message" << std::endl;
    return false;
  }

  int rc = zmq_send(dealer, message.c_str(), message.size(), 0);

  if (rc == -1) {
    std::cerr << "ERROR: ZMQ send failed: " << zmq_strerror(errno) << std::endl;
    return false;
  }

  if (rc != static_cast<int>(message.size())) {
    std::cerr << "WARNING: Partial send - expected " << message.size()
              << " bytes, sent " << rc << " bytes" << std::endl;
    return false;
  }

  return true;
}

void cleanupZmq(void* context, void* dealer) {
  if (dealer) {
    zmq_close(dealer);
    std::cout << "INFO: ZMQ dealer socket closed" << std::endl;
  }

  if (context) {
    zmq_ctx_destroy(context);
    std::cout << "INFO: ZMQ context destroyed" << std::endl;
  }
}

std::string createEnvelopeJson(const std::string& device_id,
                               const std::string& status,
                               const std::string& data_json) {
  cJSON* root = cJSON_CreateObject();
  if (!root) {
    std::cerr << "ERROR: Failed to create JSON root object" << std::endl;
    return "{}";
  }
  // Lấy Unix timestamp
  time_t unix_time = time(nullptr);

  if (!cJSON_AddStringToObject(root, "timelamp",
                               std::to_string(unix_time).c_str())) {
    std::cerr << "ERROR: Failed to add timelamp to JSON" << std::endl;
    cJSON_Delete(root);
    return "{}";
  }
  // Check return value của AddString
  if (!cJSON_AddStringToObject(root, "device_id", device_id.c_str())) {
    std::cerr << "ERROR: Failed to add device_id to JSON" << std::endl;
    cJSON_Delete(root);
    return "{}";
  }

  if (!cJSON_AddStringToObject(root, "status", status.c_str())) {
    std::cerr << "ERROR: Failed to add status to JSON" << std::endl;
    cJSON_Delete(root);
    return "{}";
  }

  cJSON* data = cJSON_Parse(data_json.c_str());
  if (!data) {
    std::cerr << "ERROR: Failed to parse data_json" << std::endl;
    cJSON_Delete(root);
    return R"({"error":"invalid data_json"})";
  }

  if (!cJSON_AddItemToObject(root, "data", data)) {
    std::cerr << "ERROR: Failed to add data to JSON" << std::endl;
    cJSON_Delete(data);  // ← Phải delete data nếu AddItem fail
    cJSON_Delete(root);
    return "{}";
  }

  char* json_string = cJSON_PrintUnformatted(root);
  if (!json_string) {
    std::cerr << "ERROR: Failed to serialize JSON" << std::endl;
    cJSON_Delete(root);
    return "{}";
  }

  std::string result(json_string);
  free(json_string);
  cJSON_Delete(root);

  return result;
}
