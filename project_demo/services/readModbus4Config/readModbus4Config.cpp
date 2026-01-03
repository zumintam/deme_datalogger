#include <modbus/modbus.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zmq.h>

#include <chrono>  // Để lấy timestamp
#include <iomanip>
#include <iostream>

// Nếu bạn chưa dùng thư viện JSON bên thứ 3, ta có thể tạo chuỗi JSON thủ công
// hoặc dùng nlohmann/json. Ở đây tôi dùng chuỗi thủ công đơn giản dựa trên kết
// quả của bạn.
#include "../../drivers/meter_driver/include/meter_config.h"

// Hàm lấy Epoch Time tính bằng mili giây
long long get_timestamp_ms() {
  auto now = std::chrono::system_clock::now();
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             now.time_since_epoch())
      .count();
}

int main() {
  std::cout << "--- SolarBK Meter Reader: Modbus + ZMQ IPC ---" << std::endl;

  // 1. Khởi tạo ZMQ Dealer
  void* context = zmq_ctx_new();
  void* dealer = zmq_socket(context, ZMQ_DEALER);
  const char* id = "MODBUS_WORKER_C";
  zmq_setsockopt(dealer, ZMQ_IDENTITY, id, strlen(id));

  if (zmq_connect(dealer, "ipc:///tmp/modbus_pipeline.ipc") != 0) {
    perror("ZMQ Connect failed");
    return -1;
  }

  // 2. Tải cấu hình từ file JSON
  MeterConfig config;
  std::string content = readFileToString("DPM380.json");
  if (!config.loadFromJson(content) || !config.validate()) {
    std::cerr << "Lỗi: File cấu hình không hợp lệ!" << std::endl;
    return -1;
  }

  // 3. Khởi tạo Modbus RTU
  modbus_t* ctx = modbus_new_rtu("/dev/ttyS3", 9600, 'N', 8, 1);
  if (ctx == nullptr) return -1;
  modbus_set_slave(ctx, config.slave_id);
  modbus_set_response_timeout(ctx, 1, 0);

  if (modbus_connect(ctx) == -1) {
    std::cerr << "Kết nối RS485 thất bại!" << std::endl;
    modbus_free(ctx);
    return -1;
  }

  uint16_t raw_data[125];

  // Vòng lặp quét dữ liệu (Vì Datalogger cần chạy liên tục)
  while (true) {
    int rc = -1;
    if (config.modbus.function_code == 4) {
      rc = modbus_read_input_registers(ctx, config.modbus.start_address,
                                       config.modbus.quantity, raw_data);
    } else {
      rc = modbus_read_registers(ctx, config.modbus.start_address,
                                 config.modbus.quantity, raw_data);
    }

    // 4. Đóng gói Envelope (Dữ liệu + Status + Timestamp)
    std::string status = (rc == config.modbus.quantity) ? "SUCCESS" : "ERROR";
    std::string data_json =
        (rc == config.modbus.quantity) ? config.parse2Json(raw_data) : "{}";
    long long ts = get_timestamp_ms();

    // Tạo gói tin JSON tổng hợp
    // Lưu ý: data_json bản thân nó đã là 1 string JSON nên không để trong ngoặc
    // kép
    std::string envelope = "{";
    envelope += "\"timestamp\":" + std::to_string(ts) + ",";
    envelope += "\"status\":\"" + status + "\",";
    envelope += "\"device_id\":\"" + config.device_model + "\",";
    envelope += "\"data\":" + data_json;
    envelope += "}";

    // 5. Gửi qua ZMQ Dealer (Mô hình 2 frame: Empty + Payload)
    zmq_send(dealer, NULL, 0, ZMQ_SNDMORE);  // Frame 1: Empty định danh
    zmq_send(dealer, envelope.c_str(), envelope.length(),
             0);  // Frame 2: Dữ liệu

    if (rc == config.modbus.quantity) {
      std::cout << "[OK] Sent data at " << ts << std::endl;
    } else {
      std::cerr << "[ERR] Modbus read failed, sent error status." << std::endl;
    }

    // Chu kỳ quét (Ví dụ: 5 giây)
    sleep(5);
  }

  // 6. Giải phóng tài nguyên (Thực tế vòng lặp vô hạn ít khi xuống đây)
  zmq_close(dealer);
  zmq_ctx_destroy(context);
  modbus_close(ctx);
  modbus_free(ctx);

  return 0;
}