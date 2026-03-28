#ifndef HOST_LINK_CONFIG_H
#define HOST_LINK_CONFIG_H

/*
 * host_link_config.h
 * ─────────────────────────────────────────────────────────────────────────────
 * Pin assignments for the SPI slave link between the display ESP32-C6 and the
 * host ESP32-C6.  The display board used here is the ESP32-C6 DevKitM Amazon
 * clone ("C6 Mini" / "SuperMini" style).
 *
 * Board note
 * ──────────
 * These boards are widely sold on Amazon and AliExpress under names such as
 * "ESP32-C6 Mini", "ESP32-C6 SuperMini", and "ESP32-C6 DevKitM-1 clone".
 * They carry a USB-C connector wired directly to the ESP32-C6 built-in
 * USB-Serial/JTAG peripheral (D+ = GPIO13, D- = GPIO12).  No external
 * USB-to-serial bridge chip is present.
 *
 * SPI host-link bus (display = SPI SLAVE, host = SPI MASTER)
 * ───────────────────────────────────────────────────────────
 *   Signal   GPIO   IDC-10 pin   Notes
 *   ──────   ────   ──────────   ─────────────────────────────────────────────
 *   SCLK      4        3         Clock driven by host
 *   MOSI      5        4         Master-Out Slave-In
 *   MISO      6       10         Master-In  Slave-Out
 *   CS        7        6         Active-LOW chip-select, driven by host
 *
 * Handshake lines
 * ───────────────
 *   Signal        GPIO   IDC-10 pin   Schematic label   Direction
 *   ───────────   ────   ──────────   ───────────────   ──────────────────────
 *   HOST_REQ       0        5         RTS / CTS *       Host → Display (input)
 *   DISP_READY     1        9         GPIO1 / TP19      Display → Host (output)
 *
 * (* The schematic labels IDC pin 5 as "RTS/CTS".  In firmware this signal is
 *    named HOST_REQ throughout.  They refer to the same physical line.)
 *
 *   HOST_REQ  (GPIO0):  Host asserts HIGH when it is about to start a transfer
 *                       and wants the display to prepare its TX buffer.
 *   DISP_READY (GPIO1): Display asserts HIGH when its RX/TX buffers are set
 *                       and it is ready for the host to begin the SPI frame.
 *                       The host must wait for this signal before asserting CS.
 *
 * ──────────────────────────────────────────────────────────────────────────────
 * ⚠  GPIO0 BOOT/STRAPPING WARNING
 * ──────────────────────────────────────────────────────────────────────────────
 * GPIO0 is sampled by the ESP32-C6 ROM bootloader at every reset.
 *   • GPIO0 HIGH (or floating-high) at reset  →  normal boot
 *   • GPIO0 LOW at reset                      →  download/flash mode
 *
 * The BOOT button on the dev board pulls GPIO0 LOW temporarily.  If the host
 * board drives GPIO0 LOW at the same time as a reset occurs the display MCU
 * will enter download mode and refuse to run application firmware.
 *
 * Mitigation rules (all three should be applied):
 *   1. Add a 10 kΩ pull-up from GPIO0 to 3V3 on the display side of the IDC
 *      cable.  This ensures a safe idle HIGH even when the host is unpowered.
 *   2. Add a 470 Ω series resistor on each handshake line (GPIO0 and GPIO1)
 *      on the ribbon cable to limit contention current.
 *   3. Configure the host MCU pins corresponding to HOST_REQ (GPIO0) and
 *      DISP_READY (GPIO1) as HIGH-IMPEDANCE INPUTS from power-on until its
 *      own firmware is running.  Never let the host drive those lines during
 *      its own reset/boot sequence.
 *
 * Suggested schematic note (add to IDC connector):
 *   "R_HS1 470Ω series on GPIO0 line; R_HS2 470Ω series on GPIO1 line;
 *    R_PU1 10kΩ pullup GPIO0 → 3V3 (display side)."
 * ──────────────────────────────────────────────────────────────────────────────
 */

#include <Arduino.h>

// ── SPI slave bus ─────────────────────────────────────────────────────────────
constexpr uint8_t HOST_LINK_SPI_SCLK_PIN = 4;  // GPIO4  – SCLK (host drives)
constexpr uint8_t HOST_LINK_SPI_MOSI_PIN = 5;  // GPIO5  – MOSI (host → display)
constexpr uint8_t HOST_LINK_SPI_MISO_PIN = 6;  // GPIO6  – MISO (display → host)
constexpr uint8_t HOST_LINK_SPI_CS_PIN   = 7;  // GPIO7  – CS   (host drives, active LOW)

// ── Handshake lines ───────────────────────────────────────────────────────────
// GPIO0: input on display side.  10kΩ pull-up required (see warning above).
constexpr uint8_t HOST_LINK_HOST_REQ_PIN   = 0;  // GPIO0 – Host→Display request
// GPIO1: output on display side.  Drive HIGH when ready, LOW when busy.
constexpr uint8_t HOST_LINK_DISP_READY_PIN = 1;  // GPIO1 – Display→Host ready

// ── Handshake polarity ────────────────────────────────────────────────────────
// Both signals are ACTIVE HIGH.
//   HOST_REQ   HIGH = host wants a transaction
//   DISP_READY HIGH = display is ready; host may assert CS

// ── SPI bus parameters ────────────────────────────────────────────────────────
constexpr uint32_t HOST_LINK_SPI_FREQ_HZ = 10000000;  // 10 MHz max recommended
constexpr uint8_t  HOST_LINK_SPI_MODE    = SPI_MODE0;  // CPOL=0, CPHA=0

// ── Packet framing constants ──────────────────────────────────────────────────
constexpr uint8_t HOST_LINK_MAGIC_BYTE  = 0xCF;  // 'Captain Fantastic' frame marker
constexpr uint8_t HOST_LINK_PROTO_VER   = 1;
constexpr size_t  HOST_LINK_MAX_PAYLOAD = 128;   // bytes, not including header

#endif  // HOST_LINK_CONFIG_H
