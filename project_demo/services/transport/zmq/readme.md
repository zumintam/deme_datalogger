Thư viện cung cấp lớp giao tiếp mạng (Transport Layer) dùng chung cho toàn bộ các Microservices trong hệ thống. Hiện tại, thư viện cài đặt giao thức ZeroMQ (ZMQ) theo mô hình Publish/Subscribe.

Thư viện được thiết kế theo hướng Interface-based để hỗ trợ Dependency Injection và dễ dàng Mocking khi viết Unit Test.
1. Yêu cầu hệ thống (Prerequisites)

Trước khi build, đảm bảo môi trường đã cài đặt thư viện ZeroMQ:

    Ubuntu/Debian:
    Bash

sudo apt-get update
sudo apt-get install libzmq3-dev

MacOS:
Bash

    brew install zeromq

2. Tích hợp vào Microservice (CMake)

Để sử dụng thư viện này trong một Service khác (ví dụ: OrderService), hãy thêm các dòng sau vào file CMakeLists.txt của Service đó.

Giả sử cấu trúc thư mục của bạn là:
Plaintext

/root
  ├── common/
  │    ├── transport/  <-- Thư viện này nằm ở đây
  │    └── CMakeLists.txt
  └── services/
       └── order-service/

Trong services/order-service/CMakeLists.txt:
CMake

# 1. Thêm thư mục common vào build
add_subdirectory(${CMAKE_SOURCE_DIR}/common/transport build_transport)

# 2. Khai báo executable của bạn
add_executable(order_service main.cpp OrderService.cpp)

# 3. Link thư viện transport
target_link_libraries(order_service 
    PRIVATE 
    libtransport    # Tên library định nghĩa trong common/transport/CMakeLists.txt
    pthread         # Cần thiết cho ZMQ thread
)

# 4. Include đường dẫn header (nếu chưa được export trong libtransport)
target_include_directories(order_service PRIVATE ${CMAKE_SOURCE_DIR}/common/transport/include)

3. Hướng dẫn sử dụng (Usage Guide)

Nguyên tắc cốt lõi: Luôn lập trình với Interface, chỉ khởi tạo Implementation ở Main.
Bước 1: Khai báo trong Service Class

Trong logic nghiệp vụ, chỉ include transport.h và nhận con trỏ Transport*. Không được include zmq_transport.h tại đây.
C++

// OrderService.h
#pragma once
#include "transport/transport.h" // Chỉ phụ thuộc Interface

class OrderService {
public:
    // Dependency Injection
    explicit OrderService(transport::Transport* transport) 
        : transport_(transport) {}

    void init() {
        // Đăng ký nhận tin từ topic "payment/completed"
        transport_->subscribe("payment/completed");

        // Cài đặt hàm xử lý tin nhắn
        transport_->setMessageHandler([this](const std::string& topic, const auto& data) {
            this->onMessage(topic, data);
        });
    }

    void sendStatus() {
        std::string msg = "Order Created";
        // Convert sang vector<uint8_t>
        std::vector<uint8_t> payload(msg.begin(), msg.end());
        
        // Gửi tin đi
        transport_->send("order/created", payload);
    }

private:
    void onMessage(const std::string& topic, const std::vector<uint8_t>& data) {
        // Xử lý tin nhắn nhận được...
    }

    transport::Transport* transport_;
};

Bước 2: "Đấu dây" (Wiring) ở Main

Đây là nơi duy nhất bạn include implementation cụ thể của ZMQ.
C++

// main.cpp
#include "transport/zmq_transport.h" // Include implementation
#include "OrderService.h"

int main() {
    // Cấu hình: Publish ra cổng 5555, Subscribe từ cổng 5556
    auto zmqModule = std::make_shared<transport::ZmqTransport>(
        "tcp://*:5555",       // Publisher Endpoint
        "tcp://localhost:5556" // Subscriber Endpoint
    );

    // Mở kết nối
    if (!zmqModule->open()) {
        return -1; // Lỗi không thể khởi tạo ZMQ
    }

    // Tiêm (Inject) dependency vào Service
    OrderService service(zmqModule.get());
    service.init();

    // Giữ chương trình chạy
    while(true) {
        // ... logic loop ...
    }
}

4. Lưu ý quan trọng (Notes)
Thread Safety (An toàn luồng)

    Sending: Hàm send() là thread-safe. Bạn có thể gọi nó từ nhiều thread khác nhau trong Service.

    Receiving: Thư viện chạy một thread ngầm (std::thread) để nhận tin nhắn từ ZMQ. Do đó, MessageHandler (Callback) sẽ được gọi từ thread ngầm này, không phải main thread.

        ⚠️ Cảnh báo: Nếu trong hàm callback bạn truy cập vào biến chung (shared variable) của Service, bạn PHẢI dùng std::mutex để khóa bảo vệ dữ liệu.

Định dạng dữ liệu (Payload)

    Dữ liệu được truyền dưới dạng std::vector<uint8_t> (raw bytes).

    Thư viện không quan tâm nội dung bên trong. Các service tự quy định protocol (JSON, Protobuf, Struct, v.v.).

PIMPL Idiom

    Class ZmqTransport sử dụng kỹ thuật PIMPL để giấu thư viện <zmq.hpp>. Điều này giúp giảm thời gian biên dịch cho các service sử dụng nó và tránh xung đột thư viện.

5. Mocking để Unit Test

Để test Service mà không cần chạy ZMQ thật, bạn có thể tạo Mock class:
C++

class MockTransport : public transport::Transport {
public:
    bool send(const std::string& ch, const Payload& data) override {
        // Lưu lại data để kiểm tra trong test case
        return true; 
    }
    // ... implement dummy các hàm khác
};