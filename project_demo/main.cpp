#include <modbus.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

using namespace std;

// Địa chỉ cổng nối tiếp và tốc độ baud cần thay đổi theo hệ thống của bạn
#define MODBUS_DEVICE "/dev/ttyS3"
#define MODBUS_BAUDRATE 9600

int main(int argc, char* argv[]) {
  // Sử dụng C++ I/O cho thông báo
  cout << "--- SolarBK Modbus RTU Client (C++) ---" << endl;

  // Khởi tạo context modbus RTU
  modbus_t* ctx = modbus_new_rtu(MODBUS_DEVICE, MODBUS_BAUDRATE, 'N', 8, 1);

  if (ctx == nullptr) {
    cerr << "Lỗi: Không thể tạo Modbus Context: " << strerror(errno) << endl;
    return -1;
  }

  // Thiết lập Slave ID (ví dụ: ID 1)
  if (modbus_set_slave(ctx, 1) == -1) {
    cerr << "Lỗi: Không thể thiết lập Slave ID: " << modbus_strerror(errno)
         << endl;
    modbus_free(ctx);
    return -1;
  }

  // Thiết lập timeout (ví dụ: 500ms)
  modbus_set_response_timeout(ctx, 0, 500000);

  // Mở kết nối
  if (modbus_connect(ctx) == -1) {
    cerr << "Lỗi: Không thể kết nối Modbus RTU: " << modbus_strerror(errno)
         << endl;
    modbus_free(ctx);
    return -1;
  }

  cout << "Kết nối Modbus RTU thành công: " << MODBUS_DEVICE << " @ "
       << MODBUS_BAUDRATE << endl;

  // --- Ví dụ: Đọc 10 Input Registers (Function Code 04) ---
  uint16_t tab_reg[10];
  int start_address = 30000;
  int num_registers = 10;

  // Đọc Holding Registers (Function Code 03) cũng tương tự:
  // int rc = modbus_read_registers(ctx, start_address, num_registers, tab_reg);

  // Thực hiện lệnh đọc Input Registers
  int rc =
      modbus_read_input_registers(ctx, start_address, num_registers, tab_reg);

  if (rc == -1) {
    cerr << "Lỗi khi đọc Input Registers: " << modbus_strerror(errno) << endl;
  } else {
    cout << "Đã đọc thành công " << rc << " registers từ địa chỉ "
         << start_address << "." << endl;
    cout << "Dữ liệu đọc được:" << endl;
    for (int i = 0; i < rc; i++) {
      // Sử dụng C++ stream để in dữ liệu
      cout << "  Reg[" << i << "] (Addr " << start_address + i << "): 0x" << hex
           << tab_reg[i] << dec << " (" << tab_reg[i] << ")" << endl;
    }
  }

  // Đóng kết nối và giải phóng bộ nhớ
  modbus_close(ctx);
  modbus_free(ctx);

  cout << "Đã hoàn thành và ngắt kết nối." << endl;

  return 0;
}