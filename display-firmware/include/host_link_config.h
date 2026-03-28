#ifndef HOST_LINK_CONFIG_H
#define HOST_LINK_CONFIG_H

// =============================================================================
// Host-Link SPI Slave + Handshake GPIO Configuration
// Target: ESP32-C6 DevKitM (Amazon clone, "C6 Mini")
// =============================================================================
//
// ESP32-C6 strapping-pin analysis (why GPIO0/GPIO1 are chosen):
//
//   GPIO8  – strapping pin: selects JTAG signal source at reset.
//             Driving it LOW externally routes JTAG to IO pins (can disrupt
//             USB-JTAG-based flashing).  → DO NOT use as handshake.
//
//   GPIO9  – strapping pin: boot-mode selector.  HIGH = SPI-Flash boot
//             (normal); LOW = Download/flash mode.  This is the BOOT button.
//             If the other board drives GPIO9 LOW during reset the chip will
//             enter download mode and refuse to boot normally.  → DO NOT use.
//
//   GPIO0  – NOT a strapping pin on ESP32-C6.  Safe to use as a handshake
//             line.  During reset the host board must hold this pin as an
//             input (high-Z), or a 10 kΩ pull-down keeps it at idle (LOW).
//
//   GPIO1  – NOT a strapping pin on ESP32-C6.  Safe as a handshake output
//             from the display side.  Drive LOW until firmware is ready.
//
// ─── Host-link SPI (display = SPI slave, host = SPI master) ─────────────────
//
//   GPIO4  MISO   – display → host data
//   GPIO5  CS     – active-LOW chip select, driven by host
//   GPIO6  MOSI   – host → display data
//   GPIO7  CLK    – SPI clock, driven by host
//
// ─── Handshake lines ─────────────────────────────────────────────────────────
//
//   GPIO0  HOST_REQ   – Host → Display, active-HIGH
//                       Host asserts HIGH to request a SPI transaction.
//                       Display polls/interrupts on this pin before clocking.
//                       Add 10 kΩ pull-down on display-board side so the pin
//                       reads LOW (idle) when the host board is unpowered or
//                       in reset.
//                       Add 470 Ω series resistor at the host-board output.
//
//   GPIO1  DISP_READY – Display → Host, active-HIGH
//                       Display drives HIGH when TX/RX buffers are prepared
//                       and a transaction can begin.  Host must only assert
//                       CS and start clocking after DISP_READY is HIGH.
//                       Display drives LOW on reset/init and immediately after
//                       a completed transaction until next buffer is queued.
//                       Add 470 Ω series resistor at the display-board output.
//
// ─── Electrical recommendations ──────────────────────────────────────────────
//
//   • 470 Ω series resistor on every inter-board signal (both SPI and handshake).
//   • 10 kΩ pull-down on HOST_REQ (GPIO0) at the display board so the pin
//     never floats when the host is unpowered.
//   • No strong pull-ups on GPIO0 or GPIO1 — idle state must be LOW.
//   • At power-on/reset both handshake pins must be high-Z or LOW; they must
//     NEVER be driven HIGH by external circuitry before firmware runs.
//   • Keep GPIO8 and GPIO9 free of external load at all times.
//
// =============================================================================

#include <Arduino.h>

// ─── SPI slave pins ───────────────────────────────────────────────────────────
constexpr uint8_t HOSTLINK_SPI_MISO_PIN = 4;
constexpr uint8_t HOSTLINK_SPI_CS_PIN   = 5;   // active-LOW, driven by host
constexpr uint8_t HOSTLINK_SPI_MOSI_PIN = 6;
constexpr uint8_t HOSTLINK_SPI_CLK_PIN  = 7;

// ─── Handshake pins ──────────────────────────────────────────────────────────
// HOST_REQ:   INPUT  on display board, OUTPUT on host board.
// DISP_READY: OUTPUT on display board, INPUT  on host board.
constexpr uint8_t HOSTLINK_HOST_REQ_PIN   = 0;   // Host → Display, active-HIGH
constexpr uint8_t HOSTLINK_DISP_READY_PIN = 1;   // Display → Host, active-HIGH

// ─── Polarity ─────────────────────────────────────────────────────────────────
// Both handshake lines are active-HIGH (1 = asserted, 0 = idle).
constexpr bool HOSTLINK_HANDSHAKE_ACTIVE_HIGH = true;

// ─── SPI transaction parameters ───────────────────────────────────────────────
constexpr uint32_t HOSTLINK_SPI_MAX_FREQ_HZ  = 10000000UL;  // 10 MHz max
constexpr uint8_t  HOSTLINK_SPI_MODE         = SPI_MODE0;
constexpr uint8_t  HOSTLINK_SPI_BIT_ORDER    = MSBFIRST;

// ─── Handshake timing ────────────────────────────────────────────────────────
// Maximum time the display will wait after asserting DISP_READY before it
// de-asserts (timeout guard, ms).
constexpr uint32_t HOSTLINK_READY_TIMEOUT_MS  = 100;
// Minimum LOW time (idle) on DISP_READY after a completed transaction before
// the next can be advertised (de-glitch gap, ms).
constexpr uint32_t HOSTLINK_READY_DEGLITCH_MS = 1;

#endif // HOST_LINK_CONFIG_H
