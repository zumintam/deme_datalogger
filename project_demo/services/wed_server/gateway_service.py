# file: gateway_service.py
from fastapi import FastAPI, Body
import zmq
import json

app = FastAPI()

# Giả lập ghi log hoặc file cấu hình khi nhận POST
LOG_FILE = "/tmp/test_results.log"


@app.get("/status")
def read_system_status():
    # Ví dụ: Đọc file uptime của Linux
    with open("/proc/uptime", "r") as f:
        uptime_seconds = f.readline().split()[0]
    return {"status": "running", "uptime": uptime_seconds}


@app.post("/execute-test")
def run_test_command(payload: dict = Body(...)):
    """
    Nhận lệnh POST từ nơi khác để điều khiển board
    Payload ví dụ: {"command": "start_ota", "version": "2.0"}
    """
    command = payload.get("command")

    # 1. Ghi vào file (Triết lý Everything is a file)
    with open(LOG_FILE, "a") as f:
        f.write(f"Received command: {command}\n")

    # 2. Hoặc bắn qua ZMQ cho Microservice khác xử lý
    # context = zmq.Context()
    # socket = context.socket(zmq.PUSH)
    # socket.connect("ipc:///tmp/internal_bus")
    # socket.send_json(payload)

    return {"message": "Command received", "target": command}
