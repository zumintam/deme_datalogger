#include <chrono>
#include <iostream>
#include <thread>

#include "modbus_ex.h"

int main() {
  ModbusMaster mb;
  Config config;
  ModbusContextPtr ctx;
  ZmqDealer zmq_dealer;

  try {
    // Initialize Modbus configuration
    if (!initModbusConfig(config, MODBUS_PORT_S3, ctx, mb, "DPM380.json")) {
      std::cerr << "Failed to initialize Modbus configuration" << std::endl;
      return EXIT_FAILURE;
    }

    // Initialize ZMQ dealer
    if (!initZmqDealer(zmq_dealer.ctx, zmq_dealer.dealer, ZMQ_ENDPOINT)) {
      std::cerr << "Failed to initialize ZMQ dealer" << std::endl;
      cleanupModbus(ctx);
      return EXIT_FAILURE;
    }=

    std::cout << "Modbus and ZMQ initialized successfully" << std::endl;

    // Main loop for continuous data reading
    uint16_t raw_data[256] = {0};
    int consecutive_errors = 0;
    const int MAX_CONSECUTIVE_ERRORS = 5;

    while (consecutive_errors < MAX_CONSECUTIVE_ERRORS) {
      // Clear raw data buffer
      memset(raw_data, 0, sizeof(raw_data));

      // Read Modbus data   !!!! READ FAIL HERE
      bool read_success =
          readModbusData(ctx.get(), config.meter.modbus, raw_data, mb);

      if (!read_success) {
        std::cerr << "Failed to read Modbus data" << std::endl;
        consecutive_errors++;
        std::this_thread::sleep_for(std::chrono::seconds(1));
        continue;
      }

      // Reset error counter on success
      consecutive_errors = 0;

      // Parse data to JSON
      std::string json_data = config.meter.parse2Json(raw_data);

      // Create envelope JSON
      std::string envelope = createEnvelopeJson("device_1", "OK", json_data);
      std::cout << "JSON:\n" << envelope << std::endl;
      // Send data via ZMQ
      bool send_success = sendZmqMessage(zmq_dealer.dealer, envelope);

      if (!send_success) {
        std::cerr << "Failed to send ZMQ message" << std::endl;
      } else {
        std::cout << "Data sent successfully" << std::endl;
      }

      std::this_thread::sleep_for(std::chrono::seconds(1));
      std::cout << "----------------------------------------" << std::endl;
    }

    std::cerr << "Too many consecutive errors, shutting down" << std::endl;

  } catch (const std::exception& e) {
    std::cerr << "Exception occurred: " << e.what() << std::endl;
    cleanupModbus(ctx);
    cleanupZmq(zmq_dealer.ctx, zmq_dealer.dealer);
    return EXIT_FAILURE;
  }

  // Cleanup resources
  cleanupModbus(ctx);
  cleanupZmq(zmq_dealer.ctx, zmq_dealer.dealer);

  std::cout << "Program terminated successfully" << std::endl;
  return EXIT_SUCCESS;
}