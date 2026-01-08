import socket
import struct
import time

HOST = "localhost"
PORT = 2404
COA = 1


# ============================================================
# IEC104 BASIC FRAMES
# ============================================================


def send_startdt(sock):
    # STARTDT ACT
    sock.send(b"\x68\x04\x07\x00\x00\x00")
    print("➡ STARTDT ACT")
    resp = sock.recv(1024)
    print("⬅ STARTDT CON:", resp.hex())


def send_interrogation(sock):
    """
    C_IC_NA_1 – Interrogation command
    """
    asdu = struct.pack("<B", 100)  # Type ID = C_IC_NA_1
    asdu += struct.pack("<B", 1)  # VSQ
    asdu += struct.pack("<B", 6)  # COT = ACTIVATION
    asdu += struct.pack("<B", 0)  # Originator
    asdu += struct.pack("<H", COA)  # COA
    asdu += struct.pack("<I", 0)[:3]  # IOA = 0
    asdu += struct.pack("<B", 20)  # QOI = Global interrogation

    send_i_frame(sock, asdu)
    print("➡ Sent INTERROGATION")


def send_setpoint(sock, ioa: int, value: float):
    """
    C_SE_NC_1 – Setpoint, short float
    """
    asdu = struct.pack("<B", 50)  # Type ID = C_SE_NC_1
    asdu += struct.pack("<B", 1)
    asdu += struct.pack("<B", 6)  # COT = ACTIVATION
    asdu += struct.pack("<B", 0)
    asdu += struct.pack("<H", COA)
    asdu += struct.pack("<I", ioa)[:3]
    asdu += struct.pack("<f", value)
    asdu += struct.pack("<B", 0)  # QOS

    send_i_frame(sock, asdu)
    print(f"➡ Sent SETPOINT IOA={ioa}, value={value}")


def send_i_frame(sock, asdu: bytes):
    length = len(asdu) + 4
    apdu = struct.pack("<BBHH", 0x68, length, 0, 0) + asdu
    sock.send(apdu)


def recv_loop(sock, timeout=3):
    sock.settimeout(timeout)
    try:
        while True:
            data = sock.recv(1024)
            if not data:
                break
            print("⬅ RX:", data.hex())
    except socket.timeout:
        pass


# ============================================================
# MAIN TEST
# ============================================================


def main():
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((HOST, PORT))
    print("✅ Connected to IEC104 server")

    # 1. STARTDT
    send_startdt(sock)
    time.sleep(0.5)

    # 2. POLLING (Interrogation)
    send_interrogation(sock)
    recv_loop(sock, timeout=2)

    # 3. SEND SETPOINT
    print("\n⚡ Send setpoint command")
    send_setpoint(sock, ioa=4000, value=12.0)
    recv_loop(sock, timeout=2)

    sock.close()
    print("🔚 Test finished")


if __name__ == "__main__":
    main()
