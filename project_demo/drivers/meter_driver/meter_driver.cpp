#include "meter_driver.h"

#include <chrono>
#include <iostream>
#include <thread>

extern int errno;  // Biến toàn cục lưu mã lỗi hệ thống
// --- KẾT THÚC MOCKING LIBMODBUS ---

/**
 * @brief Constructor: Khởi tạo đối tượng điều khiển thiết bị đo (Meter).
 * @param config Cấu hình thiết bị (cổng COM, baudrate, slave ID, danh sách
 * thanh ghi...).
 */
MeterDriver::MeterDriver(const MeterConfig& config) : config_(config) {
  if (!establishConnection()) {  // Khoi tao ket noi Modbus
    throw std::runtime_error(
        "Khoi tao Driver that bai. Khong the ket noi Modbus.");
  }
  std::cout << "[INFO] Driver " << config_.device_id << " đã sẵn sàng."
            << std::endl;
}

/**
 * @brief Thiết lập kết nối Modbus RTU qua cổng Serial.
 * @return true nếu kết nối thành công, false nếu thất bại.
 */
bool MeterDriver::establishConnection() {
  // 1. Tạo context (ngữ cảnh) Modbus RTU mới
  modbus_t* temp_ctx =
      modbus_new_rtu(config_.serial_port.c_str(), config_.baudrate, 'N', 8, 1);

  // Gán vào smart pointer (unique_ptr) để tự động quản lý bộ nhớ
  ctx_.reset(temp_ctx);

  // Kiểm tra xem việc tạo context có thành công không (đủ bộ nhớ không)
  if (!ctx_) {
    std::cerr << "[FAIL] Khong the tao Modbus context: "
              << modbus_strerror(errno) << std::endl;
    return false;
  }

  // 2. Thiết lập Slave ID (Địa chỉ trạm của thiết bị đo)
  // Nếu thiết bị không đúng Slave ID này, nó sẽ không phản hồi.
  if (modbus_set_slave(ctx_.get(), config_.slave_id) == -1) {
    std::cerr << "[FAIL] Khong the thiet lap Slave ID " << config_.slave_id
              << ": " << modbus_strerror(errno) << std::endl;
    ctx_.reset(nullptr);  // Hủy context nếu lỗi
    return false;
  }

  // 3. Mở kết nối vật lý tới cổng Serial
  if (modbus_connect(ctx_.get()) == -1) {
    std::cerr << "[FAIL] Khong the ket noi Modbus toi " << config_.serial_port
              << ": " << modbus_strerror(errno) << std::endl;
    ctx_.reset(nullptr);  // Hủy context nếu không mở được cổng
    return false;
  }

  // Kết nối thành công
  return true;
}

/**
 * @brief Chuyển đổi địa chỉ Modbus từ chuẩn tài liệu sang địa chỉ vật lý.
 * @param register_address Địa chỉ trong cấu hình.
 * @return Địa chỉ Offset thực tế để thư viện libmodbus sử dụng.
 */
std::uint16_t MeterDriver::getModbusAddress(
    std::uint16_t register_address) const {
  return register_address;
}

/**
 * @brief Đọc giá trị từ một thanh ghi cụ thể và nhân với hệ số tỉ lệ (Scale).
 * Hàm này bao gồm cơ chế Retry (thử lại) và Thread-safety (an toàn luồng).
 * @param reg Cấu hình của thanh ghi cần đọc.
 * @return Giá trị thực tế (double) hoặc -999.0 nếu đọc lỗi.
 */
double MeterDriver::readAndScaleRegister(const RegisterConfig& reg) {
  // Kiểm tra xem context đã được khởi tạo chưa
  if (!ctx_) return -999.0;

  // --- BẮT ĐẦU VÙNG TỚI HẠN (CRITICAL SECTION) ---
  // Sử dụng std::lock_guard để tự động khóa mutex khi vào hàm và mở khóa khi
  // thoát hàm. Điều này ngăn chặn 2 luồng cùng truy cập cổng Serial một lúc gây
  // lỗi dữ liệu.
  std::lock_guard<std::mutex> lock(modbus_lock_);

  const int MAX_RETRIES = 3;  // Số lần thử lại tối đa nếu đọc lỗi
  const int num_registers = 1;
  std::uint16_t raw_data[num_registers];

  // Tính toán địa chỉ thực tế (Offset)
  std::uint16_t modbus_addr = getModbusAddress(reg.address);

  for (int retry = 0; retry < MAX_RETRIES; ++retry) {
    int num_read =
        modbus_read_registers(ctx_.get(), modbus_addr, num_registers, raw_data);
    std::cout << "[INFO] Ham doc (Holding): " << num_read << " registers"
              << std::endl;

    // Kiểm tra kết quả đọc
    if (num_read == num_registers) {
      std::uint16_t raw_value = raw_data[0];
      double real_value = (double)raw_value * reg.scale;
      std::cout << "[INFO] Thanh ghi Holding " << reg.address
                << " | Raw: " << raw_value << " | Scale: " << reg.scale
                << " | Ket qua: " << real_value << std::endl;

      return real_value;
    }

    // Nếu đọc thất bại, in cảnh báo và đợi một chút trước khi thử lại
    std::cerr << "[WARN] Doc thanh ghi " << reg.address << " that bai (Thu #"
              << retry + 1 << ")..." << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return -999.0;
}

/**
 * @brief Đọc toàn bộ danh sách thanh ghi được cấu hình.
 * @return MeterData (Map chứa tên thanh ghi và giá trị đã scale).
 */
MeterData MeterDriver::readAllAndScaleData() {
  MeterData results;
  std::cout << "\n--- BAT DAU DOC VA SCALE DU LIEU (" << config_.device_id
            << ") ---" << std::endl;

  // Duyệt qua từng cấu hình thanh ghi trong map config_.registers
  for (const auto& pair : config_.registers) {
    const RegisterConfig& reg = pair.second;

    // Gọi hàm đọc cho từng thanh ghi
    double value = readAndScaleRegister(reg);

    // Chỉ lưu các giá trị hợp lệ (lớn hơn -999.0)
    if (value > -999.0) {
      results[reg.name] = value;
      std::cout << "[READ SCALED] " << reg.name << ": " << value << std::endl;
    }
  }
  return results;
}