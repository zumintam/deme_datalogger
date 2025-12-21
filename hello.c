#include <errno.h>
#include <modbus.h>
#include <stdio.h>
#include <stdlib.h>

#define UART_PATH "/dev/ttyS1"

int main(void) {
  int ret;
  modbus_t* ctx;
  uint16_t dest[100];

  // ctx = modbus_new_rtu(UART_PATH, 9600, 'N', 8, 1);
  ctx = modbus_new_tcp("192.168.3.102", 502);
  if (ctx == NULL) {
    perror("Unable to create the libmodbus context\n");
    return -1;
  }

  ret = modbus_set_slave(ctx, 1);
  if (ret < 0) {
    perror("modbus_set_slave error\n");
    return -1;
  }

  ret = modbus_connect(ctx);
  if (ret < 0) {
    perror("modbus_connect error\n");
    return -1;
  }

  for (;;) {
    // Read 2 registers from adress 0, copy data to dest.
    ret = modbus_read_registers(ctx, 0, 20, dest);
    if (ret < 0) {
      perror("modbus_read_regs error\n");
      return -1;
    }

    printf("Temp0 on pc = %d\n", dest[0]);
    printf("Temp1 on pc = %d\n", dest[1]);

    sleep(1);
  }

  modbus_close(ctx);
  modbus_free(ctx);
}
