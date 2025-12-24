#include <iomanip>  // Để trình bày số liệu cho đẹp
#include <iostream>

#include "mb_master.h"

int main(int argc, char* argv[]) {
  std::cout << "--- SolarBK Modbus Master Test ---" << std::endl;

  // 1. Khởi tạo context modbus
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
  ModbusMaster master;
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