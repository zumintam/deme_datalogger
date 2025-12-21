#pragma once
#include "../transport.h"

// common/include/zmq_transport.h
#include <memory>
#include <string>

namespace transport {

class ZmqTransport : public Transport {
 public:
  // endpoint_pub: Địa chỉ để publish tin (VD: "tcp://*:5555" hoặc connect tới
  // broker) endpoint_sub: Địa chỉ để subscribe tin (VD: "tcp://localhost:5556")
  ZmqTransport(const std::string& endpoint_pub,
               const std::string& endpoint_sub);
  ~ZmqTransport() override;

  bool open() override;
  void close() override;

  bool send(const std::string& topic, const Payload& data) override;
  bool subscribe(const std::string& topic) override;
  bool unsubscribe(const std::string& topic) override;
  void setMessageHandler(MessageHandler handler) override;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace transport