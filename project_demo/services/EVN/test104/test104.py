"""
IEC 60870-5-104 Server with Control Support
Standard: IEC 60870-5-104:2006
Role    : Gateway server với khả năng nhận lệnh điều khiển từ SCADA
"""

import socket
import struct
import threading
import time
from dataclasses import dataclass
from enum import IntEnum
from typing import Dict, Any, Optional, List, Callable

# ============================================================
# IEC 60870-5-104 BASIC DEFINITIONS
# ============================================================


class TypeID(IntEnum):
    M_SP_NA_1 = 1  # Single-point information
    M_ME_NC_1 = 13  # Measured value, short floating point
    M_SP_TB_1 = 30  # Single point with time tag
    C_SC_NA_1 = 45  # Single command
    C_SE_NC_1 = 50  # Set point command, short floating point
    C_IC_NA_1 = 100  # Interrogation command


class CauseOfTransmission(IntEnum):
    CYCLIC = 1
    SPONT = 3
    ACTIVATION = 6
    ACTCON = 7  # Activation confirmation
    ACTTERM = 10  # Activation termination
    INROGEN = 20


class QualityDescriptor:
    GOOD = 0x00
    INVALID = 0x80
    NOT_TOPICAL = 0x40
    OVERFLOW = 0x01


# ============================================================
# DATA POINT DEFINITION :: Luu tru du lieu
# ============================================================


@dataclass
class DataPoint:
    ioa: int
    type_id: TypeID
    value: Any
    timestamp: float
    quality: int = QualityDescriptor.GOOD
    coa: int = 1

    def encode_asdu(self, cause: CauseOfTransmission) -> bytes:
        if self.type_id == TypeID.M_ME_NC_1:  # Loai du lieu de luu tru
            return self._encode_measured_float(cause)
        if self.type_id == TypeID.M_SP_NA_1:
            return self._encode_single_point(cause)
        return b""

    def _encode_measured_float(self, cause: CauseOfTransmission) -> bytes:
        data = struct.pack("<B", self.type_id)
        data += struct.pack("<B", 1)
        data += struct.pack("<B", cause)
        data += struct.pack("<B", 0)
        data += struct.pack("<H", self.coa)
        data += struct.pack("<I", self.ioa)[:3]
        data += struct.pack("<f", float(self.value))
        data += struct.pack("<B", self.quality)
        return data

    def _encode_single_point(self, cause: CauseOfTransmission) -> bytes:
        data = struct.pack("<B", self.type_id)
        data += struct.pack("<B", 1)
        data += struct.pack("<B", cause)
        data += struct.pack("<B", 0)
        data += struct.pack("<H", self.coa)
        data += struct.pack("<I", self.ioa)[:3]
        siq = (1 if self.value else 0) | self.quality
        data += struct.pack("<B", siq)
        return data


# ============================================================
# CONTROL COMMAND STRUCTURES
# ============================================================


@dataclass
class SingleCommand:
    """C_SC_NA_1 - Single Command"""

    ioa: int
    state: bool  # True = ON, False = OFF
    select: bool  # True = Select, False = Execute
    qualifier: int  # QU (Qualifier of Command)

    @classmethod
    def parse(cls, asdu: bytes):
        """Parse C_SC_NA_1 từ ASDU"""
        if len(asdu) < 10:
            return None

        ioa = struct.unpack("<I", asdu[6:9] + b"\x00")[0]
        sco = asdu[9]  # Single Command (SCO)

        state = bool(sco & 0x01)
        select = bool(sco & 0x80)
        qualifier = (sco >> 2) & 0x1F

        return cls(ioa, state, select, qualifier)


@dataclass
class SetPointCommand:
    """C_SE_NC_1 - Set Point Command (Float)"""

    ioa: int
    value: float
    select: bool
    qualifier: int

    @classmethod
    def parse(cls, asdu: bytes):
        """Parse C_SE_NC_1 từ ASDU"""
        if len(asdu) < 14:
            return None

        ioa = struct.unpack("<I", asdu[6:9] + b"\x00")[0]
        value = struct.unpack("<f", asdu[9:13])[0]
        qos = asdu[13]  # Qualifier of Set-point

        select = bool(qos & 0x80)
        qualifier = qos & 0x7F

        return cls(ioa, value, select, qualifier)


# ============================================================
# CONTROL HANDLER CALLBACK
# ============================================================


class ControlHandler:
    """
    Xử lý các lệnh điều khiển từ SCADA
    User implement các callback này
    """

    def on_single_command(self, cmd: SingleCommand) -> bool:
        """
        Xử lý lệnh Single Command (ON/OFF)
        Return True nếu thành công
        """
        print(
            f"🎛️  Single Command | IOA {cmd.ioa} | {'ON' if cmd.state else 'OFF'} | {'SELECT' if cmd.select else 'EXECUTE'}"
        )

        # TODO: User implement logic điều khiển thiết bị
        # Ví dụ: Bật/tắt inverter, breaker, relay...

        return True  # Success

    def on_setpoint_command(self, cmd: SetPointCommand) -> bool:
        """
        Xử lý lệnh Set Point (đặt giá trị)
        Return True nếu thành công
        """
        print(
            f"📊 Set Point Command | IOA {cmd.ioa} | Value={cmd.value:.2f} | {'SELECT' if cmd.select else 'EXECUTE'}"
        )

        # TODO: User implement logic đặt giá trị
        # Ví dụ: Đặt công suất, điều chỉnh tham số...

        return True  # Success


# ============================================================
# DATA CACHE
# ============================================================


class DataCache:
    """In-memory cache với khả năng cập nhật từ control command"""

    def __init__(self):
        self._points: Dict[int, DataPoint] = {}
        self._lock = threading.Lock()
        self._initialize_points()

    def _initialize_points(self):
        default_points = [
            (1000, TypeID.M_ME_NC_1, 0.0),  # Total active power
            (1001, TypeID.M_ME_NC_1, 0.0),  # Total reactive power
            (1002, TypeID.M_ME_NC_1, 0.0),  # Voltage L1
            (1003, TypeID.M_ME_NC_1, 0.0),  # Voltage L2
            (1004, TypeID.M_ME_NC_1, 0.0),  # Voltage L3
            (1005, TypeID.M_ME_NC_1, 0.0),  # Current L1
            (1006, TypeID.M_ME_NC_1, 0.0),  # Current L2
            (1007, TypeID.M_ME_NC_1, 0.0),  # Current L3
            (1008, TypeID.M_ME_NC_1, 50.0),  # Frequency
            (1009, TypeID.M_ME_NC_1, 1.0),  # Power factor
            (2000, TypeID.M_SP_NA_1, True),  # Plant online status
            (3000, TypeID.M_SP_NA_1, False),  # Breaker status (controllable)
            (3001, TypeID.M_SP_NA_1, False),  # Relay 1 status
            (4000, TypeID.M_ME_NC_1, 0.0),  # Power setpoint (controllable)
        ]

        now = time.time()
        for ioa, t, value in default_points:
            self._points[ioa] = DataPoint(ioa, t, value, now)

    def update_from_dict(self, data: Dict[str, Any]):
        mapping = {
            "Total_active_power": 1000,
            "Total_reactive_power": 1001,
            "Voltage_L1": 1002,
            "Voltage_L2": 1003,
            "Voltage_L3": 1004,
            "Current_L1": 1005,
            "Current_L2": 1006,
            "Current_L3": 1007,
            "Frequency": 1008,
            "Power_factor": 1009,
            "status": 2000,
        }

        with self._lock:
            ts = time.time()
            for field, ioa in mapping.items():
                if field in data and ioa in self._points:
                    p = self._points[ioa]
                    p.value = data[field]
                    p.timestamp = ts
                    p.quality = QualityDescriptor.GOOD

    def update_point(self, ioa: int, value: Any):
        """Cập nhật giá trị từ control command"""
        with self._lock:
            if ioa in self._points:
                self._points[ioa].value = value
                self._points[ioa].timestamp = time.time()
                self._points[ioa].quality = QualityDescriptor.GOOD

    def get_all(self) -> List[DataPoint]:
        with self._lock:
            return list(self._points.values())

    def get_point(self, ioa: int) -> Optional[DataPoint]:
        with self._lock:
            return self._points.get(ioa)


# ============================================================
# IEC 104 SERVER WITH CONTROL
# ============================================================


class IEC104Server:
    """
    IEC 60870-5-104 Server với hỗ trợ điều khiển
    """

    def __init__(self, host: str = "0.0.0.0", port: int = 2404):
        self.host = host
        self.port = port
        self.cache = DataCache()
        self.control_handler = ControlHandler()
        self._clients: List[socket.socket] = []
        self._running = False

    def start(self):
        self._running = True
        threading.Thread(target=self._tcp_server, daemon=True).start()
        threading.Thread(target=self._cyclic_transmission, daemon=True).start()
        print(f"✅ IEC-104 Server started on {self.host}:{self.port}")

    def _tcp_server(self):
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((self.host, self.port))
        server.listen(5)

        while self._running:
            try:
                client, addr = server.accept()
                print(f"📡 SCADA connected: {addr}")
                self._clients.append(client)
                threading.Thread(
                    target=self._handle_client, args=(client,), daemon=True
                ).start()
            except Exception as e:
                print(f"❌ Accept error: {e}")

    def _handle_client(self, client: socket.socket):
        try:
            while self._running:
                data = client.recv(1024)
                if not data:
                    break

                if len(data) < 2 or data[0] != 0x68:
                    continue

                control_field = data[2]

                # U-Format (Control frames)
                if (control_field & 0x03) == 0x03:
                    if control_field == 0x07:  # STARTDT
                        print("⚡ Received STARTDT → Send Confirm")
                        client.send(b"\x68\x04\x0b\x00\x00\x00")
                    elif control_field == 0x43:  # TESTFR
                        print("⚡ Received TESTFR → Send Pong")
                        client.send(b"\x68\x04\x83\x00\x00\x00")
                    continue

                # I-Format (Data frames)
                if len(data) > 6:
                    type_id = data[6]

                    # Interrogation command
                    if type_id == TypeID.C_IC_NA_1:
                        print("📥 Received C_IC_NA_1 (Interrogation)")
                        self._send_interrogation_response(client)

                    # Single Command (ON/OFF)
                    elif type_id == TypeID.C_SC_NA_1:
                        print("📥 Received C_SC_NA_1 (Single Command)")
                        self._handle_single_command(client, data[6:])

                    # Set Point Command (Float)
                    elif type_id == TypeID.C_SE_NC_1:
                        print("📥 Received C_SE_NC_1 (Set Point)")
                        self._handle_setpoint_command(client, data[6:])

        except Exception as e:
            print(f"❌ Client handler error: {e}")
            import traceback

            traceback.print_exc()
        finally:
            if client in self._clients:
                self._clients.remove(client)
            client.close()
            print("📡 SCADA disconnected")

    def _handle_single_command(self, client: socket.socket, asdu: bytes):
        """Xử lý lệnh Single Command (C_SC_NA_1)"""
        cmd = SingleCommand.parse(asdu)
        if not cmd:
            return

        # Execute control
        success = self.control_handler.on_single_command(cmd)

        if success:
            # Update cache
            self.cache.update_point(cmd.ioa, cmd.state)

            # Send ACTCON (Activation Confirmation)
            self._send_command_response(
                client, cmd.ioa, TypeID.C_SC_NA_1, cmd.state, CauseOfTransmission.ACTCON
            )

            # Send ACTTERM (Activation Termination)
            time.sleep(0.1)
            self._send_command_response(
                client,
                cmd.ioa,
                TypeID.C_SC_NA_1,
                cmd.state,
                CauseOfTransmission.ACTTERM,
            )

    def _handle_setpoint_command(self, client: socket.socket, asdu: bytes):
        """Xử lý lệnh Set Point (C_SE_NC_1)"""
        cmd = SetPointCommand.parse(asdu)
        if not cmd:
            return

        # Execute control
        success = self.control_handler.on_setpoint_command(cmd)

        if success:
            # Update cache
            self.cache.update_point(cmd.ioa, cmd.value)

            # Send ACTCON
            self._send_setpoint_response(
                client, cmd.ioa, cmd.value, CauseOfTransmission.ACTCON
            )

            # Send ACTTERM
            time.sleep(0.1)
            self._send_setpoint_response(
                client, cmd.ioa, cmd.value, CauseOfTransmission.ACTTERM
            )

    def _send_command_response(
        self,
        client: socket.socket,
        ioa: int,
        type_id: TypeID,
        value: bool,
        cause: CauseOfTransmission,
    ):
        """Gửi response cho Single Command"""
        asdu = struct.pack("<B", type_id)
        asdu += struct.pack("<B", 1)
        asdu += struct.pack("<B", cause)
        asdu += struct.pack("<B", 0)
        asdu += struct.pack("<H", 1)
        asdu += struct.pack("<I", ioa)[:3]
        sco = 1 if value else 0
        asdu += struct.pack("<B", sco)

        length = len(asdu) + 4
        header = struct.pack("<BBBBBB", 0x68, length, 0x00, 0x00, 0x00, 0x00)
        client.send(header + asdu)

    def _send_setpoint_response(
        self, client: socket.socket, ioa: int, value: float, cause: CauseOfTransmission
    ):
        """Gửi response cho Set Point"""
        asdu = struct.pack("<B", TypeID.C_SE_NC_1)
        asdu += struct.pack("<B", 1)
        asdu += struct.pack("<B", cause)
        asdu += struct.pack("<B", 0)
        asdu += struct.pack("<H", 1)
        asdu += struct.pack("<I", ioa)[:3]
        asdu += struct.pack("<f", value)
        asdu += struct.pack("<B", 0)  # QOS

        length = len(asdu) + 4
        header = struct.pack("<BBBBBB", 0x68, length, 0x00, 0x00, 0x00, 0x00)
        client.send(header + asdu)

    def _send_interrogation_response(self, client: socket.socket):
        for point in self.cache.get_all():
            apdu = self._build_apdu(point, CauseOfTransmission.INROGEN)
            client.send(apdu)
            time.sleep(0.01)

    def _cyclic_transmission(self):
        while self._running:
            time.sleep(5)
            for client in list(self._clients):
                for point in self.cache.get_all():
                    apdu = self._build_apdu(point, CauseOfTransmission.CYCLIC)
                    try:
                        client.send(apdu)
                    except:
                        if client in self._clients:
                            self._clients.remove(client)

    def _build_apdu(self, point: DataPoint, cause: CauseOfTransmission) -> bytes:
        asdu = point.encode_asdu(cause)
        length = len(asdu) + 4
        header = struct.pack("<BBBBBB", 0x68, length, 0x00, 0x00, 0x00, 0x00)
        return header + asdu

    def update_data(self, data: Dict[str, Any]):
        self.cache.update_from_dict(data)


# ============================================================
# EXAMPLE USAGE
# ============================================================

if __name__ == "__main__":
    # Khởi tạo server
    server = IEC104Server()

    # Custom control handler (optional)
    class MyControlHandler(ControlHandler):
        def on_single_command(self, cmd: SingleCommand) -> bool:
            print(f"🎛️  CUSTOM: Control IOA {cmd.ioa} → {'ON' if cmd.state else 'OFF'}")

            # TODO: Thực tế gửi lệnh xuống PLC/Inverter/Relay
            # mqtt_client.publish(f"control/{cmd.ioa}", cmd.state)
            # modbus.write_coil(cmd.ioa, cmd.state)

            return True

        def on_setpoint_command(self, cmd: SetPointCommand) -> bool:
            print(f"📊 CUSTOM: Set IOA {cmd.ioa} = {cmd.value:.2f}")

            # TODO: Gửi setpoint xuống thiết bị
            # inverter.set_power_limit(cmd.value)

            return True

    # Gán custom handler
    server.control_handler = MyControlHandler()

    # Start server
    server.start()

    print("\n" + "=" * 60)
    print("🚀 IEC-104 Server with Control Support")
    print("=" * 60)
    print("Listening on port 2404")
    print("\nControllable points:")
    print("  IOA 3000: Breaker (ON/OFF)")
    print("  IOA 3001: Relay 1 (ON/OFF)")
    print("  IOA 4000: Power Setpoint (Float)")
    print("=" * 60 + "\n")

    # Simulate measurement data
    while True:
        sample = {
            "Total_active_power": 480.2,
            "Total_reactive_power": 120.4,
            "Voltage_L1": 385.0,
            "Voltage_L2": 384.6,
            "Voltage_L3": 386.1,
            "Current_L1": 720.3,
            "Current_L2": 719.8,
            "Current_L3": 721.0,
            "Frequency": 50.01,
            "Power_factor": 0.97,
            "status": True,
        }

        server.update_data(sample)
        time.sleep(2)
