#include <string.h>
#include <unistd.h>
#include <zmq.h>

#include <iostream>

int main() {
  void* context = zmq_ctx_new();
  if (!context) {
    std::cerr << "Failed to create ZMQ context\n";
    return -1;
  }

  void* publisher = zmq_socket(context, ZMQ_PUB);
  if (!publisher) {
    std::cerr << "Failed to create ZMQ socket\n";
    zmq_ctx_term(context);
    return -1;
  }

  // Bind to a TCP port
  int rc = zmq_bind(publisher, "tcp://*:5555");
  if (rc != 0) {
    std::cerr << "Failed to bind socket: " << zmq_strerror(errno) << "\n";
    zmq_close(publisher);
    zmq_ctx_term(context);
    return -1;
  }

  std::cout << "ZMQ Publisher started. Sending messages...\n";

  while (1) {
    const char* msg = "Hello RK3506 ZMQ!";
    zmq_send(publisher, msg, strlen(msg), 0);
    std::cout << "Sent: " << msg << std::endl;
    sleep(1);
  }

  zmq_close(publisher);
  zmq_ctx_term(context);

  return 0;
}
