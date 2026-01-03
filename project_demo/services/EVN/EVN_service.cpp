#include "EVN_service.h"

/*Chạy Modbus TCP Server / IEC104

Nhận dữ liệu đã chuẩn hoá từ nội bộ (ZMQ)

Map dữ liệu → Register EVN

Nhận lệnh EVN ghi (Holding Register)

Forward lệnh sang config-service*/

// giao tiep modbus - EVN su dung co che cache doc ghi register
// doc gia tri dien nang tu dong bo EVN >> sub vao topic ZMQ cua Modbus

// Lay du lieu tu EVN gui ve qua modbus va luu vao CSDL
// gui du lieu dien nang ve EVN qua modbus
// gui canh bao ve EVN qua modbus
// cau hinh thong so modbus cho EVN
// cau hinh thong so canh bao cho EVN
// cau hinh thong so dien nang cho EVN
// bao loi giao tiep modbus - EVN
// bao loi du lieu dien nang - EVN

/*+-------------------+
Modbus / Inverter
        |
        | ZMQ PUB (raw JSON)
        v
Data Normalizer
        |
        | ZMQ PUB (normalized JSON)
        v
Cache Service
        |
        | (in-memory snapshot)
        v
EVN Adapter (Modbus TCP Server)
        ^
        |
      EVN (Master)

*/



int main() {
  // Implementation of the main function for EVN service
  return 0;
}
