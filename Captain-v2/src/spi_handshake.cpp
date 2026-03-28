#include "spi_handshake.h"

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static inline void hsWrite(uint8_t pin, bool active) {
    digitalWrite(pin, active ? SPI_HANDSHAKE_ACTIVE : SPI_HANDSHAKE_IDLE);
}

static inline bool hsRead(uint8_t pin) {
    return digitalRead(pin) == SPI_HANDSHAKE_ACTIVE;
}

// ---------------------------------------------------------------------------
// HOST-SIDE implementation
// ---------------------------------------------------------------------------

void spiHandshakeHostInit() {
    // REQUEST is an output driven by the host
    pinMode(SPI_HANDSHAKE_REQUEST_PIN, OUTPUT);
    hsWrite(SPI_HANDSHAKE_REQUEST_PIN, false);

    // READY is an input read by the host (display drives it)
    pinMode(SPI_HANDSHAKE_READY_PIN, INPUT);

#if SPI_HANDSHAKE_DEBUG
    Serial.printf("[HS] host init: REQUEST=GPIO%u (OUT) READY=GPIO%u (IN)\n",
                  SPI_HANDSHAKE_REQUEST_PIN, SPI_HANDSHAKE_READY_PIN);
#endif
}

void spiHandshakeHostSetRequest(bool active) {
    hsWrite(SPI_HANDSHAKE_REQUEST_PIN, active);
#if SPI_HANDSHAKE_DEBUG
    Serial.printf("[HS] host REQUEST -> %s\n", active ? "HIGH (active)" : "LOW (idle)");
#endif
}

bool spiHandshakeHostIsDisplayReady() {
    return hsRead(SPI_HANDSHAKE_READY_PIN);
}

bool spiHandshakeHostWaitReady(uint32_t timeoutMs) {
    const uint32_t deadline = millis() + timeoutMs;
    while (!spiHandshakeHostIsDisplayReady()) {
        if (millis() >= deadline) {
#if SPI_HANDSHAKE_DEBUG
            Serial.printf("[HS] host READY wait timeout (%lums)\n",
                          static_cast<unsigned long>(timeoutMs));
#endif
            return false;
        }
        delayMicroseconds(50);
    }
    return true;
}

// ---------------------------------------------------------------------------
// DISPLAY-SIDE implementation
// ---------------------------------------------------------------------------

void spiHandshakeDisplayInit() {
    // READY is an output driven by the display
    pinMode(SPI_HANDSHAKE_READY_PIN, OUTPUT);
    hsWrite(SPI_HANDSHAKE_READY_PIN, false);

    // REQUEST is an input read by the display (host drives it)
    pinMode(SPI_HANDSHAKE_REQUEST_PIN, INPUT);

#if SPI_HANDSHAKE_DEBUG
    Serial.printf("[HS] display init: READY=GPIO%u (OUT) REQUEST=GPIO%u (IN)\n",
                  SPI_HANDSHAKE_READY_PIN, SPI_HANDSHAKE_REQUEST_PIN);
#endif
}

void spiHandshakeDisplaySignalReady(bool ready) {
    hsWrite(SPI_HANDSHAKE_READY_PIN, ready);
#if SPI_HANDSHAKE_DEBUG
    Serial.printf("[HS] display READY -> %s\n", ready ? "HIGH (ready)" : "LOW (busy)");
#endif
}

bool spiHandshakeDisplayIsRequestPending() {
    return hsRead(SPI_HANDSHAKE_REQUEST_PIN);
}
