#include <errno.h>
#include <modbus.h>
#include <stdio.h>
#include <stdlib.h>

#define SERIAL_PORT "/dev/ttyS3"
#define BAUDRATE 9600  // Baudrate của Slave (thường 9600 hoặc 115200)
#define PARITY 'N'     // 'N' = None, 'E' = Even, 'O' = Odd
#define DATA_BITS 8
#define STOP_BITS 1

#define SLAVE_ID 1
#define START_ADDRESS 4000
#define NUM_REGISTERS 50

int main(void) {
  modbus_t* ctx;
  uint16_t tab_reg[NUM_REGISTERS];
  int rc, i;

  // 1. Khởi tạo Modbus RTU context
  ctx = modbus_new_rtu(SERIAL_PORT, BAUDRATE, PARITY, DATA_BITS, STOP_BITS);
  if (ctx == NULL) {
    fprintf(stderr, "Unable to create the libmodbus context: %s\n",
            modbus_strerror(errno));
    return -1;
  }

  // 2. Thiết lập Slave ID
  modbus_set_slave(ctx, SLAVE_ID);

  // 3. Kết nối RTU
  if (modbus_connect(ctx) == -1) {
    fprintf(stderr, "Connection failed: %s\n", modbus_strerror(errno));
    modbus_free(ctx);
    return -1;
  }

  // 4. Đọc Holding Registers (FC 03)
  rc = modbus_read_registers(ctx, START_ADDRESS, NUM_REGISTERS, tab_reg);
  if (rc == -1) {
    fprintf(stderr, "Read registers failed: %s\n", modbus_strerror(errno));
  } else {
    printf("Successfully read %d registers starting from address %d\n", rc,
           START_ADDRESS);

    for (i = 0; i < rc; i++) {
      printf("Reg[%d]: %hu\n", i, tab_reg[i]);
    }
  }

  // 5. Đóng kết nối
  modbus_close(ctx);
  modbus_free(ctx);

  return 0;
}
