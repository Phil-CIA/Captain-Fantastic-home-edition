#ifndef HOST_LINK_H
#define HOST_LINK_H

// =============================================================================
// display-firmware/src/host_link.h
//
// SPI slave + handshake driver for the ESP32-C6 display board.
//
// The host (captain_control ESP32-C6) is the SPI MASTER; this board is the
// SLAVE.  GPIO4–7 carry the SPI bus; GPIO0 (HOST_REQ) and GPIO1 (DISP_READY)
// carry the handshake signals (see host_link_config.h for pin details and the
// GPIO0 strapping-pin warning).
//
// Typical transaction sequence:
//   1. Host asserts HOST_REQ (GPIO0 HIGH).
//   2. Display finishes current frame work, queues TX event packet, then
//      asserts DISP_READY (GPIO1 HIGH).
//   3. Host waits for DISP_READY, then asserts CS (GPIO7 LOW) and clocks the
//      SPI transaction (full-duplex: host sends STATE, display sends EVENT).
//   4. Both sides de-assert their handshake line after the transaction ends.
// =============================================================================

#include <Arduino.h>
#include "host_link_config.h"

// ---------------------------------------------------------------------------
// HostLinkStats — counters updated on every transaction for diagnostics.
// ---------------------------------------------------------------------------
struct HostLinkStats {
    uint32_t rxPackets;      // Total packets received from host
    uint32_t txPackets;      // Total packets sent to host
    uint32_t rxErrors;       // Checksum or magic mismatches
    uint32_t txDropped;      // TX packets dropped (no ready event to send)
    uint32_t hostReqCount;   // Number of HOST_REQ rising edges seen
};

// ---------------------------------------------------------------------------
// hostLinkInit()
//
// Call once from setup().  Configures:
//   - GPIO0 (HOST_REQ) as input with internal pull-down + interrupt on rising
//   - GPIO1 (DISP_READY) as output, initially LOW
//   - SPI2 slave peripheral on GPIO4–7
//
// Note: if GPIO1 is assigned as Serial TX on your dev board, disable Serial
// before calling this, or change DISPLAY_READY_PIN in host_link_config.h.
// ---------------------------------------------------------------------------
void hostLinkInit();

// ---------------------------------------------------------------------------
// hostLinkService()
//
// Call from loop() or a dedicated FreeRTOS task.  Non-blocking: returns
// immediately if no transaction is pending.
//
// When a complete packet is received it is copied into rxBuf (up to rxBufLen
// bytes) and returns true.  Returns false if no new data is ready.
// ---------------------------------------------------------------------------
bool hostLinkService(uint8_t* rxBuf, uint16_t rxBufLen);

// ---------------------------------------------------------------------------
// hostLinkQueueEvent()
//
// Queue an EVENT packet to be sent to the host on the next SPI transaction.
// If a packet is already queued (not yet sent), the new packet replaces it.
// Call this after detecting a touch event or button press.
// ---------------------------------------------------------------------------
void hostLinkQueueEvent(const uint8_t* payload, uint16_t payloadLen);

// ---------------------------------------------------------------------------
// hostLinkSignalReady()
//
// Manually assert DISP_READY without waiting for a HOST_REQ.  Useful when
// the display has an unsolicited event to push (e.g., a touch that happened
// between host polls).
// ---------------------------------------------------------------------------
void hostLinkSignalReady();

// ---------------------------------------------------------------------------
// hostLinkGetStats() — returns a snapshot of the internal counters.
// ---------------------------------------------------------------------------
HostLinkStats hostLinkGetStats();

// ---------------------------------------------------------------------------
// hostLinkDumpStats() — prints stats to Serial for bring-up diagnostics.
// ---------------------------------------------------------------------------
void hostLinkDumpStats();

#endif // HOST_LINK_H
