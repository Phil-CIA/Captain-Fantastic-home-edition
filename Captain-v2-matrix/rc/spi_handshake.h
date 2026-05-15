#ifndef SPI_HANDSHAKE_H
#define SPI_HANDSHAKE_H

// ---------------------------------------------------------------------------
// SPI Handshake Module
//
// Provides a small, self-contained abstraction for the two-wire RTS/CTS
// handshake between the host ESP32-C6 (SPI master) and the display-board
// ESP32-C6 (SPI slave).
//
// Signal roles:
//   REQUEST (GPIO0, IDC pin 5): HOST drives HIGH to signal a transaction is
//                               coming; DISPLAY reads this line.
//   READY   (GPIO1, IDC pin 9): DISPLAY drives HIGH when its SPI slave buffers
//                               are loaded and it is ready to transact; HOST
//                               reads this line.
//
// Typical transaction flow:
//   1. Display calls spiHandshakeDisplaySignalReady(true)
//      after loading its slave TX buffer.
//   2. Host sees READY HIGH (spiHandshakeHostWaitReady) and asserts CS.
//   3. Host clocks the SPI transaction.
//   4. Host deasserts CS, then calls spiHandshakeHostSetRequest(false).
//   5. Display deasserts READY after processing the received frame:
//      spiHandshakeDisplaySignalReady(false).
//
// Both sides call their respective Init function once in setup().
// ---------------------------------------------------------------------------

#include <Arduino.h>
#include "spi_handshake_config.h"

// ---------------------------------------------------------------------------
// HOST-SIDE API (call from the host/master ESP32-C6 firmware)
// ---------------------------------------------------------------------------

// Configure REQUEST as output (driven by host) and READY as input.
// Call once in setup() on the host board.
void spiHandshakeHostInit();

// Drive the REQUEST line HIGH or LOW.
// Set active=true before preparing a transfer; set false after CS deasserts.
void spiHandshakeHostSetRequest(bool active);

// Return true if the READY line is currently asserted by the display.
bool spiHandshakeHostIsDisplayReady();

// Block until READY is asserted or timeoutMs elapses.
// Returns true if READY was seen within the timeout, false on timeout.
bool spiHandshakeHostWaitReady(uint32_t timeoutMs = SPI_HANDSHAKE_READY_TIMEOUT_MS);

// ---------------------------------------------------------------------------
// DISPLAY-SIDE API (call from the display/slave ESP32-C6 firmware)
// ---------------------------------------------------------------------------

// Configure READY as output (driven by display) and REQUEST as input.
// Call once in setup() on the display board.
void spiHandshakeDisplayInit();

// Drive the READY line HIGH (ready) or LOW (busy).
// Call with ready=true once the SPI slave buffer is loaded and the peripheral
// is armed; call with ready=false after the transaction is processed.
void spiHandshakeDisplaySignalReady(bool ready);

// Return true if the host is currently asserting the REQUEST line.
bool spiHandshakeDisplayIsRequestPending();

#endif // SPI_HANDSHAKE_H
