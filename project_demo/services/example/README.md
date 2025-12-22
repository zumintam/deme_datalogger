📂 SolarBK Gateway - Meter Driver Module

Module này chịu trách nhiệm giao tiếp lớp vật lý (Physical Layer) qua giao thức Modbus RTU/TCP để đọc dữ liệu từ Smart Meters và Inverters, sau đó phân phối dữ liệu qua ZeroMQ.
✨ Tính năng chính

    Đa luồng (Multi-threading): Tách biệt luồng đọc dữ liệu (Polling) và luồng nhận lệnh điều khiển (Command).

    ZeroMQ Integration: * PUB (Port 5555): Phát dữ liệu telemetry (Điện áp, công suất, sản lượng) dưới dạng JSON.

        PULL (Port 5556): Nhận lệnh điều khiển từ EVN hoặc Cloud (Giới hạn công suất, Bật/Tắt).

    Data Scaling: Tự động ghép thanh ghi (U32/U64) và áp dụng hệ số nhân (Gain) từ file cấu hình.

    Safe-Control: Sử dụng Mutex để đảm bảo an toàn khi truy cập RS485 từ nhiều luồng.

🛠 Yêu cầu hệ thống (Dependencies)

Module được biên dịch chéo (cross-compile) cho chip Rockchip RK3506 (ARM):

    libmodbus: Xử lý giao thức Modbus.

    libzmq (ZeroMQ): Xử lý truyền tin nội bộ và mạng.

    cJSON: Parse và build chuỗi JSON.

    pthread & rt: Thư viện luồng và thời gian thực của Linux.

🏗 Cấu trúc thư mục
Plaintext

project_demo/
├── components/
│   └── dist_libs/          # Chứa file .a và .h của các thư viện phụ thuộc
├── drivers/
│   └── meter_driver/       # Mã nguồn chính của module
│       ├── main.cpp        # Điểm khởi đầu và quản lý luồng
│       ├── meter_driver.cpp # Logic Modbus chi tiết
│       └── meter_config.json # File cấu hình thanh ghi

🚀 Hướng dẫn biên dịch (Cross-Compile)

Sử dụng CMake với Toolchain của SDK RK3506:
Bash

mkdir build && cd build
cmake .. \
  -DCMAKE_C_COMPILER=arm-buildroot-linux-gnueabihf-gcc \
  -DCMAKE_CXX_COMPILER=arm-buildroot-linux-gnueabihf-g++ \
  -DCMAKE_SYSTEM_NAME=Linux \
  -DCMAKE_SYSROOT=/path/to/your/sysroot
make

📊 Luồng dữ liệu (Data Flow)

    Polling Thread: * Đọc thanh ghi Modbus (Input/Holding).

        Scale giá trị (Raw * Gain).

        Gửi JSON qua tcp://*:5555.

    Command Thread:

        Đợi lệnh JSON từ tcp://localhost:5556.

        Thực hiện modbus_write_register xuống thiết bị (vd: Thanh ghi 40003 - Limit Power).

⚙️ Cấu hình (meter_config.json)

File này ánh xạ địa chỉ Modbus với tên biến thực tế dựa trên tài liệu kỹ thuật của Aster.
JSON

{
    "device_id": "1",
    "serial_port": "/dev/ttyS3",
    "baudrate": 9600,
    "slave_id": 1,
    "poll_interval_ms": 1000,
    "registers": {
        "voltage_L1": {
            "address": 4012,
            "scale": 1,
            "quantity": 1
        }
    }
}
⚠️ Lưu ý vận hành

    Zero Export: Khi triển khai Zero Export, hãy đảm bảo chu kỳ quét (Polling) dưới 1000ms để phản ứng kịp thời với thay đổi tải.

    EVN Control: Mọi lệnh ghi xuống thanh ghi 40003 phải được ghi kèm trạng thái vào thanh ghi 40002 (Mode 4).