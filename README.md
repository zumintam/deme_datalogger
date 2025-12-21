Dưới đây là bản **README.md** tóm tắt đầy đủ – rõ ràng – cấu trúc, luồng dữ liệu và cách mở rộng nhiều thiết bị theo đúng kiến trúc bạn đã chọn.

---

# 📘 **Kiến trúc Hệ thống IoT Gateway – Microservices / Modular Monolith**

README này mô tả kiến trúc tổng thể của hệ thống, cách tổ chức thư mục code, luồng giao tiếp giữa C++ Driver – Python Processor – ZMQ Bus – Cloud Agent, và cách mở rộng khi board có nhiều thiết bị mới.

---

# 🏗 **Tổng quan Kiến trúc**

Hệ thống được xây dựng theo mô hình **Microservices** gồm 3 lớp chính:

1. **C++ Drivers (Hardware Layer)**

   * Giao tiếp trực tiếp với thiết bị qua Modbus/RS485/TCP/Serial
   * Decode dữ liệu low-level
   * Gửi dữ liệu thô (raw data) qua ZMQ

2. **Python Processor (Business Logic Layer)**

   * Chuẩn hóa dữ liệu
   * Xử lý logic nghiệp vụ
   * Quản lý trạng thái thiết bị (State Manager)
   * Xử lý và dịch lệnh từ cloud (Command Router)
   * Sử dụng Plugin cho từng loại thiết bị

3. **ZeroMQ Bus (Transport Layer)**

   * Là message broker nội bộ
   * Dùng PUB/SUB cho data
   * Dùng REQ/REP hoặc PUSH/PULL cho control flow

4. **Cloud Agent**

   * Đẩy dữ liệu lên cloud
   * Nhận command từ cloud và chuyển vào Python Processor

---

# 📁 **Cấu trúc Thư mục (Microservice-Ready)**

```
project-root/
│
├── libs/                        ← Nơi lưu trữ các file thư viện tĩnh (.a)
│   ├── libprotocol.a            ← Ví dụ: thư viện giao thức
│   └── libdriver_sdk.a          ← Ví dụ: thư viện SDK của hãng
│
├── include/                     ← Nơi chứa file header (.h) để code C/C++ gọi được
│   ├── protocol.h
│   └── driver_sdk.h
│
├── drivers/                     ← (C++ hoặc Python Wrapper) sử dụng thư viện trên
│   ├── CMakeLists.txt           ← [MỚI] File cấu hình link thư viện .a vào driver
│   ├── inverter_driver/
│   ├── meter_driver/
│   └── <new_device>_driver/
│
├── processor/                   
│   ├── core/
│   │   ├── processor_main.py
│   │   ├── state_manager.py
│   │   ├── command_router.py
│   │   └── zmq_bus.py
│   ├── plugins/
│   │   ├── inverter/
│   │   ├── meter/
│   │   └── <new_device>/
│   │       ├── plugin.py
│   │       └── mapping.json
│   └── configs/
│       ├── system.json
│       ├── devices.json
│       └── cloud.json
│
├── bus/                         
│   ├── zmq_pubsub.py
│   ├── zmq_reqrep.py
│   └── zmq_pushpull.py
│
├── cloud-agent/                 
│   ├── agent_main.py
│   └── queue/
│
├── shared/                      
│   ├── logger.py
│   ├── json_utils.py
│   └── constants.py
│
├── docker/
│
└── CMakeLists.txt               File build tổng
└── docker/
```

---

# 🔄 **Luồng Dữ liệu**

### **1. Data Flow (PUB/SUB)**

```
C++ Driver  →  raw_data.<device>.<id>  →  Python Processor  → clean_data → Cloud
```

* C++ gửi raw data: `raw_data.inverter.1`
* Python plugin xử lý → clean data: `clean_data.inverter.1`

### **2. Control Flow (REQ/REP hoặc PUSH/PULL)**

```
Cloud → Cloud Agent → Python Logic → ZMQ REQ → C++ Driver
                                                 ↓
                                      ZMQ REP ← ACK/Result
```

* Python Processor đảm bảo lệnh hợp lệ
* C++ Driver thực thi và trả ACK

---

# 🧩 **Quản lý Trạng thái (State Management)**

Python Processor lưu:

* Trạng thái kết nối
* Lỗi
* Last known values
* Firmware info

Lưu trong JSON local cho khả năng phục hồi (restart-safe).

---

# ➕ **Cách Thêm Device Mới (Quan trọng nhất)**

Khi board có thêm thiết bị mới:

## **1️⃣ Thêm Driver C++ mới**

Tạo folder mới:

```
drivers/<device>_driver/
    src/
    include/
    configs/
```

Driver phụ trách:

* giao tiếp hardware
* decode bytes
* publish raw_data.<device>.<id>

---

## **2️⃣ Thêm Plugin Python mới**

Tạo:

```
processor/plugins/<device>/
    plugin.py
    mapping.json
```

Plugin phụ trách:

* chuẩn hoá dữ liệu
* mapping field
* xử lý logic riêng
* validate lệnh trước khi gửi driver

---

## **3️⃣ Cập nhật khai báo trong devices.json**

```
{
  "devices": [
    { "id": "inv_1", "type": "inverter", "driver": "inverter_driver" },
    { "id": "bms_1", "type": "bms", "driver": "bms_driver" }  ← thêm device mới
  ]
}
```

Python Processor sẽ:

* load plugin mới tự động
* subscribe topic mới
* publish clean data tương ứng

---

# 📡 **Quy ước ZMQ Topic**

| Loại        | Topic                       | Mô tả              |
| ----------- | --------------------------- | ------------------ |
| Raw Data    | `raw_data.<device>.<id>`    | Driver → Processor |
| Clean Data  | `clean_data.<device>.<id>`  | Processor → Cloud  |
| Command     | `command.<device>.<id>`     | Cloud → Processor  |
| Command ACK | `command_ack.<device>.<id>` | Driver → Cloud     |

---

# 🚀 **Lợi ích của Kiến trúc này**

* Thêm/tắt thiết bị **không đụng core code**
* C++ và Python hoàn toàn tách biệt
* Dùng JSON config → thay đổi không cần build lại
* Thêm device = thêm 1 driver + 1 plugin
* Docker-friendly, CI/CD dễ dàng
* Hiệu suất cao (C++ + ZeroMQ)
* Linh hoạt cho IoT Gateway/Edge Device