#ifndef SPI_HANDSHAKE_CONFIG_H
#define SPI_HANDSHAKE_CONFIG_H

// ---------------------------------------------------------------------------
// SPI Handshake GPIO Configuration
//
// Two extra GPIO lines added to the IDC ribbon between host ESP32-C6 and
// display-board ESP32-C6 for RTS/CTS-style flow control.
//
// Physical wiring:
//   GPIO0  <-> IDC pin 5  =  REQUEST  (host → display)
//   GPIO1  <-> IDC pin 9  =  READY    (display → host)
//
// Signal polarity: ACTIVE HIGH (3.3V logic)
//   REQUEST HIGH = host wants to initiate an SPI transaction
//   READY   HIGH = display slave buffers are loaded and ready to transact
//
// The master (host) MUST NOT assert CS or clock SCLK until READY is HIGH.
// ---------------------------------------------------------------------------

// Pin numbers used on BOTH the host ESP32-C6 and the display ESP32-C6.
// The same physical GPIO numbers land on IDC pins 5 and 9 on each board.
constexpr uint8_t SPI_HANDSHAKE_REQUEST_PIN = 0;  // GPIO0, IDC pin 5 (output on host, input on display)
constexpr uint8_t SPI_HANDSHAKE_READY_PIN   = 1;  // GPIO1, IDC pin 9 (output on display, input on host)

// Active signal level (change to LOW for active-low hardware)
constexpr uint8_t SPI_HANDSHAKE_ACTIVE = HIGH;
constexpr uint8_t SPI_HANDSHAKE_IDLE   = LOW;

// How long the host will wait for the READY signal before declaring a timeout
constexpr uint32_t SPI_HANDSHAKE_READY_TIMEOUT_MS = 100;

// Minimum pulse width for the REQUEST line (µs) so the display can detect it
constexpr uint32_t SPI_HANDSHAKE_REQUEST_PULSE_US = 10;

// Enable/disable Serial debug logging in the handshake module
#ifndef SPI_HANDSHAKE_DEBUG
#define SPI_HANDSHAKE_DEBUG 1
#endif

#endif // SPI_HANDSHAKE_CONFIG_H
