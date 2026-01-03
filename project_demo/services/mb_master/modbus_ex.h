#pragma once

#include <sys/time.h>
#include <time.h>

#include <cstdint>
#include <string>

// ================= Third-party =================
#include <zmq.h>

// ================= Forward declarations =================
struct modbus_ex_t;

// ================= Project headers =================
#include "../../drivers/meter_driver/include/meter_config.h"
#include "mb_master.h"
#include "modbus_config.h"

// ================= Constants =================
#define ZMQ_ENDPOINT "ipc:///tmp/modbus_pipeline.ipc"
#define MODBUS_PORT_S3 1

// ================= Config =================

/**
 * @brief Application configuration structure
 */
struct Config {
  MeterConfig meter;
};

struct ZmqDealer {
  void* ctx = nullptr;
  void* dealer = nullptr;

  ~ZmqDealer() {
    if (dealer) zmq_close(dealer);
    if (ctx) zmq_ctx_destroy(ctx);
  }
};

// ================= Modbus =================

/**
 * @brief Initialize Modbus configuration and connect RTU
 *
 * @param config        Parsed configuration
 * @param port          Serial port index
 * @param ctx           Modbus context smart pointer
 * @param mb            Modbus master instance
 * @param config_file   JSON config file path
 * @return true on success
 */
bool initModbusConfig(Config& config, int port, ModbusContextPtr& ctx,
                      ModbusMaster& mb,
                      const std::string& config_file = "DPM380.json");

/**
 * @brief Read Modbus input registers
 *
 * @param ctx            Modbus context
 * @param modbus_config  Register configuration
 * @param raw_data       Output buffer
 * @param mb             Modbus master instance
 * @return true on success
 */
bool readModbusData(modbus_t* ctx, const ModbusConfig& modbus_config,
                    uint16_t* raw_data, ModbusMaster& mb);

/**
 * @brief Cleanup Modbus context
 */
void cleanupModbus(ModbusContextPtr& ctx);

// ================= ZMQ =================

/**
 * @brief Initialize ZMQ DEALER socket
 */
bool initZmqDealer(void*& context, void*& dealer,
                   const std::string& zmq_endpoint);

/**
 * @brief Send message via ZMQ
 */
bool sendZmqMessage(void* dealer, const std::string& message);

/**
 * @brief Cleanup ZMQ resources
 */
void cleanupZmq(void* context, void* dealer);

// ================= JSON =================

/**
 * @brief Create JSON envelope
 */
std::string createEnvelopeJson(const std::string& device_id,
                               const std::string& status,
                               const std::string& data_json);
