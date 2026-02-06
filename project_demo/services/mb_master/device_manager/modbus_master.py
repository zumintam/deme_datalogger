# python3 config_creator.py \
#   --id 2 \
#   --name dpm380_mapping \
#   --type meter \
#   --no-notify


# -*- coding: utf-8 -*-
import threading
import time
import zmq
import json

CONFIG_FILE = "devices.json"
ZMQ_REP_PORT = 5557

# Global config (shared between threads)
config_lock = threading.Lock()
current_config = {"devices": []}


def load_config_from_file():
    """Load config from file"""
    global current_config
    try:
        with open(CONFIG_FILE, "r") as f:
            data = json.load(f)
        with config_lock:
            current_config = data
        print("[Config] Loaded {} devices".format(len(data["devices"])))
        return data
    except Exception as e:
        print("[Config] Error loading: {}".format(e))
        return {"devices": []}


def modbus_read_thread():
    """Thread 1: Read Modbus devices"""
    while True:
        with config_lock:
            devices = current_config.get("devices", [])

        print("[Modbus] Reading {} devices...".format(len(devices)))
        for dev in devices:
            print("  Reading: {} ({})".format(dev["name"], dev["id"]))

        time.sleep(5)


def data_process_thread():
    """Thread 2: Process data"""
    while True:
        print("[Process] Processing data...")
        time.sleep(3)


def config_listener_thread():
    """Thread 3: Listen for config reload commands (REP socket)"""
    context = zmq.Context()
    responder = context.socket(zmq.REP)
    responder.bind("tcp://*:{}".format(ZMQ_REP_PORT))

    print("[Listener] REP socket listening on port {}".format(ZMQ_REP_PORT))

    while True:
        try:
            # Wait for request
            request = responder.recv_json()
            print("[Listener] Received: {}".format(request))

            if request.get("command") == "RELOAD_CONFIG":
                # Reload config
                new_config = load_config_from_file()

                # Send ACK
                responder.send_json(
                    {
                        "status": "OK",
                        "message": "Reloaded {} devices".format(
                            len(new_config.get("devices", []))
                        ),
                    }
                )
            else:
                responder.send_json({"status": "ERROR", "message": "Unknown command"})

        except Exception as e:
            print("[Listener] Error: {}".format(e))
            try:
                responder.send_json({"status": "ERROR", "message": str(e)})
            except:
                pass


def main():
    # Load initial config
    load_config_from_file()

    # Start threads
    threads = [
        threading.Thread(target=modbus_read_thread, name="ModbusRead"),
        threading.Thread(target=data_process_thread, name="DataProcess"),
        threading.Thread(target=config_listener_thread, name="ConfigListener"),
    ]

    for t in threads:
        t.daemon = True
        t.start()
        print("[Main] Started thread: {}".format(t.name))

    # Keep main thread alive
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\n[Main] Shutting down...")


if __name__ == "__main__":
    main()
