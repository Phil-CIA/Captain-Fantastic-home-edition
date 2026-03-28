#ifndef HOST_LINK_CONFIG_H
#define HOST_LINK_CONFIG_H

// =============================================================================
// display-firmware/include/host_link_config.h
//
// Central pin configuration for the ESP32-C6 display board host-link interface.
//
// This file defines EVERY pin used on the IDC-10 ribbon between the host MCU
// (captain_control) and this display board (ESP32-C6 SPI slave).
//
// CHANGE PINS HERE ONLY — all source files #include this header.
// =============================================================================

#include <Arduino.h>

// -----------------------------------------------------------------------------
// SPI SLAVE — host-link (IDC-10 SPI bus, GPIO4–GPIO7)
//
// The display ESP32-C6 acts as an SPI SLAVE. The host ESP32-C6 (captain_control)
// is the SPI MASTER and initiates every transaction.
//
// Signal    | GPIO | IDC-10 pin | Direction        | Notes
// ----------|------|------------|------------------|-------------------------------
// SCK       |   4  |    2       | Host → Display   | SPI clock driven by master
// MOSI      |   5  |    4       | Host → Display   | Host sends data to display
// MISO      |   6  |    6       | Display → Host   | Display sends data to host
// CS        |   7  |    8       | Host → Display   | Active-LOW chip select
//
// SPI mode: Mode 0 (CPOL=0, CPHA=0) — change DISPLAY_HOST_SPI_MODE if needed.
// Max reliable rate at 20 cm ribbon: 10 MHz. Lower to 4 MHz for breadboard.
// -----------------------------------------------------------------------------
constexpr uint8_t DISPLAY_HOST_SPI_SCK_PIN  =  4;
constexpr uint8_t DISPLAY_HOST_SPI_MOSI_PIN =  5;
constexpr uint8_t DISPLAY_HOST_SPI_MISO_PIN =  6;
constexpr uint8_t DISPLAY_HOST_SPI_CS_PIN   =  7;

constexpr uint8_t  DISPLAY_HOST_SPI_MODE          = 0;
constexpr uint32_t DISPLAY_HOST_SPI_MAX_FREQ_HZ    = 10000000UL;  // 10 MHz
constexpr uint32_t DISPLAY_HOST_SPI_SAFE_FREQ_HZ   =  4000000UL;  //  4 MHz (bench default)
constexpr uint16_t DISPLAY_HOST_SPI_MAX_PACKET_BYTES = 256;

// -----------------------------------------------------------------------------
// HANDSHAKE LINES — GPIO0 and GPIO1 (IDC-10 pins 5 and 9)
//
// These two lines provide flow-control between host and display.
//
// DISP_READY (GPIO1, IDC-10 pin 9)  — Display → Host, ACTIVE-HIGH
//   The display asserts this HIGH when it has a TX packet queued AND the SPI
//   slave receive buffer is ready.  The host MUST NOT assert CS until this
//   line is HIGH.  Asserting CS while DISP_READY is LOW may produce garbage
//   data and desync the packet framing.
//
// HOST_REQ  (GPIO0, IDC-10 pin 5)  — Host → Display, ACTIVE-HIGH
//   The host asserts this HIGH to indicate it has a new STATE packet to send.
//   The display can use this as a "wake" interrupt so it does not need to poll
//   the SPI bus continuously.  Optional: if unused, tie to GND on the host
//   side and comment out the interrupt registration below.
//
// Polarity: ACTIVE-HIGH for both lines.
//   IDLE state  = LOW  (de-asserted)
//   READY/REQ   = HIGH (asserted)
//
// ⚠️  GPIO0 STRAPPING-PIN WARNING — read FLASHING_NOTES.md before wiring:
//   GPIO0 is an ESP32-C6 strapping pin that selects boot mode at power-on.
//   It must read HIGH (or float, as the internal pull-up is enabled) when the
//   board is powered on or reset, or the chip will enter ROM download mode
//   instead of booting normally.  Fit a 10 kΩ pull-up from GPIO0 to 3.3 V on
//   the display board PCB.  The host side must NOT actively drive GPIO0 LOW
//   at power-up.  See FLASHING_NOTES.md § "GPIO0 boot-strap wiring".
//
// ⚠️  GPIO1 UART WARNING:
//   GPIO1 is not a strapping pin but on some ESP32-C6 breakout boards it is
//   routed to UART0 TX (used for Serial.print debug output).  If you need
//   serial logging during bring-up on this pin, reassign the handshake to one
//   of the ALTERNATE_HANDSHAKE pins below and update the host IDC wiring.
// -----------------------------------------------------------------------------
constexpr uint8_t DISPLAY_READY_PIN =  1;   // Display → Host  (IDC-10 pin 9)
constexpr uint8_t HOST_REQ_PIN      =  0;   // Host    → Display (IDC-10 pin 5)

// Active level for both handshake lines.
constexpr bool DISPLAY_HANDSHAKE_ACTIVE_HIGH = true;

// Minimum time the display holds DISP_READY HIGH before host may assert CS.
// This gives the SPI slave DMA time to set up the RX buffer.
constexpr uint16_t DISPLAY_READY_SETUP_US = 10;

// -----------------------------------------------------------------------------
// ALTERNATE HANDSHAKE PINS  (use if GPIO0/GPIO1 cause problems)
//
// If GPIO0 or GPIO1 interfere with boot or UART during development, reroute
// the IDC-10 ribbon to one of these safe general-purpose GPIOs and update the
// constexpr assignments above.
//
// GPIO | Safe? | Notes
// -----|-------|---------------------------------------------------------------
//    2 |  YES  | No strapping role, no default peripheral. Recommended swap.
//    3 |  YES  | No strapping role, no default peripheral.
//   10 |  YES  | No strapping role, no default peripheral.
//   11 |  YES  | No strapping role, no default peripheral.
//    8 |  NO*  | Strapping pin — same category as GPIO0; avoid for handshake.
//    9 |  NO*  | Strapping pin (flash voltage); avoid for handshake.
//   12 |  NO   | USB D- on USB-JTAG boards; do not use if board has USB-JTAG.
//   13 |  NO   | USB D+ on USB-JTAG boards; do not use if board has USB-JTAG.
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// PACKET FRAMING
//
// Every SPI transaction carries exactly one packet in each direction.  The
// display returns a zero-padded EVENT packet in the same transaction as it
// receives a STATE packet (full-duplex).
//
// Packet layout (see captain_protocol.h for field definitions):
//   [0]     magic high byte  (0xC4)
//   [1]     magic low byte   (0xF5)
//   [2]     message type     (DisplayMsgType enum, 1 byte)
//   [3]     protocol version (DISPLAY_PROTOCOL_VERSION)
//   [4..5]  payload length   (uint16_t, little-endian)
//   [6..N]  payload bytes
//   [N+1]   XOR checksum over bytes [0..N]
//
// The magic bytes spell "0xC4F5" — Captain Fantastic Display.
// -----------------------------------------------------------------------------
constexpr uint8_t DISPLAY_PACKET_MAGIC_HI  = 0xC4;
constexpr uint8_t DISPLAY_PACKET_MAGIC_LO  = 0xF5;
constexpr uint8_t DISPLAY_PROTOCOL_VERSION = 1;

enum class DisplayMsgType : uint8_t {
    NOP        = 0x00,
    STATE      = 0x01,   // Host → Display: game state snapshot
    EVENT      = 0x02,   // Display → Host: touch/button event
    ACK        = 0x03,   // Either direction: acknowledge
    RESET      = 0xFF,   // Either direction: soft reset / re-sync
};

#endif // HOST_LINK_CONFIG_H
