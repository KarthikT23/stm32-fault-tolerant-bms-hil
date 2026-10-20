"""SafeCell HIL pack emulator - v0.

Streams valid BMS frames to the STM32 over a serial port at 10 Hz and prints
whatever the firmware sends back. Every 20th frame is deliberately corrupted
(unless --no-corrupt) so the firmware's CRC rejection can be tested.
"""
import argparse
import random
import struct
import time

import serial

SOF = 0xA5
PERIOD_S = 0.1  # 10 Hz

WARMUP = 30  # healthy frames (3 s) before a scenario starts

EXPECT = {
    "normal":   "stays NORMAL, no STATE line after the first",
    "spike":    "stays NORMAL (one 65.0 C frame is ignored)",
    "hover":    "stays NORMAL (4195/4205 mV flip-flop, no chatter)",
    "warning":  "NORMAL -> WARNING (orange LED) about 300 ms after the 3 s mark",
    "overtemp": "NORMAL -> FAULT cause=OVERTEMP (red LED) about 300 ms after the 3 s mark",
}


def apply_scenario(name, seq, cells, temps):
    cells, temps = list(cells), list(temps)
    if seq >= WARMUP:
        if name == "spike" and seq == WARMUP:
            temps[0] = 650                         # one frame only
        elif name == "overtemp":
            temps[0] = 650                         # sustained
        elif name == "warning":
            cells[0] = 4210                        # sustained, above 4200 warn
        elif name == "hover":
            cells[0] = 4195 if seq % 2 else 4205   # straddles the 4200 threshold
    return cells, temps

def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def build_frame(seq, cells_mv, current_ma, temps_dc):
    payload = struct.pack("<4Hh2h", *cells_mv, current_ma, *temps_dc)  # 14 bytes
    body = bytes([len(payload), seq & 0xFF]) + payload
    return bytes([SOF]) + body + struct.pack("<H", crc16_modbus(body))


def selftest():
    # Standard CRC-16/MODBUS check value for the ASCII string "123456789"
    assert crc16_modbus(b"123456789") == 0x4B37, "CRC-16/MODBUS check value mismatch"
    f = build_frame(0, [3800] * 4, -1500, (280, 281))
    assert len(f) == 19 and f[0] == SOF and f[1] == 14, "frame layout mismatch"
    print("selftest OK")
    print("sample frame:", f.hex(" "))


def main():
    ap = argparse.ArgumentParser(description="SafeCell HIL pack emulator")
    ap.add_argument("port", nargs="?", help="serial port of the FTDI adapter, e.g. COM14")
    ap.add_argument("--selftest", action="store_true", help="check CRC and frame layout, then exit")
    ap.add_argument("--no-corrupt", action="store_true", help="do not corrupt every 20th frame")
    ap.add_argument("--scenario", choices=list(EXPECT), default="normal",
                    help="fault scenario to inject after a 3 s healthy warm-up")
    args = ap.parse_args()

    if args.selftest:
        selftest()
        return
    if not args.port:
        ap.error("serial port required, e.g. COM14")

    ser = serial.Serial(args.port, 115200, timeout=0)
    print(f"scenario {args.scenario}: expect {EXPECT[args.scenario]}", flush=True)
    seq = 0
    next_t = time.perf_counter()
    try:
        while True:
            cells = [3800 + random.randint(-3, 3) for _ in range(4)]
            temps = (280 + random.randint(-2, 2), 281 + random.randint(-2, 2))
            cells, temps = apply_scenario(args.scenario, seq, cells, temps)
            frame = build_frame(seq, cells, -1500, temps)
            if not args.no_corrupt and seq % 20 == 19:
                frame = frame[:-1] + bytes([frame[-1] ^ 0xFF])  # flip the last CRC byte
            ser.write(frame)
            out = ser.read(512)
            if out:
                print(out.decode(errors="replace"), end="", flush=True)
            seq += 1
            next_t += PERIOD_S
            time.sleep(max(0.0, next_t - time.perf_counter()))
    except KeyboardInterrupt:
        print("\nstopped")
    finally:
        ser.close()


if __name__ == "__main__":
    main()
