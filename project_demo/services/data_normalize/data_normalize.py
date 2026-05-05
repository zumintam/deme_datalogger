"""
Energy Management Router with Data Aggregation and Publishing
Nhận dữ liệu từ Meter (DPM380) và Inverter, tổng hợp và publish ra topic
"""

import zmq
import json
import time
from datetime import datetime
from collections import defaultdict
import logging
import os

# Cấu hình logging
logging.basicConfig(
    level=logging.INFO, format="%(asctime)s - %(levelname)s - %(message)s"
)
logger = logging.getLogger(__name__)

# ZMQ Endpoints
ZMQ_ROUTER_ENDPOINT = "ipc:///tmp/modbus_pipeline.ipc"
ZMQ_PUB_ENDPOINT = "ipc:///tmp/energy_summary.ipc"  # Topic để publish data tổng hợp

# Publishing interval
PUBLISH_INTERVAL = 2  # seconds


class EnergyManagementRouter:
    def __init__(
        self, router_endpoint=ZMQ_ROUTER_ENDPOINT, pub_endpoint=ZMQ_PUB_ENDPOINT
    ):

        self.router_endpoint = router_endpoint
        self.pub_endpoint = pub_endpoint

        # Đảm bảo thư mục IPC tồn tại
        for endpoint in [router_endpoint, pub_endpoint]:
            ipc_dir = os.path.dirname(endpoint.replace("ipc://", ""))
            if ipc_dir and not os.path.exists(ipc_dir):
                os.makedirs(ipc_dir, exist_ok=True)

            # Xóa file IPC cũ nếu tồn tại
            ipc_file = endpoint.replace("ipc://", "")
            if os.path.exists(ipc_file):
                os.remove(ipc_file)
                logger.info(f"Removed stale IPC file: {ipc_file}")

        # Khởi tạo ZMQ context
        self.context = zmq.Context()

        # ===== ROUTER Socket - Nhận dữ liệu từ các DEALER =====
        self.zmq_router = self.context.socket(zmq.ROUTER)
        self.zmq_router.setsockopt(zmq.LINGER, 1000)
        self.zmq_router.setsockopt(zmq.RCVTIMEO, 1000)  # 1s timeout
        self.zmq_router.setsockopt(zmq.SNDHWM, 1000)
        self.zmq_router.setsockopt(zmq.RCVHWM, 1000)
        self.zmq_router.bind(self.router_endpoint)
        logger.info(f"ROUTER bound to {self.router_endpoint}")

        # ===== PUB Socket - Publish dữ liệu tổng hợp =====
        self.zmq_pub = self.context.socket(zmq.PUB)
        self.zmq_pub.bind(self.pub_endpoint)
        logger.info(f"PUBLISHER bound to {self.pub_endpoint}")

        # Lưu trữ trạng thái hệ thống
        self.meter_data = {}  # {meter_id: data}
        self.inverter_energy = {}  # {inv_id: data}
        self.control_data = {}  # {control_id: data}
        self.device_registry = {}  # Theo dõi tất cả thiết bị

        # Thống kê
        self.stats = {
            "messages_received": 0,
            "messages_published": 0,
            "errors": 0,
            "last_meter_update": None,
            "last_publish_time": 0,
            "active_inverters": set(),
            "start_time": time.time(),
        }

    def handle_meter(self, device_id, data):
        """
        Xử lý dữ liệu từ Meter DPM380
        Mapping theo file: Modbus Address 30023-30027
        """
        try:
            meter_summary = {
                "device_id": device_id,
                "timestamp": time.time(),
                "datetime": datetime.now().isoformat(),
                # Điện áp 3 pha (V)
                "u_abc": [
                    data.get("Voltage_L1", 0),
                    data.get("Voltage_L2", 0),
                    data.get("Voltage_L3", 0),
                ],
                # Dòng điện 3 pha (A)
                "i_abc": [
                    data.get("Current_L1", 0),
                    data.get("Current_L2", 0),
                    data.get("Current_L3", 0),
                ],
                # Công suất (kW, kVAr, kVA)
                "p_total": data.get("Total_active_power", 0),  # Modbus 30023
                "q_total": data.get("Total_reactive_power", 0),
                "s_total": data.get("Total_apparent_power", 0),
                # Sản lượng trong ngày (kWh)
                "today_production": data.get(
                    "Total_today_production", 0
                ),  # Modbus 30027
                # Tần số và hệ số công suất
                "freq": data.get("Frequency", 0),
                "pf": data.get("Total_power_factor", 0),
            }

            self.meter_data[device_id] = meter_summary
            self.stats["last_meter_update"] = time.time()

            logger.info(
                f"METER [{device_id}]: P={meter_summary['p_total']:.2f}kW, "
                f"Daily={meter_summary['today_production']:.2f}kWh, "
                f"PF={meter_summary['pf']:.3f}"
            )

            # Kiểm tra bất thường
            self._check_meter_anomalies(meter_summary)

            return meter_summary

        except Exception as e:
            logger.error(f"Error handling meter data: {e}")
            return None

    def handle_inverter(self, device_id, data):
        """
        Xử lý dữ liệu từ Inverter
        Mapping theo file: Modbus Address 31128 (Active power), 31124 (Today production)
        """
        try:
            # Modbus 31128: Active power
            active_power = data.get("Active_power", 0)

            # Modbus 31124: Today production
            today_production = data.get("Today_production", 0)

            inverter_info = {
                "device_id": device_id,
                "timestamp": time.time(),
                "datetime": datetime.now().isoformat(),
                # Công suất tức thời (kW)
                "active_power": active_power,
                # Sản lượng trong ngày (kWh)
                "today_production": today_production,
                # Thông tin DC
                "dc_voltage": data.get("DC_voltage", 0),
                "dc_current": data.get("DC_current", 0),
                # Trạng thái
                "status": data.get("status", "unknown"),
            }

            self.inverter_energy[device_id] = inverter_info
            self.stats["active_inverters"].add(device_id)

            logger.info(
                f"INVERTER [{device_id}]: Power={active_power:.2f}kW, "
                f"Daily={today_production:.2f}kWh"
            )

            return inverter_info

        except Exception as e:
            logger.error(f"Error handling inverter data: {e}")
            return None

    def handle_control(self, device_id, data):
        """
        Xử lý dữ liệu từ Control System
        Mapping: Modbus 40002 (Active adjustment mode)
        """
        try:
            control_info = {
                "device_id": device_id,
                "timestamp": time.time(),
                "datetime": datetime.now().isoformat(),
                # Modbus 40002: Chế độ điều chỉnh đang hoạt động
                "active_adjustment_mode": data.get("Active_adjustment_mode", 0),
                # Các chế độ có thể: 0=Off, 1=Zero Export, 2=Peak Shaving, etc.
                "mode_name": self._get_mode_name(data.get("Active_adjustment_mode", 0)),
            }

            self.control_data[device_id] = control_info

            logger.info(f"CONTROL [{device_id}]: Mode={control_info['mode_name']}")

            return control_info

        except Exception as e:
            logger.error(f"Error handling control data: {e}")
            return None

    def _get_mode_name(self, mode_code):
        """Chuyển mã chế độ thành tên"""
        mode_map = {
            0: "OFF",
            1: "ZERO_EXPORT",
            2: "PEAK_SHAVING",
            3: "LOAD_FOLLOWING",
            4: "MANUAL",
        }
        return mode_map.get(mode_code, f"UNKNOWN_{mode_code}")

    def _check_meter_anomalies(self, meter_summary):
        """Kiểm tra các bất thường trong dữ liệu meter"""
        # Kiểm tra mất cân bằng điện áp
        voltages = meter_summary["u_abc"]
        if voltages and max(voltages) > 0:
            avg_voltage = sum(voltages) / len(voltages)
            max_deviation = max(abs(v - avg_voltage) for v in voltages)
            if avg_voltage > 0 and max_deviation / avg_voltage > 0.05:
                logger.warning(f"Voltage imbalance: {voltages}")

        # Kiểm tra hệ số công suất thấp
        pf = meter_summary["pf"]
        if 0 < pf < 0.85:
            logger.warning(f"Low power factor: {pf:.3f}")

        # Kiểm tra tần số (hệ thống 50Hz)
        freq = meter_summary["freq"]
        if freq > 0 and (freq < 49.5 or freq > 50.5):
            logger.warning(f"Frequency out of range: {freq:.2f}Hz")

    def publish_aggregated_data(self):
        """
        Tổng hợp dữ liệu từ Meter, Inverter, Control và publish ra topic ZMQ
        """
        try:
            # ===== Lấy dữ liệu Meter mới nhất =====
            latest_meter = {}
            if self.meter_data:
                meter_id = list(self.meter_data.keys())[-1]
                latest_meter = self.meter_data[meter_id]

            # ===== Tính toán tổng từ các Inverter =====
            total_inv_active_power = sum(
                inv["active_power"] for inv in self.inverter_energy.values()
            )
            total_inv_daily_production = sum(
                inv["today_production"] for inv in self.inverter_energy.values()
            )

            # Chi tiết từng inverter
            inverter_details = [
                {
                    "id": inv_id,
                    "power_kw": inv["active_power"],
                    "daily_kwh": inv["today_production"],
                    "status": inv["status"],
                }
                for inv_id, inv in self.inverter_energy.items()
            ]

            # ===== Lấy thông tin Control =====
            control_mode = "UNKNOWN"
            if self.control_data:
                control_id = list(self.control_data.keys())[-1]
                control_mode = self.control_data[control_id]["mode_name"]

            # ===== Tạo Payload tổng hợp =====
            summary_payload = {
                "timestamp": datetime.now().isoformat(),
                "unix_time": time.time(),
                # Dữ liệu từ Meter (Grid)
                "grid": {
                    "power_kw": latest_meter.get("p_total", 0),
                    "reactive_power_kvar": latest_meter.get("q_total", 0),
                    "apparent_power_kva": latest_meter.get("s_total", 0),
                    "voltages_v": latest_meter.get("u_abc", [0, 0, 0]),
                    "currents_a": latest_meter.get("i_abc", [0, 0, 0]),
                    "frequency_hz": latest_meter.get("freq", 0),
                    "power_factor": latest_meter.get("pf", 0),
                    "today_production_kwh": latest_meter.get("today_production", 0),
                },
                # Dữ liệu tổng hợp từ Inverters (Solar)
                "solar": {
                    "total_active_power_kw": round(total_inv_active_power, 2),
                    "total_daily_production_kwh": round(total_inv_daily_production, 2),
                    "inverter_count": len(self.stats["active_inverters"]),
                    "inverters": inverter_details,
                },
                # Thông tin Control System
                "control": {"mode": control_mode},
                # Trạng thái hệ thống
                "system": {
                    "status": self._get_system_status(),
                    "devices_online": len(self.device_registry),
                    "messages_processed": self.stats["messages_received"],
                },
            }

            # ===== Gửi ra ZMQ Topic "ENERGY_DATA" =====
            topic = "ENERGY_DATA"
            self.zmq_pub.send_multipart(
                [topic.encode("utf-8"), json.dumps(summary_payload).encode("utf-8")]
            )

            self.stats["messages_published"] += 1
            self.stats["last_publish_time"] = time.time()

            logger.info(
                f"📤 PUBLISHED [{topic}]: Grid={summary_payload['grid']['power_kw']:.1f}kW, "
                f"Solar={summary_payload['solar']['total_active_power_kw']:.1f}kW"
            )

            return summary_payload

        except Exception as e:
            logger.error(f"Error publishing aggregated data: {e}", exc_info=True)
            return None

    def _get_system_status(self):
        """Xác định trạng thái tổng thể của hệ thống"""
        if not self.meter_data:
            return "NO_METER_DATA"

        if not self.inverter_energy:
            return "NO_INVERTER_DATA"

        # Kiểm tra xem có dữ liệu mới không (trong 10s)
        current_time = time.time()
        if self.stats["last_meter_update"]:
            if current_time - self.stats["last_meter_update"] > 10:
                return "METER_TIMEOUT"

        return "NORMAL"

    def get_system_summary(self):
        """Tạo báo cáo tổng quan hệ thống"""
        total_inv_power = sum(
            inv["active_power"] for inv in self.inverter_energy.values()
        )

        total_inv_daily = sum(
            inv["today_production"] for inv in self.inverter_energy.values()
        )

        meter_power = 0
        if self.meter_data:
            latest_meter = list(self.meter_data.values())[-1]
            meter_power = latest_meter.get("p_total", 0)

        uptime = time.time() - self.stats["start_time"]

        return {
            "timestamp": datetime.now().isoformat(),
            "uptime_seconds": round(uptime, 2),
            "grid_power_kw": round(meter_power, 2),
            "solar_power_kw": round(total_inv_power, 2),
            "solar_daily_kwh": round(total_inv_daily, 2),
            "active_inverters": len(self.stats["active_inverters"]),
            "registered_devices": len(self.device_registry),
            "messages_received": self.stats["messages_received"],
            "messages_published": self.stats["messages_published"],
            "errors": self.stats["errors"],
            "system_status": self._get_system_status(),
        }

    def process_message(self, dealer_id, payload):
        """Xử lý tin nhắn đến và định tuyến đến handler phù hợp"""
        device_id = payload.get("device_id", "unknown")
        data = payload.get("data", {})

        # Đăng ký thiết bị lần đầu
        if device_id not in self.device_registry:
            self.device_registry[device_id] = {
                "first_seen": time.time(),
                "model": data.get("device_model", "unknown"),
                "message_count": 0,
            }
            logger.info(
                f"✨ New device: {device_id} [{data.get('device_model', 'unknown')}]"
            )

        self.device_registry[device_id]["message_count"] += 1
        self.device_registry[device_id]["last_seen"] = time.time()

        # Định tuyến dựa trên loại thiết bị
        device_model = data.get("device_model", "")

        if device_model == "DPM380":
            result = self.handle_meter(device_id, data)
        elif device_model == "CONTROL":
            result = self.handle_control(device_id, data)
        else:
            result = self.handle_inverter(device_id, data)

        self.stats["messages_received"] += 1

        # Chuẩn bị phản hồi
        response = {
            "status": "ACK",
            "timestamp": time.time(),
            "device_id": device_id,
            "processed": result is not None,
        }

        return response

    def run(self):
        """Vòng lặp chính"""
        logger.info("=" * 70)
        logger.info("🚀 Energy Management Router Started")
        logger.info(f"📥 ROUTER: {self.router_endpoint}")
        logger.info(f"📤 PUBLISHER: {self.pub_endpoint}")
        logger.info(f"⏱️  Publish Interval: {PUBLISH_INTERVAL}s")
        logger.info("=" * 70)

        last_summary_time = time.time()
        last_publish_time = time.time()
        summary_interval = 60  # Print summary mỗi 60s

        while True:
            try:
                # Nhận tin nhắn với timeout
                try:
                    msg_frames = self.zmq_router.recv_multipart()

                    if len(msg_frames) < 2:
                        logger.warning("Malformed message received")
                        continue

                    dealer_id = msg_frames[0]
                    payload = json.loads(msg_frames[1].decode("utf-8"))

                    # Xử lý tin nhắn
                    response = self.process_message(dealer_id, payload)

                    # Gửi ACK
                    self.zmq_router.send_multipart(
                        [dealer_id, json.dumps(response).encode("utf-8")]
                    )

                except zmq.Again:
                    # Timeout - không có tin nhắn
                    pass

                # ===== PUBLISH dữ liệu tổng hợp theo chu kỳ =====
                current_time = time.time()
                if current_time - last_publish_time >= PUBLISH_INTERVAL:
                    self.publish_aggregated_data()
                    last_publish_time = current_time

                # ===== In báo cáo hệ thống định kỳ =====
                if current_time - last_summary_time >= summary_interval:
                    summary = self.get_system_summary()
                    logger.info("=" * 70)
                    logger.info("📊 SYSTEM SUMMARY:")
                    logger.info(json.dumps(summary, indent=2))
                    logger.info("=" * 70)
                    last_summary_time = current_time

            except json.JSONDecodeError as e:
                logger.error(f"Invalid JSON: {e}")
                self.stats["errors"] += 1

            except KeyboardInterrupt:
                logger.info("\n⚠️  Shutdown requested...")
                break

            except Exception as e:
                logger.error(f"Unexpected error: {e}", exc_info=True)
                self.stats["errors"] += 1
                time.sleep(1)

        self.cleanup()

    def cleanup(self):
        """Dọn dẹp khi tắt"""
        logger.info("🧹 Cleaning up...")

        # In báo cáo cuối cùng
        final_summary = self.get_system_summary()
        logger.info("Final Summary:")
        logger.info(json.dumps(final_summary, indent=2))

        # Đóng socket
        self.zmq_router.close()
        self.zmq_pub.close()
        self.context.term()

        # Xóa file IPC
        for endpoint in [self.router_endpoint, self.pub_endpoint]:
            ipc_file = endpoint.replace("ipc://", "")
            if os.path.exists(ipc_file):
                os.remove(ipc_file)
                logger.info(f"Removed: {ipc_file}")

        logger.info("✅ Router stopped successfully")


if __name__ == "__main__":
    try:
        server = EnergyManagementRouter()
        server.run()
    except Exception as e:
        logger.error(f"Fatal error: {e}", exc_info=True)
