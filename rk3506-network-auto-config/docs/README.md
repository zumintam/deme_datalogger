Dưới đây là nội dung file **README.md** hoàn chỉnh, được viết theo phong cách chuyên nghiệp (Technical Documentation) để bạn gửi cho anh Mentor kiểm tra. 

File này tóm tắt toàn bộ tư duy logic, cấu trúc thư mục và cách thức vận hành hệ thống cấu hình mạng tự động trên board RK3506.

---

# Project Horus: Network Auto-Configuration System
**Target Platform:** Rockchip RK3506 (Embedded Linux)  
**Author:** Vu Minh Tam  
**Focus:** Smart Interface Selection & Deterministic Routing

---

## 1. Overview
Hệ thống này được thiết kế để tự động hóa quá trình thiết lập mạng cho các Industrial Gateway Horus. Mục tiêu chính là loại bỏ việc cấu hình thủ công và đảm bảo hệ thống luôn có kết nối Internet ổn định bất kể dây mạng được cắm vào cổng vật lý nào (**eth1** hoặc **eth2**).

### Key Features:
*   **Smart Link Sensing:** Tự động phát hiện trạng thái vật lý (Link Carrier) của các cổng Ethernet.
*   **User-Defined Profiles:** Cho phép người dùng cấu hình Static IP hoặc DHCP thông qua file cấu hình tại phân vùng dữ liệu (`/data`).
*   **Routing Sanitization:** Triệt để xử lý lỗi xung đột bảng định tuyến (Routing table conflict) và lỗi trỏ Gateway nhầm vào giao diện Loopback (`lo`).

---

## 2. Directory Structure
Cấu trúc các tệp tin trong module:
```text
rk3506-network-auto-config/
├── data/
│   └── network.conf          # User configuration file (Key-Value format)
└── rootfs_overlay/
    └── etc/
        └── init.d/
            └── S46network    # Core Network Initialization Script
```

---

## 3. Configuration Specification (`network.conf`)
File cấu hình nằm trong phân vùng `/data` giúp kỹ thuật viên có thể thay đổi thông số mạng mà không cần nạp lại Firmware.

```bash
# Network Mode: STATIC or DHCP
MODE="STATIC"

# Interface Specifications
IP="192.168.0.100"
MASK="255.255.255.0"
GW="192.168.0.1"
DNS="8.8.8.8"
```

---

## 4. Logical Implementation Details

### Step 1: Interface Discovery
Script sử dụng thuộc tính kernel tại `/sys/class/net/ethX/carrier` để xác định chính xác cổng nào đang có kết nối vật lý. Việc này chính xác hơn nhiều so với việc kiểm tra trạng thái phần mềm (Software UP).

### Step 2: Environment Sanitization
Trước khi áp dụng cấu hình mới, script thực hiện:
*   **Flush IP/Route:** Xóa toàn bộ địa chỉ và bảng định tuyến cũ trên interface mục tiêu để tránh tình trạng Secondary IP hoặc Overlap Subnet.
*   **Loopback Protection:** Khôi phục trạng thái chuẩn cho `lo` (127.0.0.1) và xóa bỏ mọi default route bị gán nhầm vào cổng này.

### Step 3: Metric-based Routing
Khi gán Gateway mặc định, script sử dụng **Metric 10** để đảm bảo ưu tiên đường truyền này là cao nhất trong hệ thống, tránh tranh chấp với các dịch vụ mạng mặc định của OS.

---

## 5. Deployment & Testing

### How to Deploy:
1. Copy thư mục `rootfs_overlay` vào thư mục overlay tương ứng trong Buildroot SDK.
2. Đảm bảo quyền thực thi được khai báo trong `device_table.txt`:
   ```text
   /etc/init.d/S46network f 755 0 0
   ```
3. Đảm bảo `/etc/fstab` đã mount phân vùng chứa file config trước khi script chạy.

### Verification Commands:
*   **Kiểm tra bảng định tuyến:** `ip route` (Đảm bảo không còn `dev lo` cho default gateway).
*   **Kiểm tra kết nối:** `ping 8.8.8.8` (Kết quả đạt 0% packet loss).
*   **Kiểm tra logs:** Xem log khởi động tại console để xác nhận logic phát hiện interface.

---

## 6. Project Notes (Mentor Review)
*   **Xử lý lỗi:** Đã fix lỗi board không ping được do cấu hình đè vào interface `lo`.
*   **Tính linh hoạt:** Hệ thống ưu tiên `eth1` (Gigabit) nếu cả hai cổng cùng có link, ngược lại sẽ tự động chuyển sang `eth2`.
*   **Độ tin cậy:** Sử dụng `udhcpc` với cờ `-b` (background) để tránh block quá trình boot nếu không có DHCP server.

---
```