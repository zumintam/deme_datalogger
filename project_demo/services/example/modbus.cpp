#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#include "meter_driver.h"
#include "zmq.h"

using namespace std;

/* ================== BIẾN DÙNG CHUNG ================== */
mutex driver_mutex;
atomic<bool> running(true);

/* ================== LUỒNG POLLING ================== */
void pollingThread(MeterDriver* meter, void* publisher, int poll_interval_ms) {
  int cycle = 0;

  while (running) {
    cycle++;

    MeterData data;
    {
      lock_guard<mutex> lock(driver_mutex);
      data = meter->readAllAndScaleData();
      // data = meter->readRawData();
    }

    stringstream ss;
    ss << "{ \"cycle\": " << cycle << ", \"data\": { ";

    for (auto it = data.begin(); it != data.end(); ++it) {
      ss << "\"" << it->first << "\":" << it->second;
      if (next(it) != data.end()) ss << ", ";
    }
    ss << " } }";

    string msg = ss.str();
    zmq_send(publisher, msg.c_str(), msg.size(), 0);

    cout << "[POLLING] " << msg << endl;

    this_thread::sleep_for(chrono::milliseconds(poll_interval_ms));
  }

  cout << "[POLLING] Thread stopped\n";
}

/* ================== LUỒNG ĐIỀU KHIỂN ================== */
void controlThread(MeterDriver* driver, void* context) {
  void* subscriber = zmq_socket(context, ZMQ_SUB);
  zmq_connect(subscriber, "tcp://localhost:5556");
  zmq_setsockopt(subscriber, ZMQ_SUBSCRIBE, "", 0);

  char buffer[256];

  while (running) {
    int size = zmq_recv(subscriber, buffer, sizeof(buffer) - 1, 0);
    if (size <= 0) continue;

    buffer[size] = '\0';
    string cmd(buffer);

    cout << "[CONTROL] Received: " << cmd << endl;

    if (cmd == "STOP") {
      running = false;
      cout << "[CONTROL] Stop system\n";
    }
  }

  zmq_close(subscriber);
}

/* ================== MAIN ================== */
int main() {
  const string CONFIG_FILE = "meter_config.json";

  /* Load config */
  MeterConfig config;
  if (!config.loadFromJson(CONFIG_FILE)) {
    cerr << "[FATAL] Cannot load config\n";
    return 1;
  }

  /* ZMQ context */
  void* context = zmq_ctx_new();

  /* Publisher socket */
  void* publisher = zmq_socket(context, ZMQ_PUB);
  if (zmq_bind(publisher, "tcp://*:5555") != 0) {
    cerr << "[FATAL] ZMQ bind failed\n";
    return 1;
  }

  cout << "[SYSTEM] ZMQ PUB at port 5555\n";

  /* Meter driver */
  unique_ptr<MeterDriver> driver(new MeterDriver(config));
  Vu
      /* Start threads */
      thread t_poll(pollingThread, driver.get(), publisher,
                    config.poll_interval_ms);

  thread t_ctrl(controlThread, driver.get(), context);

  /* Wait threads */
  t_poll.join();
  t_ctrl.join();

  /* Cleanup */
  zmq_close(publisher);
  zmq_ctx_destroy(context);

  cout << "[SYSTEM] Exit\n";
  return 0;
}
