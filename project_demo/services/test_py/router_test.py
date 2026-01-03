#!/usr/bin/env python3
"""
Professional ZMQ-MQTT Bridge Service
====================================
Chức năng: Nhận dữ liệu từ ZMQ ROUTER và forward lên MQTT Broker
Thiết kế: Production-ready với error handling, logging, reconnection, health check
"""

import zmq
import paho.mqtt.client as mqtt
import time
import logging
import signal
import sys
import json
from typing import Optional, Dict, Any
from dataclasses import dataclass
from enum import Enum
from threading import Event, Lock


# ============== CONFIGURATION ==============
@dataclass
class MQTTConfig:
    """MQTT Broker Configuration"""

    broker: str = "broker.emqx.io"
    port: int = 1883
    topic: str = "device/modbus/data"
    qos: int = 1  # 0=At most once, 1=At least once, 2=Exactly once
    keepalive: int = 60
    client_id: str = "zmq_mqtt_bridge"
    reconnect_delay: int = 5  # seconds

    # Authentication (nếu cần)
    username: Optional[str] = None
    password: Optional[str] = None


@dataclass
class ZMQConfig:
    """ZMQ Router Configuration"""

    endpoint: str = "ipc:///tmp/modbus_pipeline.ipc"
    recv_timeout: int = 1000  # milliseconds
    high_water_mark: int = 1000  # Giới hạn buffer


class ServiceState(Enum):
    """Trạng thái của service"""

    INITIALIZING = "initializing"
    RUNNING = "running"
    STOPPING = "stopping"
    STOPPED = "stopped"
    ERROR = "error"


# ============== LOGGING SETUP ==============
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
    datefmt="%Y-%m-%d %H:%M:%S",
)
logger = logging.getLogger("ZMQ-MQTT-Bridge")


# ============== MAIN SERVICE CLASS ==============
class ZMQMQTTBridge:
    """
    Professional Bridge Service giữa ZMQ và MQTT

    Features:
    - Auto-reconnection cho MQTT
    - Graceful shutdown
    - Health monitoring
    - Message validation
    - Error recovery
    - Statistics tracking
    """

    def __init__(self, mqtt_config: MQTTConfig, zmq_config: ZMQConfig):
        self.mqtt_config = mqtt_config
        self.zmq_config = zmq_config

        # State management
        self.state = ServiceState.INITIALIZING
        self.shutdown_event = Event()
        self.mqtt_connected = Event()

        # Statistics
        self.stats = {
            "messages_received": 0,
            "messages_sent": 0,
            "messages_failed": 0,
            "mqtt_reconnects": 0,
            "start_time": None,
        }
        self.stats_lock = Lock()

        # Components
        self.mqtt_client: Optional[mqtt.Client] = None
        self.zmq_context: Optional[zmq.Context] = None
        self.zmq_socket: Optional[zmq.Socket] = None

        # Register signal handlers
        signal.signal(signal.SIGINT, self._signal_handler)
        signal.signal(signal.SIGTERM, self._signal_handler)

    # ========== MQTT CALLBACKS ==========
    def _on_mqtt_connect(self, client, userdata, flags, rc):
        """Callback khi MQTT kết nối"""
        if rc == 0:
            logger.info(
                f"MQTT connected to {self.mqtt_config.broker}:{self.mqtt_config.port}"
            )
            self.mqtt_connected.set()
        else:
            error_messages = {
                1: "Incorrect protocol version",
                2: "Invalid client identifier",
                3: "Server unavailable",
                4: "Bad username or password",
                5: "Not authorized",
            }
            logger.error(
                f"MQTT connection failed: {error_messages.get(rc, f'Unknown error ({rc})')}"
            )
            self.mqtt_connected.clear()

    def _on_mqtt_disconnect(self, client, userdata, rc):
        """Callback khi MQTT bị ngắt kết nối"""
        self.mqtt_connected.clear()
        if rc != 0:
            logger.warning(
                f"MQTT disconnected unexpectedly (code: {rc}), attempting reconnect..."
            )
            with self.stats_lock:
                self.stats["mqtt_reconnects"] += 1

    def _on_mqtt_publish(self, client, userdata, mid):
        """Callback khi publish thành công"""
        logger.debug(f"MQTT message {mid} published successfully")

    # ========== INITIALIZATION ==========
    def _init_mqtt(self):
        """Khởi tạo MQTT client với đầy đủ cấu hình"""
        logger.info("Initializing MQTT client...")

        self.mqtt_client = mqtt.Client(client_id=self.mqtt_config.client_id)

        # Set callbacks
        self.mqtt_client.on_connect = self._on_mqtt_connect
        self.mqtt_client.on_disconnect = self._on_mqtt_disconnect
        self.mqtt_client.on_publish = self._on_mqtt_publish

        # Set authentication nếu có
        if self.mqtt_config.username and self.mqtt_config.password:
            self.mqtt_client.username_pw_set(
                self.mqtt_config.username, self.mqtt_config.password
            )

        # Configure reconnection
        self.mqtt_client.reconnect_delay_set(
            min_delay=1, max_delay=self.mqtt_config.reconnect_delay
        )

        # Connect
        try:
            self.mqtt_client.connect(
                self.mqtt_config.broker,
                self.mqtt_config.port,
                self.mqtt_config.keepalive,
            )
            self.mqtt_client.loop_start()

            # Đợi kết nối thành công (timeout 10s)
            if not self.mqtt_connected.wait(timeout=10):
                raise TimeoutError("MQTT connection timeout")

        except Exception as e:
            logger.error(f"Failed to initialize MQTT: {e}")
            raise

    def _init_zmq(self):
        """Khởi tạo ZMQ router"""
        logger.info("Initializing ZMQ router...")

        try:
            self.zmq_context = zmq.Context()
            self.zmq_socket = self.zmq_context.socket(zmq.ROUTER)

            # Set socket options
            self.zmq_socket.setsockopt(zmq.RCVTIMEO, self.zmq_config.recv_timeout)
            self.zmq_socket.setsockopt(zmq.SNDHWM, self.zmq_config.high_water_mark)
            self.zmq_socket.setsockopt(zmq.RCVHWM, self.zmq_config.high_water_mark)

            # Bind
            self.zmq_socket.bind(self.zmq_config.endpoint)
            logger.info(f"ZMQ ROUTER listening on {self.zmq_config.endpoint}")

        except Exception as e:
            logger.error(f"Failed to initialize ZMQ: {e}")
            raise

    # ========== MESSAGE PROCESSING ==========
    def _validate_message(self, parts: list) -> bool:
        """Validate ZMQ message format"""
        if len(parts) != 2:
            logger.warning(
                f"Invalid message format (expected 2 parts, got {len(parts)})"
            )
            return False
        return True

    def _process_message(self, identity: bytes, payload: bytes) -> bool:
        """
        Xử lý message từ ZMQ và gửi lên MQTT

        Returns:
            True nếu thành công, False nếu thất bại
        """
        try:
            # Decode payload
            message_str = payload.decode("utf-8")

            # Log (có thể parse JSON nếu cần)
            logger.info(
                f"[ZMQ] Client: {identity.hex()[:8]}... | Data: {message_str[:100]}"
            )

            # Đợi MQTT kết nối (nếu đang reconnect)
            if not self.mqtt_connected.wait(timeout=5):
                logger.error("MQTT not connected, dropping message")
                return False

            # Publish to MQTT
            result = self.mqtt_client.publish(
                self.mqtt_config.topic, message_str, qos=self.mqtt_config.qos
            )

            # Đợi xác nhận (với timeout)
            result.wait_for_publish(timeout=2)

            if result.rc == mqtt.MQTT_ERR_SUCCESS:
                logger.info(f"[MQTT] Published to '{self.mqtt_config.topic}'")
                with self.stats_lock:
                    self.stats["messages_sent"] += 1
                return True
            else:
                logger.error(f"MQTT publish failed: {result.rc}")
                return False

        except UnicodeDecodeError:
            logger.error("Failed to decode message payload")
            return False
        except Exception as e:
            logger.error(f"Error processing message: {e}")
            return False

    # ========== MAIN LOOP ==========
    def run(self):
        """Main service loop"""
        try:
            # Initialize components
            self._init_mqtt()
            self._init_zmq()

            self.state = ServiceState.RUNNING
            self.stats["start_time"] = time.time()
            logger.info("=" * 60)
            logger.info("ZMQ-MQTT Bridge Service is RUNNING")
            logger.info("=" * 60)

            while not self.shutdown_event.is_set():
                try:
                    # Receive message from ZMQ (non-blocking với timeout)
                    parts = self.zmq_socket.recv_multipart()

                    with self.stats_lock:
                        self.stats["messages_received"] += 1

                    # Validate
                    if not self._validate_message(parts):
                        with self.stats_lock:
                            self.stats["messages_failed"] += 1
                        continue

                    identity, payload = parts

                    # Process
                    if not self._process_message(identity, payload):
                        with self.stats_lock:
                            self.stats["messages_failed"] += 1

                except zmq.Again:
                    # Timeout - không có message, tiếp tục loop
                    continue

                except Exception as e:
                    logger.error(f"Error in main loop: {e}")
                    with self.stats_lock:
                        self.stats["messages_failed"] += 1
                    time.sleep(0.1)  # Tránh tight loop khi lỗi

        except Exception as e:
            logger.critical(f"Fatal error: {e}")
            self.state = ServiceState.ERROR
            raise
        finally:
            self._cleanup()

    # ========== SHUTDOWN & CLEANUP ==========
    def _signal_handler(self, signum, frame):
        """Handle shutdown signals"""
        logger.info(f"\nReceived signal {signum}, shutting down gracefully...")
        self.shutdown_event.set()

    def _cleanup(self):
        """Clean up resources"""
        self.state = ServiceState.STOPPING
        logger.info("Cleaning up resources...")

        # Stop MQTT
        if self.mqtt_client:
            self.mqtt_client.loop_stop()
            self.mqtt_client.disconnect()
            logger.info("MQTT client disconnected")

        # Close ZMQ
        if self.zmq_socket:
            self.zmq_socket.close()
        if self.zmq_context:
            self.zmq_context.term()
            logger.info("ZMQ resources released")

        # Print statistics
        self._print_statistics()

        self.state = ServiceState.STOPPED
        logger.info("Service stopped")

    def _print_statistics(self):
        """In thống kê hoạt động"""
        with self.stats_lock:
            uptime = (
                time.time() - self.stats["start_time"]
                if self.stats["start_time"]
                else 0
            )

            logger.info("=" * 60)
            logger.info("SERVICE STATISTICS")
            logger.info(f"  Uptime: {uptime:.2f}s")
            logger.info(f"  Messages received: {self.stats['messages_received']}")
            logger.info(f"  Messages sent: {self.stats['messages_sent']}")
            logger.info(f"  Messages failed: {self.stats['messages_failed']}")
            logger.info(f"  MQTT reconnects: {self.stats['mqtt_reconnects']}")
            if uptime > 0:
                logger.info(
                    f"  Throughput: {self.stats['messages_received']/uptime:.2f} msg/s"
                )
            logger.info("=" * 60)


# ============== ENTRY POINT ==============
def main():
    """Main entry point"""

    # Configuration
    mqtt_config = MQTTConfig(
        broker="broker.emqx.io",  # Thay bằng broker của bạn
        port=1883,
        topic="device/modbus/data",
        qos=1,
        client_id="professional_zmq_bridge",
    )

    zmq_config = ZMQConfig(endpoint="ipc:///tmp/modbus_pipeline.ipc", recv_timeout=1000)

    # Create and run service
    bridge = ZMQMQTTBridge(mqtt_config, zmq_config)

    try:
        bridge.run()
    except KeyboardInterrupt:
        logger.info("\nInterrupted by user")
    except Exception as e:
        logger.critical(f"Service crashed: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()
