#ifndef HOST_LINK_CONFIG_H
#define HOST_LINK_CONFIG_H

// =============================================================================
// Host-Link SPI Slave + Handshake Pin Configuration
// Target: ESP32-C6 "C6 mini" (Amazon clone) — display board
// Host:   ESP32-C6 (SPI master, connected via IDC-10 ribbon cable)
// =============================================================================
//
// ELECTRICAL OVERVIEW
// -------------------
//   HOST (SPI master)  ←IDC-10 ribbon→  DISPLAY (SPI slave, this board)
//
//   IDC pin 1  MOSI   ──────────────→  GPIO6  (SPI2 MOSI — data host→display)
//   IDC pin 3  MISO   ←──────────────  GPIO4  (SPI2 MISO — data display→host)
//   IDC pin 5  CLK    ──────────────→  GPIO7  (SPI2 CLK  — clock from host)
//   IDC pin 7  CS     ──────────────→  GPIO5  (SPI2 CS   — chip select, active LOW)
//   IDC pin 9  DISP_READY ←─────────  GPIO1  (handshake: display→host READY, active HIGH)
//   IDC pin 5  HOST_REQ   ──────────→  GPIO0  (handshake: host→display REQUEST, active HIGH)
//   IDC pin 2,4 GND   ──────────────  GND
//   IDC pin 6,8 3.3V  ──────────────  3.3V
//
// =============================================================================
// STRAPPING PIN SAFETY — ESP32-C6 SPECIFIC
// =============================================================================
//
//   The ESP32-C6 strapping pins (sampled at reset) are:
//
//     GPIO8  — Boot mode selection (HIGH = SPI boot normal, LOW = UART download mode)
//     GPIO9  — ROM print control  (HIGH = enable, LOW = suppress ROM serial output)
//     GPIO15 — JTAG source select
//
//   GPIO0 and GPIO1 are NOT strapping pins on the ESP32-C6.
//   This means there is NO boot-mode conflict when using GPIO0/GPIO1 for handshake.
//   (On the original ESP32, GPIO0 WAS a strapping pin — do not confuse the two.)
//
//   IMPORTANT: Do NOT allow external circuitry to drive GPIO8 LOW during reset.
//   If the host board asserts any signal on GPIO8, it WILL force download mode.
//
//   The BOOT button on most C6 mini boards is wired to GPIO9.
//   If your board has a user button on GPIO9, be aware external pull-downs on
//   GPIO9 suppress ROM serial messages but do not prevent normal boot.
//
// =============================================================================
// HANDSHAKE LINE SAFETY DURING FLASHING AND BOOT
// =============================================================================
//
// GPIO0 (HOST_REQ — input on display, output on host):
//   - Safe on ESP32-C6: not a strapping pin.
//   - Add a 10 kΩ pull-up on the display board so the idle state is HIGH (no request).
//   - The host must configure this pin as INPUT (high-Z) until after its own boot
//     completes and firmware is running. This prevents the host from accidentally
//     driving the line during the display board's reset window.
//   - Add a 220 Ω series resistor on the host side to limit contention current.
//   - On some C6 mini boards GPIO0 has a user LED; check your specific board
//     schematic and remove / bypass the LED if it conflicts.
//
// GPIO1 (DISP_READY — output on display, input on host):
//   - Safe on ESP32-C6: not a strapping pin, not UART0.
//   - Display firmware drives this HIGH when a TX packet is queued and ready.
//   - Idle LOW (display not ready / booting) — this is the safe default.
//   - Add a 220 Ω series resistor between the display GPIO1 and the IDC line.
//   - The host must not assert this line; configure host-side pin as INPUT with no pull.
//
// =============================================================================
// SPI SLAVE LINES (GPIO4–7) DURING FLASHING
// =============================================================================
//
// The C6 mini flashes via USB-JTAG (built-in USB PHY on dedicated pads, not GPIO4–7).
// GPIO4–7 are NOT used during USB-C flashing; the host side is free to ignore them.
//
// Bring-up rules for SPI lines:
//   1. Keep CS (GPIO5) HIGH on the host during display board reset.
//      A LOW CS during reset causes the SPI slave to start capturing MOSI bits
//      before its buffers are configured, leading to garbled first transactions.
//   2. MOSI (GPIO6) and CLK (GPIO7) may be in any state during reset with no boot effect.
//   3. MISO (GPIO4) is driven by the display board. The host must not actively
//      drive GPIO4 low (i.e., keep host MISO pin as INPUT, not output).
//   4. Add a 100 Ω series resistor on MISO between display GPIO4 and IDC line.
//      This limits current if both sides drive the line simultaneously during startup.
//   5. SPI frequency limit on ESP32-C6 SPI slave: recommended ≤ 10 MHz for reliable
//      operation in slave mode; start at 1 MHz during bring-up and step up.
//
// =============================================================================

#include <Arduino.h>

// ── SPI Slave pins (IDC-10, SPI2 peripheral) ──────────────────────────────

constexpr uint8_t DISPLAY_SPI_MISO_PIN = 4;   // IDC pin 3  — output from display to host
constexpr uint8_t DISPLAY_SPI_CS_PIN   = 5;   // IDC pin 7  — chip select, active LOW
constexpr uint8_t DISPLAY_SPI_MOSI_PIN = 6;   // IDC pin 1  — input to display from host
constexpr uint8_t DISPLAY_SPI_CLK_PIN  = 7;   // IDC pin 5  — clock input from host

// ── Handshake pins ────────────────────────────────────────────────────────

// HOST_REQ: host asserts HIGH to signal it has data or wants a transaction.
// Direction on this board: INPUT (with external 10 kΩ pull-up, active HIGH).
// IDC pin 5.  NOT a strapping pin on ESP32-C6.
constexpr uint8_t DISPLAY_HOST_REQ_PIN = 0;

// DISP_READY: display asserts HIGH when it has a response queued and is ready
// for the host to initiate an SPI transaction.
// Direction on this board: OUTPUT, idle LOW, push-pull.
// IDC pin 9.  NOT a strapping pin on ESP32-C6.
constexpr uint8_t DISPLAY_READY_PIN = 1;

// ── Handshake polarity ─────────────────────────────────────────────────────

// Active HIGH: signal == 1 means the condition is asserted.
// Idle LOW for DISP_READY keeps the line safe during display boot.
constexpr bool DISPLAY_HOST_REQ_ACTIVE_HIGH = true;
constexpr bool DISPLAY_READY_ACTIVE_HIGH    = true;

// ── SPI protocol parameters ───────────────────────────────────────────────

constexpr uint32_t DISPLAY_SPI_BRING_UP_HZ  = 1000000UL;   // 1 MHz — use during first bring-up
constexpr uint32_t DISPLAY_SPI_NOMINAL_HZ   = 8000000UL;   // 8 MHz — target operating rate
constexpr uint8_t  DISPLAY_SPI_MODE         = SPI_MODE0;   // CPOL=0, CPHA=0

// Maximum SPI packet payload (bytes). Both sides must agree.
constexpr size_t DISPLAY_SPI_MAX_PAYLOAD_BYTES = 128;

// ── DO NOT USE during bring-up ─────────────────────────────────────────────
// GPIO8 is the boot-mode strapping pin on ESP32-C6.
// Never connect external hardware to GPIO8 that can drive it LOW during reset.
// (It is not assigned here; this comment is a reminder.)

#endif // HOST_LINK_CONFIG_H
