#include <zmq.h>

#include <iomanip>  // Để trình bày số liệu cho đẹp
#include <iostream>

#include "mb_master.h"
#include "modbus_config.h"
// output: raw data from Modbus slave device in json format

int main(int argc, char* argv[]) {
  ModbusMaster master;

  std::cout << "--- SolarBK Modbus Master Test ---" << std::endl;

  // 1. Khởi tạo context modbus RTU
  modbus_t* ctx = modbus_new_rtu("/dev/ttyS3", 9600, 'N', 8, 1);
  if (ctx == nullptr) {
    std::cerr << "Không thể tạo context Modbus" << std::endl;
    return -1;
  }

  // 2. Thiết lập Slave ID
  modbus_set_slave(ctx, 1);

  // 3. Kết nối
  if (modbus_connect(ctx) == -1) {
    std::cerr << "Kết nối thất bại: " << modbus_strerror(errno) << std::endl;
    modbus_free(ctx);
    return -1;
  }

  // 4. Đọc dữ liệu

  uint16_t addr = 4000;
  uint16_t qty = 80;
  uint16_t data[100];

  int rc = master.readHoldingRegisters(ctx, addr, qty, data);

  if (rc == -1) {
    std::cerr << "Lỗi đọc thanh ghi: " << modbus_strerror(errno) << std::endl;
  } else {
    std::cout << "Đọc thành công " << rc << " thanh ghi!" << std::endl;

    // 5. In giá trị đọc được ra màn hình
    for (int i = 0; i < rc; i++) {
      std::cout << "Register [" << addr + i << "]: " << data[i] << " (0x"
                << std::hex << std::setw(4) << std::setfill('0') << data[i]
                << std::dec << ")" << std::endl;
    }
  }

  // 6. Đóng kết nối
  modbus_close(ctx);
  modbus_free(ctx);

  return 0;
}

// modbus_load_config -- trong demo nay se su dung config mac dinh cua
// MeterConfig
#include "../../drivers/meter_driver/include/meter_config.h"  // lay config
bool loadConfig(const std::string& filename, MeterConfig& config) {
  std::string content = readFileToString(filename);
  if (!config.loadFromJson(content) || !config.validate()) {
    std::cerr << "Lỗi: File cấu hình không hợp lệ!" << std::endl;
    return false;
  }
  return true;
}

// modbus_init
bool initModbusRTU(modbus_t*& ctx, const std::string& device, int slave_id) {
  ctx = modbus_new_rtu(device.c_str(), 9600, 'N', 8, 1);
  if (ctx == nullptr) {
    std::cerr << "Không thể tạo context Modbus" << std::endl;
    return false;
  }
  modbus_set_slave(ctx, slave_id);
  modbus_set_response_timeout(ctx, 1, 0);

  if (modbus_connect(ctx) == -1) {
    std::cerr << "Kết nối RS485 thất bại!" << std::endl;
    modbus_free(ctx);
    return false;
  }
  return true;
}
// read_modbus_data
bool readModbusData(modbus_t* ctx, const ModbusConfig& modbus_config,
                    uint16_t* raw_data) {
  int rc = -1;
  if (modbus_config.function_code == 4) {  // Read Input Registers
    rc = modbus_read_input_registers(ctx, modbus_config.start_address,
                                     modbus_config.quantity, raw_data);
  } else {  // Read Holding Registers
    rc = modbus_read_registers(ctx, modbus_config.start_address,
                               modbus_config.quantity, raw_data);
  }
  return (rc == modbus_config.quantity);
}
/* ===================== ZMQ ======================= */
// init_zmq_deadler
// zmq_endpoint: "ipc:///tmp/modbus_pipeline.ipc" de ket noi local IPC
bool initZmqDealer(void*& dealer, const std::string& zmq_endpoint) {
  void* context = zmq_ctx_new();
  dealer = zmq_socket(context, ZMQ_DEALER);
  int rc = zmq_connect(dealer, zmq_endpoint.c_str());
  if (rc != 0) {
    std::cerr << "Kết nối ZMQ Dealer thất bại!" << std::endl;
    return false;
  }
  return true;
  initZmqDealer
}
// cjsonstruct_envelope [MODBUS] create_envelope_json
std::string createEnvelopeJson(const std::string& device_id,
                               const std::string& status,
                               const std::string& data_json) {
  cJSON* root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "device_id", device_id.c_str());
  cJSON_AddStringToObject(root, "status", status.c_str());
  cJSON_AddStringToObject(root, "data", data_json.c_str());
  char* json_string = cJSON_Print(root);
  std::string result(json_string);
  free(json_string);
  cJSON_Delete(root);
  return result;
}

// send_zmq_message
bool sendZmqMessage(void* dealer, const std::string& message) {
  int rc = zmq_send(dealer, message.c_str(), message.size(), 0);
  return (rc != -1);
}

void cleanupZmq(void* dealer, modbus_t*& ctx) {
  if (ctx) {
    modbus_close(ctx);
    modbus_free(ctx);
    ctx = nullptr;
  }
  zmq_close(dealer);
  zmq_ctx_destroy(zmq_ctx_new());
}

/* ============================Doc ==========================
  - SolarBK Modbus Master Test
  - Mục đích: Minh họa cách sử dụng ModbusMaster class để đọc dữ liệu từ
  thiết bị Modbus RTU qua cổng serial.
  - Chức năng:
    + Tạo context Modbus RTU
    + Kết nối đến thiết bị slave với Slave ID xác định
    + Đọc một loạt thanh ghi giữ (holding registers)
    + In giá trị đọc được
*/
// modbus API
// modbus read_data
// modbus write_data
// modbus zmq
// modbus thread safe
