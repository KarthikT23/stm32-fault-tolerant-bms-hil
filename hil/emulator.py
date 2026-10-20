"""SafeCell HIL pack emulator - v0.

Streams valid BMS frames to the STM32 over a serial port at 10 Hz and prints
whatever the firmware sends back. Every 20th frame is deliberately corrupted
(unless --no-corrupt) so the firmware's CRC rejection can be tested.
"""
import argparse
import csv
import os
import random
import re
import struct
import sys
import time
from datetime import datetime

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

LIMIT_MS = 400  # requirement NFR-1: a real fault must be reported within 400 ms


class Monitor:
    """Collects the STATE / PENDING lines the firmware prints, with their arrival times."""

    def __init__(self):
        self.buf = ""
        self.events = []                 # (arrival time, kind, state name, seq)

    def feed(self, text, now):
        self.buf += text
        while "\n" in self.buf:
            line, self.buf = self.buf.split("\n", 1)
            m = re.match(r"\s*(STATE|PENDING) -> (\w+).*?seq=(\d+)", line)
            if m:
                self.events.append((now, m.group(1), m.group(2), int(m.group(3))))


def evaluate(scenario, events, t_sent):
    """Returns (verdict, detection latency in ms or None, detail)."""
    states = [e for e in events if e[1] == "STATE"]
    if not states or states[0][2] != "NORMAL":
        return "FAIL", None, "no initial STATE -> NORMAL line (press RESET before the run)"
    changes = states[1:]
    if scenario in ("normal", "spike", "hover"):
        if changes:
            return "FAIL", None, f"unexpected change to {changes[0][2]} at seq {changes[0][3]}"
        if scenario == "spike" and not any(e[1] == "PENDING" and e[3] == WARMUP for e in events):
            return "FAIL", None, "spike frame never reached the firmware (no PENDING line)"
        return "PASS", None, "no state change"
    want = "WARNING" if scenario == "warning" else "FAULT"
    if not changes:
        return "FAIL", None, f"never reached {want}"
    if len(changes) > 1:
        return "FAIL", None, f"state chattered ({len(changes)} changes)"
    t, _, name, _ = changes[0]
    if name != want:
        return "FAIL", None, f"went to {name} instead of {want}"
    lat_ms = (t - t_sent[WARMUP]) * 1000.0
    if lat_ms > LIMIT_MS:
        return "FAIL", lat_ms, f"too slow, limit {LIMIT_MS} ms"
    return "PASS", lat_ms, f"first bad frame to {want} line, limit {LIMIT_MS} ms"

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
    ap.add_argument("--check", action="store_true",
                    help="stop after --duration seconds and print PASS/FAIL for the scenario")
    ap.add_argument("--duration", type=float, default=8.0, help="run time in seconds with --check")
    ap.add_argument("--csv", help="append the --check result to this CSV file")
    args = ap.parse_args()

    if args.selftest:
        selftest()
        return
    if not args.port:
        ap.error("serial port required, e.g. COM14")
    if args.check and args.duration < 5:
        ap.error("--duration must be at least 5 s with --check")

    ser = serial.Serial(args.port, 115200, timeout=0)
    print(f"scenario {args.scenario}: expect {EXPECT[args.scenario]}", flush=True)
    seq = 0
    next_t = time.perf_counter()
    t_start = next_t
    t_sent = {}                          # frame seq -> time the frame was written
    monitor = Monitor()
    finished = False
    try:
        while True:
            cells = [3800 + random.randint(-3, 3) for _ in range(4)]
            temps = (280 + random.randint(-2, 2), 281 + random.randint(-2, 2))
            cells, temps = apply_scenario(args.scenario, seq, cells, temps)
            frame = build_frame(seq, cells, -1500, temps)
            if not args.no_corrupt and seq % 20 == 19:
                frame = frame[:-1] + bytes([frame[-1] ^ 0xFF])  # flip the last CRC byte
            t_sent[seq & 0xFF] = time.perf_counter()
            ser.write(frame)
            seq += 1
            next_t += PERIOD_S
            while True:                  # poll the port until the next frame is due
                out = ser.read(512)
                if out:
                    text = out.decode(errors="replace")
                    print(text, end="", flush=True)
                    monitor.feed(text, time.perf_counter())
                remaining = next_t - time.perf_counter()
                if remaining <= 0:
                    break
                time.sleep(min(0.002, remaining))
            if args.check and time.perf_counter() - t_start >= args.duration:
                finished = True
                break
    except KeyboardInterrupt:
        print("\nstopped")
    finally:
        ser.close()
    if args.check and finished:
        verdict, lat_ms, detail = evaluate(args.scenario, monitor.events, t_sent)
        lat_txt = f"{lat_ms:.0f}" if lat_ms is not None else ""
        print(f"\n=== HIL RESULT  scenario={args.scenario}  {verdict}  "
              f"detection_ms={lat_txt or '-'}  ({detail})")
        if args.csv:
            new = not os.path.exists(args.csv)
            with open(args.csv, "a", newline="") as f:
                w = csv.writer(f)
                if new:
                    w.writerow(["time", "scenario", "verdict", "detection_ms", "detail"])
                w.writerow([datetime.now().isoformat(timespec="seconds"),
                            args.scenario, verdict, lat_txt, detail])
        sys.exit(0 if verdict == "PASS" else 1)


if __name__ == "__main__":
    main()
