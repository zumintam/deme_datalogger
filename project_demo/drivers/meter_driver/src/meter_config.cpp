#include "../include/meter_config.h"

bool MeterConfig::loadFromJson(const std::string& json_content) {
  cJSON* root = cJSON_Parse(json_content.c_str());
  if (!root) return false;

  // Đọc device_model
  cJSON* model = cJSON_GetObjectItem(root, "device_model");
  if (cJSON_IsString(model)) this->device_model = model->valuestring;

  // Đọc slave_id
  cJSON* sid = cJSON_GetObjectItem(root, "slave_id");
  if (cJSON_IsNumber(sid)) this->slave_id = sid->valueint;

  // Đọc modbus_config
  cJSON* m_cfg = cJSON_GetObjectItem(root, "modbus_config");
  if (m_cfg) {
    this->modbus.start_address =
        cJSON_GetObjectItem(m_cfg, "start_address")->valueint;
    this->modbus.quantity = cJSON_GetObjectItem(m_cfg, "quantity")->valueint;
    this->modbus.function_code =
        cJSON_GetObjectItem(m_cfg, "function_code")->valueint;
    this->modbus.byte_order =
        cJSON_GetObjectItem(m_cfg, "byte_order")
            ? cJSON_GetObjectItem(m_cfg, "byte_order")->valuestring
            : "big_endian";
  }

  // Đọc mapping array
  cJSON* mapping_arr = cJSON_GetObjectItem(root, "mapping");
  if (cJSON_IsArray(mapping_arr)) {
    this->mappings.clear();
    int size = cJSON_GetArraySize(mapping_arr);
    for (int i = 0; i < size; i++) {
      cJSON* item = cJSON_GetArrayItem(mapping_arr, i);
      RegisterMapping reg;
      reg.name = cJSON_GetObjectItem(item, "name")->valuestring;
      reg.address = (uint16_t)cJSON_GetObjectItem(item, "address")->valueint;
      reg.type = cJSON_GetObjectItem(item, "type")->valuestring;
      this->mappings.push_back(reg);
    }
  }

  cJSON_Delete(root);
  return true;
}
// kiem tra tinh hop le cua cau hinh
// ID co nam trong khoang 1-247
// mapping khong duoc de trong
bool MeterConfig::validate() const {
  if (slave_id < 1 || slave_id > 247) return false;
  if (mappings.empty()) return false;
  return true;
}

/**
 * @brief Chuyển dữ liệu thanh ghi Modbus thô sang map giá trị.
 *
 * Đọc mảng thanh ghi 16-bit, giải mã theo cấu hình mapping (u16/u32/i32)
 * và lưu kết quả vào map với khóa là tên thanh ghi.
 *
 * @param raw_data   Con trỏ tới mảng dữ liệu thanh ghi Modbus.
 * @param output_map Map đầu ra (tên -> giá trị double).
 * @return true nếu xử lý thành công.
 */

bool MeterConfig::parseRaw2Map(
    const uint16_t* raw_data, std::map<std::string, double>& output_map) const {
  for (const auto& reg : mappings) {
    int idx = reg.address - modbus.start_address;
    if (idx < 0 || idx >= modbus.quantity) continue;
    double final_value = 0.0;

    if (reg.type == "u32" || reg.type == "i32") {
      if (idx + 1 < modbus.quantity) {
        uint32_t val = (uint32_t)((raw_data[idx] << 16) | raw_data[idx + 1]);
        final_value = static_cast<double>(val);
      }
    } else {
      final_value = static_cast<double>(raw_data[idx]);
    }
    output_map[reg.name] = final_value;
  }
  return true;
}

std::string MeterConfig::parse2Json(const std::uint16_t* raw_data) const {
  // 1. Giải mã dữ liệu thô vào Map thông qua hàm trung gian
  std::map<std::string, double> temp_map;
  this->parseRaw2Map(raw_data, temp_map);

  // 2. Khởi tạo đối tượng cJSON
  cJSON* root = cJSON_CreateObject();
  if (!root) return "{}";

  // Thêm thông tin định danh thiết bị vào JSON (nếu cần)
  cJSON_AddStringToObject(root, "device_model", this->device_model.c_str());

  // Duyệt Map để đưa các thông số đo lường vào JSON
  for (const auto& pair : temp_map) {
    cJSON_AddNumberToObject(root, pair.first.c_str(), pair.second);
  }

  // 3. Chuyển đổi sang chuỗi và dọn dẹp bộ nhớ
  char* rendered = cJSON_Print(root);  // Trả về chuỗi có định dạng (để debug)
  // Nếu muốn tiết kiệm băng thông khi gửi đi, dùng cJSON_PrintUnformatted(root)

  std::string json_result = rendered ? rendered : "{}";

  // Giải phóng vùng nhớ do cJSON cấp phát (Rất quan trọng)
  if (rendered) free(rendered);
  cJSON_Delete(root);

  return json_result;
}

std::string readFileToString(const std::string& filename) {
  std::ifstream ifs(filename);
  if (!ifs.is_open()) return "";
  std::stringstream ss;
  ss << ifs.rdbuf();
  return ss.str();
}