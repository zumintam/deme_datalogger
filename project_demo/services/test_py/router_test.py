import zmq

context = zmq.Context()

router = context.socket(zmq.ROUTER)
router.bind("ipc:///tmp/modbus_pipeline.ipc")

print(" ZMQ ROUTER listening on ipc:///tmp/modbus_pipeline.ipc")

try:
    while True:
        parts = router.recv_multipart()

        if len(parts) != 2:
            print(" Invalid message:", parts)
            continue

        identity, message = parts

        print(" Client ID:", identity)
        print(" Message:", message.decode())
        print("-" * 60)

except KeyboardInterrupt:
    print(" Stopped")

finally:
    router.close()
    context.term()
