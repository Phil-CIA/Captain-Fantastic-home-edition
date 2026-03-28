# Display Board Firmware — ESP32-C6

This directory contains the firmware for the **Captain Fantastic home-edition
display board**: an ESP32-C6 module that acts as the visual front-end for the
pinball machine system.

---

## Hardware Summary

| Component       | Part         | Interface to ESP32-C6 |
|-----------------|--------------|----------------------|
| TFT display     | ST7796S 3.5″ | SPI master (GPIO19/20/21) |
| Touch controller| XPT2046      | SPI master (shared bus)  |
| SD card         | Standard µSD | SPI master (shared bus)  |
| Host link       | IDC-10 ribbon| SPI **slave** (GPIO4–7) + handshake (GPIO0/1) |

---

## Directory Layout

```
display-firmware/
├── include/
│   ├── host_link_config.h       ← ALL host-link pin/protocol constants (edit here)
│   └── display_local_config.h   ← ALL local peripheral pin constants (edit here)
├── src/
│   ├── display_main.cpp         ← setup() / loop() entry point
│   ├── host_link.h              ← SPI slave + handshake API
│   └── host_link.cpp            ← SPI slave + handshake implementation
├── platformio.ini               ← PlatformIO project (ESP32-C6)
├── FLASHING_NOTES.md            ← ⚠️ GPIO safety, strapping pins, bring-up steps
└── README.md                    ← this file
```

---

## Architecture

```
 captain_control (host ESP32-C6)          display board (this firmware)
 ┌──────────────────────────────┐         ┌──────────────────────────────┐
 │  Game logic                  │         │  TFT render (ST7796S)        │
 │  ADC / sensors               │         │  Touch read  (XPT2046)       │
 │  Solenoid / lamp drive       │         │  SD card                     │
 │                              │         │                              │
 │  SPI MASTER ─────────────────┼──IDC-10─┼──▶ SPI SLAVE                │
 │  GPIO4 SCK  ─────────────────┼────────▶│  GPIO4 SCK                  │
 │  GPIO5 MOSI ─────────────────┼────────▶│  GPIO5 MOSI                 │
 │  GPIO6 MISO ◀────────────────┼─────────│  GPIO6 MISO                 │
 │  GPIO7 CS   ─────────────────┼────────▶│  GPIO7 CS                   │
 │                              │         │                              │
 │  HOST_REQ (GPIO0) ───────────┼────────▶│  GPIO0  (in)  [strapping!]  │
 │  DISP_READY (GPIO1) ◀────────┼─────────│  GPIO1  (out)               │
 └──────────────────────────────┘         └──────────────────────────────┘
```

**Host is always master.**  The display never starts a transaction; it only
prepares data and signals DISP\_READY to tell the host it is ready.

### Transaction sequence

1. Host has a new STATE packet; asserts HOST\_REQ (GPIO0 HIGH on display side).
2. Display finishes current render work, queues an EVENT (or NOP) TX packet,
   asserts DISP\_READY (GPIO1 HIGH).
3. Host waits for DISP\_READY, asserts CS, clocks the SPI transaction.
4. Both sides receive their respective packets and de-assert their handshake line.

---

## Packet Format

```
Byte  0     : Magic HIGH (0xC4)
Byte  1     : Magic LOW  (0xF5)
Byte  2     : Message type (DisplayMsgType enum)
Byte  3     : Protocol version (1)
Bytes 4–5   : Payload length (uint16, little-endian)
Bytes 6..N  : Payload
Byte  N+1   : XOR checksum of bytes 0..N
```

Magic bytes spell **0xC4F5** — "Captain Fantastic Display".

| Type  | Hex  | Direction       | Description                         |
|-------|------|-----------------|-------------------------------------|
| NOP   | 0x00 | Either          | No data; keep-alive                 |
| STATE | 0x01 | Host → Display  | Game state snapshot (V, I, scores…) |
| EVENT | 0x02 | Display → Host  | Touch / button event                |
| ACK   | 0x03 | Either          | Acknowledge                         |
| RESET | 0xFF | Either          | Soft reset / re-sync                |

---

## Pin Configuration — Changing Pins

All pin constants are **defined in exactly two files**:

| File                              | What it configures                      |
|-----------------------------------|-----------------------------------------|
| `include/host_link_config.h`      | IDC-10 SPI pins + handshake GPIO + packet framing |
| `include/display_local_config.h`  | TFT, touch, SD, backlight               |

To reassign any pin:
1. Edit the relevant `constexpr` in the header file.
2. Rebuild and flash.
3. Update IDC-10 wiring if a host-link pin changed.

Do not add `#define` pin numbers inside `.cpp` files.

---

## ⚠️ Flashing — Read Before Wiring

**GPIO0 is an ESP32-C6 strapping pin.**  Incorrect wiring can prevent the board
from booting.  See **[FLASHING_NOTES.md](FLASHING_NOTES.md)** for:

* Required 10 kΩ pull-up on GPIO0
* USB-JTAG vs. UART flash methods
* Step-by-step first-power-on checklist
* Safe / unsafe GPIO table
* Alternate handshake pins if GPIO0 causes problems

---

## Quick Start

```powershell
# 1. Open this directory in PlatformIO
cd display-firmware

# 2. Build (default: display_board environment, ESP32-C6, USB-JTAG)
pio run

# 3. Upload via USB-JTAG (safe — does not require GPIO0 to be LOW)
pio run -t upload

# 4. Monitor serial output
pio device monitor
```

Expected boot output:
```
[display] setup start — ESP32-C6 display board
[display] Host-link SPI: SCK=GPIO4 MOSI=GPIO5 MISO=GPIO6 CS=GPIO7
[display] Handshake: DISP_READY=GPIO1  HOST_REQ=GPIO0
[host_link] SPI slave ready. SCK=4 MOSI=5 MISO=6 CS=7
[host_link] Handshake: DISP_READY=GPIO1 HOST_REQ=GPIO0
[display] setup complete
```

---

## Status

| Feature                         | Status         |
|---------------------------------|----------------|
| SPI slave init (GPIO4–7)        | ✅ Implemented  |
| DISP\_READY / HOST\_REQ handshake| ✅ Implemented  |
| Packet framing + checksum       | ✅ Implemented  |
| TFT init (ST7796S)              | ✅ Stub ready   |
| Touch read (XPT2046)            | 🔲 TODO         |
| STATE packet rendering          | 🔲 TODO         |
| EVENT packet (touch → host)     | 🔲 TODO         |
| SD card logging                 | 🔲 TODO         |
| OTA update                      | 🔲 TODO         |

---

## Related Repos / Docs

* **[Captain-v2 firmware](../Captain-v2/README.md)** — host (`captain_control`)
  and matrix (`captain_matrix`) ESP32 firmware
* **[FLASHING_NOTES.md](FLASHING_NOTES.md)** — GPIO safety and bring-up procedure
* Espressif ESP32-C6 datasheet — strapping pin table (Table 2-4)
* [ESP-IDF SPI Slave API](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/api-reference/peripherals/spi_slave.html)
* [Bodmer/TFT\_eSPI](https://github.com/Bodmer/TFT_eSPI)
