#pragma once
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <zmq.hpp>  

// Bao gom cac module driver/config
#include "meter_driver/meter_config.h"
#include "meter_driver/meter_driver.h"

class SystemManager {
 public:
  // Constructor: Khoi tao cac module va ZMQ sockets
  SystemManager(const MeterConfig& config);

  // Destructor: Dam bao tat an toan cac luong
  ~SystemManager();

  // Khoi dong tat ca cac luong tac vu
  void start();

  // Yeu cau tat an toan (Graceful Shutdown)
  void stop();

 private:
  const MeterConfig& m_config;
  std::atomic<bool> m_is_running{false};

  // Cac luong con
  std::thread m_polling_thread;
  std::thread m_communication_thread;

  // --- ZERO MQ COMPONENTS ---
  zmq::context_t m_zmq_context{1};

  std::unique_ptr<zmq::socket_t> m_push_socket;

  std::unique_ptr<zmq::socket_t> m_pull_socket;

  // Dia chi giao tiep noi bo
  const std::string DATA_PIPE_ADDRESS = "inproc://meter_data_pipe";

  // --- DRIVER/MODULES ---
  std::unique_ptr<MeterDriver> m_meter_driver;
  // TODO: Khai bao cac module khac nhu MQTTClient

  // Cac chuc nang luong (Tasks)
  void pollingTask();        // Tac vu: Doc Modbus va PUSH du lieu
  void communicationTask();  // Tac vu: PULL du lieu va xu ly/gui di

  // Cac ham tien ich ZeroMQ (Can co thu vien JSON de chuyen doi MeterData)
  std::string serializeMeterData(const MeterData& data);
  MeterData deserializeMeterData(const std::string& str);
};
