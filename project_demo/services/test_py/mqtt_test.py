import zmq
import paho.mqtt.client as mqtt
import json
import time

# --- CẤU HÌNH ---
ZMQ_ADDR = "tcp://192.168.137.57:5555"  # IP của máy chạy code C++
MQTT_BROKER = "192.168.137.57"  # IP của MQTT Broker
MQTT_PORT = 1883
MQTT_TOPIC = "meter/data"


def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print("✅ Connected to MQTT Broker!")
    else:
        print(f"❌ Failed to connect, return code {rc}")


def run_bridge():
    # 1. Khởi tạo ZMQ Subscriber
    context = zmq.Context()
    zmq_sub = context.socket(zmq.SUB)
    zmq_sub.connect(ZMQ_ADDR)
    zmq_sub.setsockopt_string(zmq.SUBSCRIBE, "")  # Subscribe tất cả message

    print(f"🔗 Connected to ZMQ at {ZMQ_ADDR}")

    # 2. Khởi tạo MQTT Client
    mqtt_client = mqtt.Client()
    mqtt_client.on_connect = on_connect

    try:
        mqtt_client.connect(MQTT_BROKER, MQTT_PORT, 60)
    except Exception as e:
        print(f"❌ MQTT Connection Error: {e}")
        return

    mqtt_client.loop_start()

    print(f"🚀 Bridge started: ZMQ:{5555} -> MQTT:{MQTT_TOPIC}")

    try:
        while True:
            # Nhận dữ liệu từ ZMQ (non-blocking hoặc blocking)
            message = zmq_sub.recv_string()

            print(f"📩 Received from ZMQ: {message}")

            # Kiểm tra định dạng JSON (tùy chọn)
            try:
                # Nếu muốn xử lý/lọc dữ liệu trước khi gửi, parse tại đây
                data = json.loads(message)

                # Publish lên MQTT
                mqtt_client.publish(MQTT_TOPIC, json.dumps(data))
                print(f"📤 Published to MQTT: {MQTT_TOPIC}")

            except json.JSONDecodeError:
                # Nếu message không phải JSON, gửi thô
                mqtt_client.publish(MQTT_TOPIC, message)

    except KeyboardInterrupt:
        print("\n🛑 Stopping bridge...")
    finally:
        zmq_sub.close()
        context.term()
        mqtt_client.loop_stop()
        mqtt_client.disconnect()


if __name__ == "__main__":
    run_bridge()
