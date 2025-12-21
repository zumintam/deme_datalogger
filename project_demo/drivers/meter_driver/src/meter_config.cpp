#include "meter_config.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

/**
 * @brief Đọc toàn bộ nội dung file vào một chuỗi string
 * @param filename Đường dẫn tệp cần đọc
 * @return Chuỗi chứa nội dung file, hoặc chuỗi rỗng nếu file không tồn tại
 */
std::string readFileToString(const std::string& filename) {
  std::ifstream fileStream(filename);
  if (!fileStream.is_open()) {
    return "";
  }
  std::stringstream buffer;
  buffer << fileStream.rdbuf();
  return buffer.str();
}

/**
 * @brief Tải cấu hình thiết bị đồng hồ từ file JSON
 * @param filename Đường dẫn tệp JSON chứa cấu hình
 * @return true nếu tải cấu hình thành công, false nếu có lỗi
 *
 * Hàm này:
 * - Đọc file JSON từ đường dẫn được cấp
 * - Phân tích các trường cấu hình: device_id, serial_port, baudrate, slave_id,
 * poll_interval_ms
 * - Xử lý danh sách các register với địa chỉ và hệ số scale
 */
bool MeterConfig::loadFromJson(const std::string& filename) {
  std::string json_content = readFileToString(filename);
  if (json_content.empty()) {  // tra ve "" neu khong doc duoc file
    std::cerr << "ERROR: Khong the doc file hoac file rong: " << filename
              << std::endl;
    return false;  // return false if file cannot be read
  }

  // Phân tích chuỗi JSON thành cấu trúc dữ liệu
  cJSON* root =
      cJSON_Parse(json_content.c_str());  // Parse JSON string tra ve cho root
  if (root == nullptr) {
    const char* error_ptr = cJSON_GetErrorPtr();
    if (error_ptr != nullptr) {
      std::cerr << "ERROR: Loi phan tich JSON truoc: " << error_ptr
                << std::endl;
    }
    return false;
  }

  bool success = true;
  try {
    // Trích xuất ID thiết bị tu root JSON
    cJSON* id = cJSON_GetObjectItemCaseSensitive(root, "device_id");
    if (cJSON_IsString(id))
      device_id =
          id->valuestring;  // neu dung la tra ve String thi ID = gia tri do

    // Trích xuất cổng serial
    cJSON* port = cJSON_GetObjectItemCaseSensitive(root, "serial_port");
    if (cJSON_IsString(port)) serial_port = port->valuestring;

    // Trích xuất tốc độ baud
    cJSON* br = cJSON_GetObjectItemCaseSensitive(root, "baudrate");
    if (cJSON_IsNumber(br)) baudrate = br->valueint;

    // Trích xuất ID slave Modbus
    cJSON* sid = cJSON_GetObjectItemCaseSensitive(root, "slave_id");
    if (cJSON_IsNumber(sid)) slave_id = sid->valueint;

    // Trích xuất khoảng thời gian polling (ms)
    cJSON* poll = cJSON_GetObjectItemCaseSensitive(root, "poll_interval_ms");
    if (cJSON_IsNumber(poll)) poll_interval_ms = poll->valueint;

    // Trích xuất danh sách các register
    cJSON* json_registers = cJSON_GetObjectItemCaseSensitive(root, "registers");

    // lay danh sach register neu la object
    if (cJSON_IsObject(
            json_registers)) {  // gia tri tra ve cua registers phai la object
      cJSON* register_item =
          json_registers->child;  // tro toi cac truong con trong doi tuong

      // Duyệt qua tất cả các register trong JSON
      while (register_item != nullptr) {
        RegisterConfig
            reg;  // lay cua truc struct de luu tru cau hinh tung register
        reg.name = register_item->string;
        cJSON* register_info = register_item;

        // Lấy địa chỉ register
        cJSON* address =
            cJSON_GetObjectItemCaseSensitive(register_info, "address");
        if (cJSON_IsNumber(address))
          reg.address = (std::uint16_t)address->valueint;

        // Lấy hệ số scale để chuyển đổi giá trị
        cJSON* scale = cJSON_GetObjectItemCaseSensitive(register_info, "scale");
        if (cJSON_IsNumber(scale)) reg.scale = scale->valuedouble;

        // Lưu cấu hình register vào bản đồ
        registers[reg.name] = reg;
        register_item = register_item->next;
      }
    }
  } catch (...) {
    std::cerr << "ERROR: Loi truy cap du lieu JSON voi cJSON." << std::endl;
    success = false;
  }

  cJSON_Delete(root);

  if (success) {
    std::cout << "[INFO] Tai cau hinh thanh cong cho: " << device_id
              << std::endl;
  }
  return success;
}