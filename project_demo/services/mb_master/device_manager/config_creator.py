# -*- coding: utf-8 -*-
import json
import os
import argparse
import zmq
import time

DEFAULT_CONFIG_FILE = "devices.json"
ZMQ_REP_PORT = 5557
ZMQ_TIMEOUT = 5000  # 5 seconds


class DeviceConfigManager:
    def __init__(self, config_file=DEFAULT_CONFIG_FILE, enable_notify=True):
        self.config_file = config_file
        self.enable_notify = enable_notify

        if enable_notify:
            self.context = zmq.Context()
            self.requester = self.context.socket(zmq.REQ)
            self.requester.connect("tcp://localhost:{}".format(ZMQ_REP_PORT))
            self.requester.setsockopt(zmq.RCVTIMEO, ZMQ_TIMEOUT)

    def notify_reload(self):
        """Send reload request and wait for ACK"""
        if not self.enable_notify:
            return True

        try:
            print("Sending reload request...")
            self.requester.send_json(
                {"command": "RELOAD_CONFIG", "file": self.config_file}
            )

            reply = self.requester.recv_json()

            if reply.get("status") == "OK":
                print("Config reloaded successfully: {}".format(reply.get("message")))
                return True
            else:
                print("Reload failed: {}".format(reply.get("message")))
                return False

        except zmq.Again:
            print("Timeout: No response from modbus_master")
            return False
        except Exception as e:
            print("Error: {}".format(e))
            return False

    def load_config(self):
        """Load config from file"""
        if os.path.exists(self.config_file):
            try:
                with open(self.config_file, "r") as f:
                    return json.load(f)
            except ValueError:
                print("Warning: Invalid JSON format")
                return {"devices": []}
        return {"devices": []}

    def save_config(self, data):
        """Save config to file"""
        with open(self.config_file, "w") as f:
            json.dump(data, f, indent=4, ensure_ascii=False)

    def add_or_update_device(self, device_id, device_name, device_type, **kwargs):
        """Add or update device"""
        config = self.load_config()

        # Check if device exists
        for dev in config["devices"]:
            if dev["id"] == device_id:
                dev["name"] = device_name
                dev["type"] = device_type
                dev.update(kwargs)
                print("Updated device: {}".format(device_id))
                self.save_config(config)
                self.notify_reload()
                return

        # Add new device
        new_device = {"id": device_id, "name": device_name, "type": device_type}
        new_device.update(kwargs)
        config["devices"].append(new_device)
        print("Added new device: {}".format(device_id))
        self.save_config(config)
        self.notify_reload()

    def remove_device(self, device_id):
        """Remove device by ID"""
        config = self.load_config()
        original_len = len(config["devices"])
        config["devices"] = [d for d in config["devices"] if d["id"] != device_id]

        if len(config["devices"]) < original_len:
            self.save_config(config)
            print("Removed device: {}".format(device_id))
            self.notify_reload()
            return True
        print("Device not found: {}".format(device_id))
        return False

    def list_devices(self):
        """List all devices"""
        config = self.load_config()
        if not config["devices"]:
            print("No devices found")
            return

        print("Total devices: {}".format(len(config["devices"])))
        for dev in config["devices"]:
            print("  {:<15} -> {}".format(dev["id"], dev["name"]))


def interactive_mode():
    """Interactive input mode"""
    manager = DeviceConfigManager()
    print("=== Device Config Creator ===")

    try:
        device_id = raw_input("Enter device ID   : ").strip()
        device_name = raw_input("Enter device name : ").strip()
        device_type = raw_input("Enter device type : ").strip()
    except NameError:
        device_id = input("Enter device ID   : ").strip()
        device_name = input("Enter device name : ").strip()
        device_type = input("Enter device type : ").strip()

    if not device_id or not device_name:
        print("Error: Device ID and name must not be empty")
        return

    manager.add_or_update_device(device_id, device_name, device_type)
    print("Config saved to {}".format(manager.config_file))


def main():
    parser = argparse.ArgumentParser(description="Device Config Manager")
    parser.add_argument("--id", help="Device ID")
    parser.add_argument("--name", help="Device name")
    parser.add_argument("--type", help="Device type")
    parser.add_argument("--list", action="store_true", help="List all devices")
    parser.add_argument("--remove", help="Remove device by ID")
    parser.add_argument("--file", default=DEFAULT_CONFIG_FILE, help="Config file")
    parser.add_argument(
        "--no-notify", action="store_true", help="Disable ZMQ notification"
    )

    args = parser.parse_args()
    manager = DeviceConfigManager(args.file, enable_notify=not args.no_notify)

    if args.list:
        manager.list_devices()
    elif args.remove:
        manager.remove_device(args.remove)
    elif args.id and args.name:
        kwargs = {}
        if args.type:
            kwargs["type"] = args.type
        manager.add_or_update_device(args.id, args.name, **kwargs)
    else:
        interactive_mode()


if __name__ == "__main__":
    main()
