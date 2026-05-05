#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
  // 1. Header bắt buộc
  printf("Content-Type: application/json\n\n");

  char* method = getenv("REQUEST_METHOD");

  // 2. Xử lý GET: Trả về dữ liệu Solar từ RAM
  if (method && strcmp(method, "GET") == 0) {
    FILE* fp = fopen("/tmp/solar_raw.json", "r");
    if (fp) {
      char buffer[256];
      while (fgets(buffer, sizeof(buffer), fp)) printf("%s", buffer);
      fclose(fp);
    } else {
      printf("{\"status\":\"error\", \"msg\":\"No data in RAM\"}");
    }
  }
  // 3. Xử lý POST: Nhận cấu hình từ Client
  else if (method && strcmp(method, "POST") == 0) {
    char* len_str = getenv("CONTENT_LENGTH");
    if (len_str) {
      int len = atoi(len_str);
      char* data = malloc(len + 1);
      fread(data, 1, len, stdin);
      data[len] = '\0';

      // Lưu lại để kiểm chứng
      FILE* f_cfg = fopen("/tmp/new_config.json", "w");
      if (f_cfg) {
        fprintf(f_cfg, "%s", data);
        fclose(f_cfg);
      }

      printf("{\"status\":\"success\", \"received\":%s}", data);
      free(data);
    }
  }
  return 0;
}