#ifndef DISPLAY_LOCAL_CONFIG_H
#define DISPLAY_LOCAL_CONFIG_H

// =============================================================================
// Local Peripheral Pin Configuration
// Target: ESP32-C6 "C6 mini" (Amazon clone) — display board
// Peripherals: TFT ST7796S, touch XPT2046, SD card (all SPI master on SPI2)
// =============================================================================
//
// The ESP32-C6 has one SPI peripheral (SPI2) usable for both slave and master.
// On this board SPI2 acts as SLAVE toward the host and MASTER toward local
// peripherals. The slave role is gated by the host-link CS (GPIO5). Local
// peripherals each have their own dedicated CS line so the bus is properly
// arbitrated via separate CS pins.
//
// NOTE: The shared-SPI topology means the host link and local peripherals
// cannot be active simultaneously. The display firmware must:
//  1. Not service local SPI peripherals while a host SPI transaction is in
//     progress (CS GPIO5 is LOW).
//  2. Keep DISP_READY (GPIO1) LOW during any local SPI operation so the host
//     does not initiate a transaction while the bus is busy.
//
// =============================================================================

#include <Arduino.h>

// ── SPI Master bus (shared with host-link slave, same physical pins) ──────

// These mirror the slave-side signal names but from the local-peripheral perspective.
constexpr uint8_t LOCAL_SPI_MOSI_PIN = 6;    // GPIO6 — MOSI to local peripherals
constexpr uint8_t LOCAL_SPI_MISO_PIN = 4;    // GPIO4 — MISO from local peripherals
constexpr uint8_t LOCAL_SPI_CLK_PIN  = 7;    // GPIO7 — SCK to local peripherals

// ── TFT display: ST7796S ─────────────────────────────────────────────────

constexpr uint8_t TFT_CS_PIN  = 10;   // GPIO10 — SPI chip select (active LOW)
constexpr uint8_t TFT_DC_PIN  = 11;   // GPIO11 — data/command select (D=1, C=0)
constexpr uint8_t TFT_RST_PIN = 12;   // GPIO12 — hardware reset (active LOW)
constexpr uint8_t TFT_BL_PIN  = 13;   // GPIO13 — backlight PWM (active HIGH)

constexpr uint32_t TFT_SPI_HZ = 40000000UL;   // 40 MHz — ST7796S write clock max

// ── Touch controller: XPT2046 ────────────────────────────────────────────

constexpr uint8_t TOUCH_CS_PIN  = 14;   // GPIO14 — SPI chip select (active LOW)
constexpr uint8_t TOUCH_IRQ_PIN = 15;   // GPIO15 — touch interrupt (active LOW)

constexpr uint32_t TOUCH_SPI_HZ = 2000000UL;   // 2 MHz — XPT2046 max reliable rate

// ── SD card ──────────────────────────────────────────────────────────────

constexpr uint8_t SD_CS_PIN = 20;   // GPIO20 — SPI chip select (active LOW)

constexpr uint32_t SD_SPI_HZ = 25000000UL;   // 25 MHz — SD card SPI mode max

// ── UART0 — reserved for flash/debug serial ───────────────────────────────
//
// On the ESP32-C6 the default UART0 pins are:
//   TXD0 = GPIO16
//   RXD0 = GPIO17
//
// These are the pins used when flashing via an external USB-UART adapter.
// Do NOT connect external hardware to GPIO16 or GPIO17.
// The C6 mini also supports flashing via its built-in USB-JTAG (USB-C port),
// which does not use GPIO16/17. See FLASHING_NOTES.md for details.

constexpr uint8_t UART0_TX_PIN = 16;   // Reserved — flash/debug TX
constexpr uint8_t UART0_RX_PIN = 17;   // Reserved — flash/debug RX

// ── Unassigned / spare GPIOs ─────────────────────────────────────────────
//
// GPIO2, GPIO3, GPIO8, GPIO9, GPIO18, GPIO19, GPIO21, GPIO22, GPIO23
//
// GPIO8: Boot-mode strapping pin — keep unconnected or with a weak pull-up.
//        Do NOT connect active external drivers to GPIO8.
// GPIO9: ROM print strapping pin, often doubles as BOOT button on C6 mini boards.
//        If your board has a BOOT button on GPIO9, leave it free.

#endif // DISPLAY_LOCAL_CONFIG_H
