// common/src/zmq_transport.cpp
#include "zmq.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>
#include <zmq.hpp>

namespace transport {

class ZmqTransport::Impl {
 public:
  Impl(const std::string& pub_addr, const std::string& sub_addr)
      : pub_addr_(pub_addr),
        sub_addr_(sub_addr),
        context_(1),
        pub_socket_(context_, zmq::socket_type::pub),
        sub_socket_(context_, zmq::socket_type::sub),
        running_(false) {}

  ~Impl() { shutdown(); }

  bool start() {
    try {
      // 1. Cấu hình socket Publisher
      // Tùy kiến trúc: Service thường BIND để người khác connect,
      // hoặc CONNECT tới một Central Broker. Ở đây ví dụ Connect.
      pub_socket_.connect(pub_addr_);

      // 2. Cấu hình socket Subscriber
      sub_socket_.connect(sub_addr_);

      // Mặc định ZMQ sub lọc tất cả, phải subscribe "" để nhận hết (nếu muốn)
      // Ở đây ta để user tự gọi hàm subscribe() sau.

      running_ = true;
      // 3. Chạy thread nhận tin
      worker_thread_ = std::thread(&Impl::receiveLoop, this);

      return true;
    } catch (const zmq::error_t& e) {
      std::cerr << "[ZMQ] Start Failed: " << e.what() << std::endl;
      return false;
    }
  }

  void shutdown() {
    running_ = false;
    if (worker_thread_.joinable()) {
      worker_thread_.join();
    }
    // Context ZMQ tự hủy khi destructor được gọi
  }

  bool publish(const std::string& topic, const Payload& data) {
    // Bảo vệ socket PUB vì có thể nhiều thread trong service cùng gọi send
    std::lock_guard<std::mutex> lock(pub_mutex_);
    try {
      // Gửi Topic trước (SNDMORE)
      pub_socket_.send(zmq::buffer(topic), zmq::send_flags::sndmore);
      // Gửi Data sau
      pub_socket_.send(zmq::buffer(data), zmq::send_flags::none);
      return true;
    } catch (const zmq::error_t& e) {
      std::cerr << "[ZMQ] Publish Error: " << e.what() << std::endl;
      return false;
    }
  }

  bool subscribe(const std::string& topic) {
    // Socket SUB không thread-safe, nhưng hàm setsockopt có thể thread-safe
    // tùy implementation. Tốt nhất nên lock hoặc chỉ gọi khi init.
    // ZMQ Document khuyên không thao tác socket từ thread khác thread tạo ra
    // nó.
    // -> Cách an toàn nhất là dùng socket pair để báo thread worker subscribe,
    // NHƯNG để đơn giản cho wrapper này, ta giả định sub chỉ gọi lúc init.
    try {
      sub_socket_.set(zmq::sockopt::subscribe, topic);
      return true;
    } catch (...) {
      return false;
    }
  }

  bool unsubscribe(const std::string& topic) {
    try {
      sub_socket_.set(zmq::sockopt::unsubscribe, topic);
      return true;
    } catch (...) {
      return false;
    }
  }

  void setHandler(MessageHandler handler) {
    std::lock_guard<std::mutex> lock(cb_mutex_);
    handler_ = handler;
  }

 private:
  void receiveLoop() {
    while (running_) {
      // Polling với timeout 100ms để check biến running_
      zmq::pollitem_t items[] = {{sub_socket_, 0, ZMQ_POLLIN, 0}};
      zmq::poll(items, 1, std::chrono::milliseconds(100));

      if (items[0].revents & ZMQ_POLLIN) {
        processMessage();
      }
    }
  }

  void processMessage() {
    try {
      zmq::message_t topic_msg, data_msg;

      // Đọc topic
      auto res = sub_socket_.recv(topic_msg, zmq::recv_flags::none);
      if (!res) return;

      // Đọc payload nếu có
      if (topic_msg.more()) {
        sub_socket_.recv(data_msg, zmq::recv_flags::none);
      }

      std::string topic(static_cast<char*>(topic_msg.data()), topic_msg.size());
      Payload data(static_cast<uint8_t*>(data_msg.data()),
                   static_cast<uint8_t*>(data_msg.data()) + data_msg.size());

      // Gọi callback an toàn
      MessageHandler callback;
      {
        std::lock_guard<std::mutex> lock(cb_mutex_);
        callback = handler_;
      }
      if (callback) {
        callback(topic, data);
      }

    } catch (const std::exception& e) {
      std::cerr << "[ZMQ] Recv Error: " << e.what() << std::endl;
    }
  }

  std::string pub_addr_, sub_addr_;
  zmq::context_t context_;
  zmq::socket_t pub_socket_;
  zmq::socket_t sub_socket_;

  std::atomic<bool> running_;
  std::thread worker_thread_;

  std::mutex pub_mutex_;  // Lock cho socket gửi
  std::mutex cb_mutex_;   // Lock cho callback handler
  MessageHandler handler_;
};

// --- Phần Wrapper chuyển tiếp gọi vào Impl ---

ZmqTransport::ZmqTransport(const std::string& pub, const std::string& sub)
    : impl_(std::make_unique<Impl>(pub, sub)) {}

ZmqTransport::~ZmqTransport() = default;

bool ZmqTransport::open() { return impl_->start(); }
void ZmqTransport::close() { impl_->shutdown(); }
bool ZmqTransport::send(const std::string& t, const Payload& d) {
  return impl_->publish(t, d);
}
bool ZmqTransport::subscribe(const std::string& t) {
  return impl_->subscribe(t);
}
bool ZmqTransport::unsubscribe(const std::string& t) {
  return impl_->unsubscribe(t);
}
void ZmqTransport::setMessageHandler(MessageHandler h) { impl_->setHandler(h); }

}  // namespace transport