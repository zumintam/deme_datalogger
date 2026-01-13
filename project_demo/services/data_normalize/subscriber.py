"""
Energy Data Subscriber
Nhận dữ liệu tổng hợp từ Router qua ZMQ PUB/SUB
"""

import zmq
import json
import time
from datetime import datetime
import logging

# Cấu hình logging
logging.basicConfig(
    level=logging.INFO, format="%(asctime)s - %(levelname)s - %(message)s"
)
logger = logging.getLogger(__name__)

# ZMQ Endpoint
ZMQ_SUB_ENDPOINT = "ipc:///tmp/energy_summary.ipc"


class EnergyDataSubscriber:
    def __init__(self, endpoint=ZMQ_SUB_ENDPOINT, topics=None):
        """
        Khởi tạo Subscriber

        Args:
            endpoint: ZMQ endpoint để subscribe
            topics: List các topic muốn nhận (None = nhận tất cả)
        """
        self.endpoint = endpoint
        self.topics = topics or ["ENERGY_DATA"]

        # Khởi tạo ZMQ context và socket
        self.context = zmq.Context()
        self.subscriber = self.context.socket(zmq.SUB)

        # Kết nối đến Publisher
        self.subscriber.connect(self.endpoint)
        logger.info(f"📡 Connected to {self.endpoint}")

        # Subscribe các topic
        for topic in self.topics:
            self.subscriber.setsockopt_string(zmq.SUBSCRIBE, topic)
            logger.info(f"📬 Subscribed to topic: {topic}")

        # Thống kê
        self.stats = {
            "messages_received": 0,
            "last_message_time": None,
            "start_time": time.time(),
        }

    def process_energy_data(self, data):
        """
        Xử lý dữ liệu năng lượng nhận được

        Args:
            data: Dictionary chứa dữ liệu năng lượng
        """
        try:
            grid = data.get("grid", {})
            solar = data.get("solar", {})
            control = data.get("control", {})
            system = data.get("system", {})

            # ===== Hiển thị thông tin chính =====
            logger.info("=" * 70)
            logger.info(f"⚡ ENERGY DATA UPDATE - {data.get('timestamp')}")
            logger.info("-" * 70)

            # Grid (Lưới điện)
            logger.info(f"🔌 GRID:")
            logger.info(f"   • Power: {grid.get('power_kw', 0):.2f} kW")
            logger.info(f"   • Reactive: {grid.get('reactive_power_kvar', 0):.2f} kVAr")
            logger.info(f"   • Voltages: {grid.get('voltages_v', [])} V")
            logger.info(f"   • Currents: {grid.get('currents_a', [])} A")
            logger.info(f"   • Frequency: {grid.get('frequency_hz', 0):.2f} Hz")
            logger.info(f"   • Power Factor: {grid.get('power_factor', 0):.3f}")
            logger.info(
                f"   • Today Production: {grid.get('today_production_kwh', 0):.2f} kWh"
            )

            # Solar (Điện mặt trời)
            logger.info(f"☀️  SOLAR:")
            logger.info(
                f"   • Total Power: {solar.get('total_active_power_kw', 0):.2f} kW"
            )
            logger.info(
                f"   • Daily Production: {solar.get('total_daily_production_kwh', 0):.2f} kWh"
            )
            logger.info(f"   • Active Inverters: {solar.get('inverter_count', 0)}")

            # Chi tiết từng inverter
            inverters = solar.get("inverters", [])
            if inverters:
                logger.info(f"   • Inverter Details:")
                for inv in inverters:
                    logger.info(
                        f"     - {inv['id']}: {inv['power_kw']:.2f}kW, "
                        f"{inv['daily_kwh']:.2f}kWh, Status={inv['status']}"
                    )

            # Control System
            logger.info(f"🎛️  CONTROL:")
            logger.info(f"   • Mode: {control.get('mode', 'UNKNOWN')}")

            # System Status
            logger.info(f"📊 SYSTEM:")
            logger.info(f"   • Status: {system.get('status', 'UNKNOWN')}")
            logger.info(f"   • Devices Online: {system.get('devices_online', 0)}")
            logger.info(
                f"   • Messages Processed: {system.get('messages_processed', 0)}"
            )

            # ===== Tính toán thêm =====
            grid_power = grid.get("power_kw", 0)
            solar_power = solar.get("total_active_power_kw", 0)

            # Công suất tiêu thụ = Grid + Solar
            consumption = grid_power + solar_power

            # Tỷ lệ tự cấp (%)
            self_consumption_rate = 0
            if consumption > 0:
                self_consumption_rate = (solar_power / consumption) * 100

            logger.info(f"💡 ANALYSIS:")
            logger.info(f"   • Total Consumption: {consumption:.2f} kW")
            logger.info(f"   • Self-Consumption Rate: {self_consumption_rate:.1f}%")

            if grid_power < 0:
                logger.info(f"   • Export to Grid: {abs(grid_power):.2f} kW 📤")
            elif grid_power > 0:
                logger.info(f"   • Import from Grid: {grid_power:.2f} kW 📥")
            else:
                logger.info(f"   • Zero Export ✅")

            logger.info("=" * 70)

        except Exception as e:
            logger.error(f"Error processing data: {e}", exc_info=True)

    def run(self):
        """Vòng lặp chính để nhận dữ liệu"""
        logger.info("=" * 70)
        logger.info("🎧 Energy Data Subscriber Started")
        logger.info(f"Endpoint: {self.endpoint}")
        logger.info(f"Topics: {', '.join(self.topics)}")
        logger.info("Waiting for data...")
        logger.info("=" * 70)

        while True:
            try:
                # Nhận tin nhắn từ Publisher
                message = self.subscriber.recv_multipart()

                if len(message) < 2:
                    logger.warning("Received malformed message")
                    continue

                topic = message[0].decode("utf-8")
                payload = json.loads(message[1].decode("utf-8"))

                # Cập nhật thống kê
                self.stats["messages_received"] += 1
                self.stats["last_message_time"] = time.time()

                # Xử lý dữ liệu
                logger.info(f"📩 Received message from topic: {topic}")
                self.process_energy_data(payload)

            except KeyboardInterrupt:
                logger.info("\n⚠️  Shutdown requested...")
                break

            except json.JSONDecodeError as e:
                logger.error(f"Invalid JSON: {e}")

            except Exception as e:
                logger.error(f"Unexpected error: {e}", exc_info=True)
                time.sleep(1)

        self.cleanup()

    def cleanup(self):
        """Dọn dẹp khi tắt"""
        logger.info("🧹 Cleaning up...")

        # In thống kê
        uptime = time.time() - self.stats["start_time"]
        logger.info(f"Total messages received: {self.stats['messages_received']}")
        logger.info(f"Uptime: {uptime:.2f} seconds")

        # Đóng socket
        self.subscriber.close()
        self.context.term()

        logger.info("✅ Subscriber stopped successfully")


class EnergyDataLogger(EnergyDataSubscriber):
    """
    Subscriber với chức năng ghi log vào file
    """

    def __init__(
        self, endpoint=ZMQ_SUB_ENDPOINT, topics=None, log_file="energy_data.log"
    ):
        super().__init__(endpoint, topics)
        self.log_file = log_file
        logger.info(f"📝 Logging to file: {log_file}")

    def process_energy_data(self, data):
        """Ghi dữ liệu vào file và hiển thị"""
        # Hiển thị như bình thường
        super().process_energy_data(data)

        # Ghi vào file
        try:
            with open(self.log_file, "a") as f:
                f.write(json.dumps(data) + "\n")
        except Exception as e:
            logger.error(f"Error writing to log file: {e}")


class EnergyDataAnalyzer(EnergyDataSubscriber):
    """
    Subscriber với phân tích dữ liệu thời gian thực
    """

    def __init__(self, endpoint=ZMQ_SUB_ENDPOINT, topics=None):
        super().__init__(endpoint, topics)
        self.data_history = []
        self.max_history = 100  # Giữ 100 mẫu gần nhất

    def process_energy_data(self, data):
        """Phân tích và hiển thị xu hướng"""
        # Lưu vào lịch sử
        self.data_history.append(
            {
                "timestamp": data.get("unix_time"),
                "grid_power": data.get("grid", {}).get("power_kw", 0),
                "solar_power": data.get("solar", {}).get("total_active_power_kw", 0),
            }
        )

        # Giới hạn kích thước lịch sử
        if len(self.data_history) > self.max_history:
            self.data_history.pop(0)

        # Hiển thị thông tin cơ bản
        super().process_energy_data(data)

        # Phân tích xu hướng nếu có đủ dữ liệu
        if len(self.data_history) >= 10:
            recent = self.data_history[-10:]
            avg_grid = sum(d["grid_power"] for d in recent) / len(recent)
            avg_solar = sum(d["solar_power"] for d in recent) / len(recent)

            logger.info(f"📈 TREND (Last 10 samples):")
            logger.info(f"   • Avg Grid Power: {avg_grid:.2f} kW")
            logger.info(f"   • Avg Solar Power: {avg_solar:.2f} kW")
            logger.info("=" * 70)


if __name__ == "__main__":
    import sys

    # Chọn loại subscriber dựa trên tham số
    if len(sys.argv) > 1:
        mode = sys.argv[1]
        if mode == "logger":
            subscriber = EnergyDataLogger()
        elif mode == "analyzer":
            subscriber = EnergyDataAnalyzer()
        else:
            subscriber = EnergyDataSubscriber()
    else:
        subscriber = EnergyDataSubscriber()

    try:
        subscriber.run()
    except Exception as e:
        logger.error(f"Fatal error: {e}", exc_info=True)
