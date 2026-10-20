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

| ID | Requirement | Status | Verified by |
|---|---|---|---|
| FR-1 | Receive and validate frames (SOF, LEN, CRC). Drop bad frames and count them. | Done | T-01..T-08, HIL `crc_err` counter |
| FR-2 | Monitor 4 cell voltages, pack current and 2 temperatures. | Done | P-02..P-06 |
| FR-3 | Protection state machine INIT, NORMAL, WARNING, FAULT (latched). LEDs: green NORMAL, orange WARNING, red FAULT. | Done (contactor LED not implemented) | P-01..P-12, HIL warning, overtemp |
| FR-4 | Report state changes, pending counts and fault cause to the PC, plus one status line per second. | Done | HIL logs |
| FR-5 | A latched FAULT shall survive reset and power loss. A damaged record shall keep the board in FAULT. FAULT is released only by `!CLEAR` and only if the clear record was stored. | Done | FL-01..FL-12, P-13, HIL persist |
| FR-6 | Logging shall never block the parser task. Dropped lines are counted. | Done | HIL `drop=0` |
| NFR-1 | Fault detection <= 400 ms from the first out-of-range frame. | Met, 208 to 209 ms | HIL warning, overtemp |
| NFR-2 | A single-frame spike shall not raise FAULT, and a value hovering at a threshold shall not make the state chatter. | Met | P-08..P-11, HIL spike, hover |
| NFR-3 | A hung task shall be recovered by the watchdog within 3 s (changed from 2 s: IWDG timeout is 0.7 to 1.9 s over LSI tolerance, plus boot). | Met, 1209 ms | HIL hang |
| NFR-4 | ISR-to-decision latency <= 1 ms (replaces the unmeasured CPU-load requirement). | Met, 101 to 412 us | DWT counter, `lat=` lines |
| NFR-5 | A power cut while writing a record shall never release a latched FAULT. | Met at every simulated cut point on the host, and one torn write on target. Not tested with 20 physical power cuts. | FL-05, FL-07, HIL persist |

Dropped from the first draft: state-of-charge estimation (out of scope), contactor LED, CPU-load measurement.

## 3. Protection thresholds (final)

| Quantity | Warning | Fault |
|---|---|---|
| Cell voltage high | >= 4.20 V | >= 4.25 V |
| Cell voltage low | <= 3.10 V | <= 2.90 V |
| Temperature | >= 50 deg C | >= 60 deg C |
| Pack current | - | abs(I) > 10 A |

Debounce: 3 consecutive frames to change state. Warning clears 20 mV (cells) or 2.0 deg C (temperature) inside the threshold. FAULT is latched.

## 4. Fault log record (flash sector 11, 0x080E0000, 128 KB)
32 bytes: seq, type|cause<<8, 4 snapshot words, CRC-16, commit marker 0xC0FFEE01 (written last). Append-only, no erase at run time.
Boot rule: start in FAULT if the last valid record is FAULT, or a damaged record follows the last valid one.