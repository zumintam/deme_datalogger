#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#include "meter_driver.h"
#include "zmq.h"

using namespace std;

int main() {
  const string CONFIG_FILE = "meter_config.json";

  // 1. Khởi tạo ZMQ Context và Socket (Kiểm tra thư viện .a)
  cout << "--- 0. Khoi tao ZeroMQ ---" << endl;
  void* context = zmq_ctx_new();
  void* publisher = zmq_socket(context, ZMQ_PUB);

  // Bind vào port 5555 để các ứng dụng khác có thể subcribe dữ liệu meter
  int rc = zmq_bind(publisher, "tcp://*:5555");
  if (rc != 0) {
    cerr << "[ERROR] ZMQ bind that bai!" << endl;
    return 1;
  }
  cout << "ZMQ Publisher hop le, dang cho tai cau hinh..." << endl;

  MeterConfig config;
  cout << "--- 1. Bat dau Tai & Xac thuc Cau hinh ---" << endl;

  if (!config.loadFromJson(CONFIG_FILE)) {
    cerr << "[FATAL] Khong the tai cau hinh. Chuong trinh ket thuc." << endl;
    return 1;
  }

  try {
    // 2. Khởi tạo Driver
    cout << "\n--- 2. Khoi tao Meter Driver va Ket noi Modbus ---" << endl;
    unique_ptr<MeterDriver> driver(new MeterDriver(config));

    // 3. Vòng lặp đọc dữ liệu (Polling Loop)
    cout << "\n--- 3. Bat dau Vong lap Doc du lieu (Polling) ---" << endl;

    for (int i = 0; i < 100; ++i) {
      cout << "\n[POLLING] Chu ky doc #" << i + 1 << endl;

      MeterData data = driver->readAllAndScaleData();

      // Tạo chuỗi message để gửi qua ZMQ
      stringstream ss;
      ss << "{ \"cycle\": " << i + 1 << ", \"data\": { ";

      for (auto it = data.begin(); it != data.end(); ++it) {
        ss << "\"" << it->first << "\":" << it->second;
        if (next(it) != data.end()) ss << ", ";
      }
      ss << " } }";

      string msg = ss.str();

      // Gửi dữ liệu qua ZMQ
      zmq_send(publisher, msg.c_str(), msg.length(), 0);
      cout << "[ZMQ SENT] " << msg << endl;

      this_thread::sleep_for(chrono::milliseconds(config.poll_interval_ms));
    }

  } catch (const runtime_error& e) {
    cerr << "\n[FATAL] Loi he thong: " << e.what() << endl;
  }

  // Cleanup ZMQ
  zmq_close(publisher);
  zmq_ctx_destroy(context);

  cout << "\n--- 4. Ket thuc Chuong trinh ---" << endl;
  return 0;
}