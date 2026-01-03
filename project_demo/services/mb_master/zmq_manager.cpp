// zmq_manager.h
class ZmqManager {
 private:
  void* context_;
  void* dealer_;
  std::string endpoint_;
  bool is_connected_;

 public:
  ZmqManager() : context_(nullptr), dealer_(nullptr), is_connected_(false) {}

  ~ZmqManager() { cleanup(); }

  bool init(const std::string& endpoint) {
    if (endpoint.empty()) {
      std::cerr << "ERROR: ZMQ endpoint is empty" << std::endl;
      return false;
    }

    endpoint_ = endpoint;

    context_ = zmq_ctx_new();
    if (!context_) {
      std::cerr << "ERROR: Failed to create ZMQ context" << std::endl;
      return false;
    }

    dealer_ = zmq_socket(context_, ZMQ_DEALER);
    if (!dealer_) {
      std::cerr << "ERROR: Failed to create ZMQ dealer socket" << std::endl;
      zmq_ctx_destroy(context_);
      context_ = nullptr;
      return false;
    }

    int rc = zmq_connect(dealer_, endpoint.c_str());
    if (rc != 0) {
      std::cerr << "ERROR: ZMQ connect failed: " << zmq_strerror(errno)
                << std::endl;
      cleanup();
      return false;
    }

    is_connected_ = true;
    std::cout << "INFO: ZMQ connected to " << endpoint << std::endl;
    return true;
  }

  bool send(const std::string& message) {
    if (!is_connected_ || !dealer_) {
      std::cerr << "ERROR: ZMQ not connected" << std::endl;
      return false;
    }

    if (message.empty()) {
      std::cerr << "WARNING: Attempting to send empty message" << std::endl;
      return false;
    }

    int rc = zmq_send(dealer_, message.c_str(), message.size(), 0);

    if (rc == -1) {
      std::cerr << "ERROR: ZMQ send failed: " << zmq_strerror(errno)
                << std::endl;
      return false;
    }

    if (rc != static_cast<int>(message.size())) {
      std::cerr << "WARNING: Partial send" << std::endl;
      return false;
    }

    return true;
  }

  void cleanup() {
    if (dealer_) {
      zmq_close(dealer_);
      dealer_ = nullptr;
      std::cout << "INFO: ZMQ dealer closed" << std::endl;
    }

    if (context_) {
      zmq_ctx_destroy(context_);
      context_ = nullptr;
      std::cout << "INFO: ZMQ context destroyed" << std::endl;
    }

    is_connected_ = false;
  }

  bool isConnected() const { return is_connected_; }
};

// Sử dụng:
ZmqManager zmq;
if (!zmq.init("tcp://localhost:5555")) {
  return -1;
}

std::string json = createEnvelopeJson("device1", "ok", data_json);
if (!zmq.send(json)) {
  std::cerr << "Failed to send message" << std::endl;
}