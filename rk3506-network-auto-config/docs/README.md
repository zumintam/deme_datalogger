# Horus Network Service: S45user_network
**Module:** User-defined Network Initialization  
**Platform:** Rockchip RK3506 (Embedded Linux)  
**Priority:** Execution Level 45 (Post-Mount, Pre-Application)

---

## 1. Introduction
`S45user_network` là script quản lý cấu hình mạng thông minh cho dự án Horus. Thay vì sử dụng cấu hình mặc định cứng nhắc của hệ điều hành, script này cho phép hệ thống tự động điều chỉnh thông số mạng dựa trên tệp cấu hình của người dùng và trạng thái kết nối vật lý của phần cứng.

---

## 2. Problem Solver
Script này được viết để giải quyết triệt để 3 vấn đề kỹ thuật lớn đã gặp phải trong quá trình phát triển:
1.  **Routing Loopback Error:** Khắc phục lỗi gói tin Internet bị điều hướng nhầm vào giao diện `lo` (Metric 10), gây mất kết nối hoàn toàn.
2.  **Subnet Overlap:** Xử lý tình trạng xung đột khi cả hai cổng mạng (`eth1`, `eth2`) cùng nhận địa chỉ IP thuộc dải `192.168.0.x`.
3.  **Static/DHCP Flexibility:** Cho phép thay đổi IP mà không cần can thiệp vào phân vùng RootFS (Read-only).

---

## 3. Workflow & Logic
Script vận hành theo quy trình **Deterministic Networking** (Mạng xác định):

1.  **Sanitization Phase:**
    *   Thực hiện `ip addr flush` trên interface mục tiêu để dọn dẹp các IP "rác" từ DHCP trước đó.
    *   Xóa bỏ mọi `default route` cũ để chuẩn bị cho việc nạp Gateway mới.
    *   Khôi phục trạng thái chuẩn cho Loopback (`127.0.0.1`).
2.  **Interface Sensing:**
    *   Sử dụng `/sys/class/net/ethX/carrier` để phát hiện dây mạng đang cắm vào cổng nào.
3.  **Configuration Deployment:**
    *   Nạp biến từ `/data/network.conf`.
    *   Áp dụng `ifconfig` cho IP/Mask.
    *   Thiết lập Gateway với **Metric 10** để đảm bảo mức ưu tiên cao nhất trong bảng định tuyến.

---

## 4. Configuration Template (`/data/network.conf`)
Định dạng file cấu hình được tối giản để dễ dàng bảo trì:
```bash
MODE="STATIC"        # Options: STATIC | DHCP
IP="192.168.0.100"
MASK="255.255.255.0"
GW="192.168.0.1"
DNS="8.8.8.8"
```

---

## 5. Technical Notes for Mentor
*   **Execution Order:** Script được đặt tên là `S45` để đảm bảo nó chạy sau khi `/data` đã được mount (`S00`) và ghi đè được các thiết lập mặc định của `S40network`.
*   **Performance:** Sử dụng `udhcpc -b` (background mode) để không làm treo quá trình khởi động nếu môi trường mạng không có DHCP server.
*   **Verification:** Bảng định tuyến sau khi chạy script sẽ chỉ chứa 1 `default gateway` duy nhất hướng ra interface vật lý (eth1 hoặc eth2), đảm bảo Ping 8.8.8.8 đạt độ trễ thấp và ổn định.

---

## 6. Maintenance Commands
*   **Restart Service:** `/etc/init.d/S45user_network restart`
*   **Debug Routing:** `ip route` hoặc `route -n`
*   **Manual Link Check:** `cat /sys/class/net/eth2/carrier`
