#include <sys/time.h>
#include <time.h>

#include <cstring>
#include <iostream>

// Đọc thời gian hệ thống (đã sync với RTC)
void readSystemTime() {
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);

  char buffer[80];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);

  std::cout << "System Time: " << buffer << std::endl;
  std::cout << "Unix Timestamp: " << now << std::endl;
}

// Đọc với độ chính xác cao hơn (microseconds)
void readHighResolutionTime() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);

  struct tm* timeinfo = localtime(&tv.tv_sec);
  char buffer[80];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);

  std::cout << "High-Res Time: " << buffer << "." << tv.tv_usec << std::endl;
}
int main() {
  readSystemTime();
  readHighResolutionTime();
}