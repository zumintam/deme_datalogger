#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <memory>  // Cho std::unique_ptr
#include <thread>

#include "../../drivers/meter_driver/meter_config.h"
#include "../../drivers/meter_driver/meter_driver.h"

using namespace std;

int main() {
  const string CONFIG_FILE = "meter_config.json";

  MeterConfig config;
  cout << "--- 1. Bat dau Tai & Xac thuc Cau hinh ---" << endl;

  if (!config.loadFromJson(CONFIG_FILE)) {
    cerr << "[FATAL] Khong the tai cau hinh. Chuong trinh ket thuc." << endl;
    return 1;
  }

  if (!config.validate()) {
    cerr << "[FATAL] Cau hinh khong hop le. Chuong trinh ket thuc." << endl;
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

      cout << "[RESULT FINAL] Du lieu thiet bi: ";
      for (const auto& pair : data) {
        cout << pair.first << "=" << pair.second << " | ";
      }
      cout << endl;

      this_thread::sleep_for(chrono::milliseconds(config.poll_interval_ms));
    }

  } catch (const runtime_error& e) {
    cerr << "\n[FATAL] Loi he thong khi khoi tao hoac ket noi: " << e.what()
         << endl;
    return 1;
  }

  cout << "\n--- 4. Ket thuc Chuong trinh ---" << endl;
  return 0;
}