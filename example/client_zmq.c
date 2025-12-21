#include <zmq.h>
#include <string.h>
#include <stdio.h>

int main(void) {
    // Tạo context ZMQ
    void *context = zmq_ctx_new();

    // Tạo socket kiểu REQ (request)
    void *requester = zmq_socket(context, ZMQ_REQ);
    zmq_connect(requester, "tcp://localhost:5555");

    for (int i = 0; i < 5; i++) {
        char message[50];
        sprintf(message, "Hello %d", i);

        printf("Gửi tới server: %s\n", message);
        zmq_send(requester, message, strlen(message), 0);

        char buffer[256];
        zmq_recv(requester, buffer, 255, 0);
        buffer[255] = '\0';

        printf("Nhận từ server: %s\n", buffer);
    }

    zmq_close(requester);
    zmq_ctx_destroy(context);
    return 0;
}

