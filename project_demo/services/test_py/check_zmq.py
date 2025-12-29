import zmq

context = zmq.Context()
subscriber = context.socket(zmq.SUB)
subscriber.connect("tcp://192.168.137.34:5555")  # IP của board
subscriber.setsockopt_string(zmq.SUBSCRIBE, "")

print("Waiting for data from RK3506...")
while True:
    message = subscriber.recv_string()
    print(f"Received: {message}")
