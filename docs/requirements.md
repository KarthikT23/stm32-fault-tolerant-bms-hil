# SafeCell - Requirements and Interface Spec

## 1. Frame format (PC -> STM32, 115200 8N1, little-endian, 10 Hz)

| Bytes | Field |
|---|---|
| 1 | SOF = 0xA5 |
| 1 | LEN = 14 (payload length) |
| 1 | SEQ (wraps at 255) |
| 8 | 4 cell voltages, uint16, mV |
| 2 | pack current, int16, mA (negative = discharging) |
| 4 | 2 temperatures, int16, units of 0.1 deg C |
| 2 | CRC-16/MODBUS over LEN, SEQ and payload |

Total: 19 bytes per frame.

## 2. Requirements (provisional, refined with measured numbers)

| ID | Requirement |
|---|---|
| FR-1 | Receive and validate frames (SOF, LEN, CRC). Drop bad frames and count them. |
| FR-2 | Monitor 4 cell voltages, pack current and 2 temperatures. |
| FR-3 | Estimate state of charge (SoC) and report it. |
| FR-4 | Protection state machine: INIT, NORMAL, WARNING, FAULT (latched). LEDs: green = NORMAL, orange = WARNING, red = FAULT, blue = contactor closed (off on FAULT). |
| FR-5 | Report state, SoC and fault cause to the PC on every valid frame. |
| NFR-1 | Fault detection latency <= 400 ms from the first out-of-range frame. |
| NFR-2 | Zero false alarms over 20 noisy scripted runs. |
| NFR-3 | SoC error <= +/-5% against the emulator ground truth. |
| NFR-4 | Hung-task recovery within 2 s (watchdog). |
| NFR-5 | CPU load <= 30%. |
| NFR-6 | Zero corrupt log entries after 20 power cuts. |

## 3. Protection thresholds (provisional)

| Quantity | Warning | Fault |
|---|---|---|
| Cell voltage high | >= 4.20 V | >= 4.25 V |
| Cell voltage low | <= 3.10 V | <= 2.90 V |
| Temperature | >= 50 deg C | >= 60 deg C |
| Pack current | - | abs(I) > 10 A |

Debounce: 3 consecutive frames to enter a state, with a hysteresis margin to leave it.
