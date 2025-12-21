#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace transport {
class Transport {
 public:
  typedef std::vector<uint8_t> Payload;
  typedef std::function<void(const std::string& channel, const Payload& data)>
      MessageHandler;

  virtual ~Transport() = default;
  virtual bool open() = 0;
  virtual void close() = 0;

  // message sending
  virtual bool send(const std::string& channel, const Payload& data) = 0;
  virtual bool subscribe(const std::string& channel) = 0;
  virtual bool unsubscribe(const std::string& channel) = 0;
  // callback registration
  virtual void setMessageHandler(MessageHandler handler) = 0;
};
}  // namespace transport