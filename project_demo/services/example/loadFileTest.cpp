#include "../../drivers/meter_driver/include/meter_config.h"

// Giả lập hàm đọc cho mục đích test
int modbus_read_registers_mock(int start, int qty, uint16_t* buf) {
  for (int i = 0; i < qty; i++) buf[i] = i;
  return qty;
}

int main() {
  std::cout << "Start " << std::endl;
  MeterConfig config;
  std::string content = readFileToString("DPM380.json");
  // load và validate config
  if (config.loadFromJson(content) && config.validate()) {
    uint16_t raw_data[125];

    // Giả lập hoặc đọc Modbus thật
    modbus_read_registers_mock(config.modbus.start_address,
                               config.modbus.quantity, raw_data);

    // LẤY KẾT QUẢ CUỐI CÙNG CHỈ VỚI 1 DÒNG
    std::string final_output = config.parseToJson(raw_data);

    std::cout << "Output Data:\n" << final_output << std::endl;
  }

  return 0;
}