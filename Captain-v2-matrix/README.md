# Captain v2 Matrix Board Datasheet (Firmware Contract)

## 1. Purpose

This board is a dedicated switch/lamp matrix controller isolated from the main control board to reduce switching-noise coupling and provide a clean I2C break point.

The board presents itself on I2C like a peripheral IC (HT16K33-style behavior):
- Internal register/RAM map
- Command bytes for global control
- Master reads/writes registers instead of exchanging framed packets

## 2. Hardware Summary

### MCU
- ESP32-C6 (current PlatformIO target: `esp32-c6-devkitc-1`)

### I2C Interface
- Slave address: `0x24`
- SDA: GPIO16
- SCL: GPIO17
- Bus speed: 100 kHz

### Matrix Signals
- Row drivers (8): GPIO3, GPIO2, GPIO11, GPIO10, GPIO8, GPIO7, GPIO6, GPIO4
- Switch columns (4 inputs): GPIO20, GPIO21, GPIO22, GPIO23

### Lamp Shift Registers (74HC595 chain)
- DATA (DS): GPIO2
- CLOCK (SHCP): GPIO12
- LATCH (STCP): GPIO4
- Lamp columns mapped to SR bits 0..4

## 3. Functional Model

The firmware maintains:
- Lamp RAM: 8 rows, 1 byte per row (lower 5 bits used)
- Switch state bytes: 4 bytes packed from 8x4 switch matrix

The lamp scanner runs row-by-row. For each active row:
- Shift out column bits
- Enable row for a dwell period
- Move to next row

Global gates:
- System enable controls switch scanning and overall operation
- Output enable controls lamp output

If system or output is disabled, lamp output is forced off.

## 4. I2C Protocol (HT16K33-style)

### 4.1 Command Bytes

- `0x20` = System setup, disable
- `0x21` = System setup, enable
- `0x80` = Output setup, disable
- `0x81` = Output setup, enable
- `0xE0..0xEF` = Pulse-width level (low nibble = 0..15)

Pulse-width level is global (not per-lamp dimming).

### 4.2 Register Windows

- Lamp RAM: `0x00..0x07`
  - 8 bytes total (row 0..7)
  - Bits 0..4 = lamp columns 0..4
- Switch RAM: `0x40..0x43`
  - 4 bytes packed switch states from bit index `(row * 4 + col)`
- Diagnostics: `0xF0..0xF3`

### 4.3 Read/Write Behavior

Write transaction:
1. First byte selects command/register pointer.
2. Optional payload bytes write sequential registers.

Read transaction:
1. Master writes register pointer.
2. Master requests bytes.
3. Slave returns bytes from that window.

## 5. Diagnostics Registers (`0xF0..0xF3`)

- `0xF0`: status flags
  - Bit 0: system enabled
  - Bit 1: output enabled
  - Bit 2: test override active (test mode)
  - Bit 3: test auto-walk active (test mode)
- `0xF1`: current pulse-width level (0..15)
- `0xF2`: lamp row 0 value
- `0xF3`: switch byte 0 value

## 6. Lamp Pulse Width

Current implementation:
- `pulse_us = 50 + (level * 50)`
- Level range: 0..15
- Default level: 4 (250 us dwell)

This is intended as a coarse global brightness/current control. Typical operation is fixed level, not dynamic dimming.

## 7. Standalone Matrix Test Support (Removable)

Test support is intentionally isolated in:
- `Captain-v2/include/matrix_test_support.h`
- `Captain-v2/src/matrix_test_support.cpp`

Build control flag:
- `CAPTAIN_MATRIX_TEST_SUPPORT=1` enables interactive serial test commands.
- Set to `0` (or remove define) to disable test features.

### Serial Test Commands

- `HELP`
- `STATUS`
- `SYSTEM ON|OFF`
- `OUTPUT ON|OFF`
- `PULSE <0-15>`
- `ALL ON|OFF`
- `CLEAR`
- `ROW <0-7> <0-31>`
- `LAMP <row> <col> ON|OFF`
- `WALK ON|OFF`
- `SWITCHES`

## 8. Project Layout

This folder is a dedicated matrix-board PlatformIO project:
- `Captain-v2-matrix/platformio.ini`

It references shared source/include in `Captain-v2/` so matrix firmware and protocol stay centralized.

## 9. Minimal Master Example (Wire API)

```cpp
// Enable matrix device
Wire.beginTransmission(0x24); Wire.write(0x21); Wire.endTransmission();
Wire.beginTransmission(0x24); Wire.write(0x81); Wire.endTransmission();

// Set pulse width level = 4
Wire.beginTransmission(0x24); Wire.write(0xE4); Wire.endTransmission();

// Write lamp rows 0..7
uint8_t lampRows[8] = {0x00,0x02,0x02,0x02,0x00,0x00,0x00,0x00};
Wire.beginTransmission(0x24);
Wire.write(0x00);
Wire.write(lampRows, 8);
Wire.endTransmission();

// Read 4 switch bytes
Wire.beginTransmission(0x24);
Wire.write(0x40);
Wire.endTransmission(false);
Wire.requestFrom(0x24, 4);
```

## 10. Validation Checklist (No Control Board Required)

1. Confirm board responds at `0x24` on I2C scan.
2. Write `0x21` and `0x81`; verify lamp output can activate.
3. Write test lamp rows (`0x00..0x07`) and verify expected rows/columns.
4. Toggle physical switches and verify switch bytes (`0x40..0x43`) change.
5. Sweep pulse level (`0xE0..0xEF`) and verify visible/intensity timing behavior.
6. Read diagnostics (`0xF0..0xF3`) and confirm status bits/values.

## 11. Recommended Hardware Iteration (March 2026)

Decision: lower the high-side rail voltage instead of pushing the BSS84/BSS138 pre-driver stage closer to its voltage limits.

Reasoning from original design behavior:
- The original TIP125-based path tolerated a large conduction drop (`VCE` around 4 V in this use case).
- The current MOSFET path has much lower conduction drop (`VDS` typically below 0.2 V at 5 A for the selected device), which can increase effective stress elsewhere if the rail is left high.
- With measured rail excursions near 25.5 V, the small-signal gate-drive devices (BSS84/BSS138 stage) are at higher risk of gate overstress and non-deterministic turn-off behavior.

Practical guidance:
- Prefer rail reduction as the first hardware iteration.
- Re-validate ON/OFF gate-source voltages for the BSS84/BSS138 stage and the power MOSFET after rail adjustment.
- Keep this as a safety and reliability step before any component substitutions.

Cross-reference:
- Canonical redesign parking lot: `docs/NEXT_ITERATION_RECOMMENDATIONS.md`.
- Next-iteration handoff prompt: `NEXT_CHAT_PROMPT_2026-03-26.txt`.
- Top-level status summary: `README.md` (Current Development Focus).
