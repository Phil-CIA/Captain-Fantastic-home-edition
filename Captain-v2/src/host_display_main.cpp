// host_display_main.cpp
//
// Firmware stub for the host ESP32-C6's display-link side.
//
// The host is the SPI MASTER.  It gates every transaction behind the
// handshake lines so that it never clocks the slave before it is ready.
//
// Handshake lines (IDC ribbon):
//   GPIO0 (IDC pin 5) = REQUEST — driven by this board, read by display
//   GPIO1 (IDC pin 9) = READY   — driven by display, read by this board
//
// SPI MASTER bus pins (to display board, IDC ribbon):
//   MOSI = GPIO4    MISO = GPIO5    SCLK = GPIO6    CS = GPIO7
//
// NOTE: In the full Captain-v2 project the host firmware lives in
//       control_main.cpp which runs on the original ESP32.  This file is
//       the dedicated ESP32-C6 host-side display link for a future split
//       or a stand-alone bench-supply front-panel host.

#include <Arduino.h>
#include <SPI.h>
#include "spi_handshake.h"

// ---------------------------------------------------------------------------
// SPI master bus pin assignment (IDC ribbon, host → display)
// ---------------------------------------------------------------------------
constexpr uint8_t HOST_SPI_MOSI_PIN  = 4;
constexpr uint8_t HOST_SPI_MISO_PIN  = 5;
constexpr uint8_t HOST_SPI_SCLK_PIN  = 6;
constexpr uint8_t HOST_SPI_CS_PIN    = 7;
constexpr uint32_t HOST_SPI_CLOCK_HZ = 1000000UL;  // 1 MHz — safe initial rate

// ---------------------------------------------------------------------------
// Protocol — must match display_main.cpp exactly
// ---------------------------------------------------------------------------
constexpr uint8_t DISP_PACKET_MAGIC   = 0xCF;
constexpr uint8_t DISP_PACKET_VERSION = 1;

enum DisplayMsgType : uint8_t {
    MSG_STATE = 0x01,
    MSG_EVENT = 0x02,
    MSG_ACK   = 0x03,
    MSG_NOP   = 0xFF,
};

struct __attribute__((packed)) DisplayPacketHeader {
    uint8_t  magic;
    uint8_t  version;
    uint8_t  msgType;
    uint8_t  sequence;
    uint16_t payloadLen;
    uint8_t  crc;
};

// ---------------------------------------------------------------------------
// CRC helper — must match display_main.cpp
// ---------------------------------------------------------------------------
static uint8_t calcCrc(const uint8_t* buf, size_t len) {
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc ^= buf[i];
    }
    return crc;
}

// ---------------------------------------------------------------------------
// Build a STATE packet in buf; return total byte count written
// ---------------------------------------------------------------------------
static size_t buildStatePacket(uint8_t* buf, size_t maxLen, uint8_t sequence) {
    if (maxLen < sizeof(DisplayPacketHeader)) {
        return 0;
    }

    auto* hdr = reinterpret_cast<DisplayPacketHeader*>(buf);
    hdr->magic      = DISP_PACKET_MAGIC;
    hdr->version    = DISP_PACKET_VERSION;
    hdr->msgType    = MSG_STATE;
    hdr->sequence   = sequence;
    hdr->payloadLen = 0;  // no payload for now; add voltage/current fields here

    hdr->crc = calcCrc(buf, sizeof(DisplayPacketHeader) - 1);
    return sizeof(DisplayPacketHeader);
}

// ---------------------------------------------------------------------------
// Perform one guarded SPI master transaction
// Returns true on success, false on handshake timeout.
// ---------------------------------------------------------------------------
static bool runMasterTransaction(const uint8_t* txBuf, size_t txLen,
                                 uint8_t* rxBuf, size_t rxLen) {
    // 1. Assert REQUEST so the display starts preparing its slave buffer
    spiHandshakeHostSetRequest(true);

    // 2. Wait for the display to assert READY
    const bool ready = spiHandshakeHostWaitReady(SPI_HANDSHAKE_READY_TIMEOUT_MS);
    if (!ready) {
        Serial.println("[HOST] transaction aborted: display did not signal READY");
        spiHandshakeHostSetRequest(false);
        return false;
    }

    // 3. READY is HIGH — clock the transaction
    SPI.beginTransaction(SPISettings(HOST_SPI_CLOCK_HZ, MSBFIRST, SPI_MODE0));
    digitalWrite(HOST_SPI_CS_PIN, LOW);

    for (size_t i = 0; i < txLen || i < rxLen; i++) {
        const uint8_t out = (i < txLen) ? txBuf[i] : 0x00;
        const uint8_t in  = SPI.transfer(out);
        if (rxBuf && i < rxLen) {
            rxBuf[i] = in;
        }
    }

    digitalWrite(HOST_SPI_CS_PIN, HIGH);
    SPI.endTransaction();

    // 4. Deassert REQUEST — transaction complete
    spiHandshakeHostSetRequest(false);

    return true;
}

// ---------------------------------------------------------------------------
// Arduino entry points
// ---------------------------------------------------------------------------

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("[HOST] host display-link setup start");

    // Configure SPI master bus
    pinMode(HOST_SPI_CS_PIN, OUTPUT);
    digitalWrite(HOST_SPI_CS_PIN, HIGH);  // CS idle HIGH

    SPI.begin(HOST_SPI_SCLK_PIN, HOST_SPI_MISO_PIN, HOST_SPI_MOSI_PIN, HOST_SPI_CS_PIN);
    Serial.printf("[HOST] SPI master: MOSI=GPIO%u MISO=GPIO%u SCLK=GPIO%u CS=GPIO%u @ %luHz\n",
                  HOST_SPI_MOSI_PIN, HOST_SPI_MISO_PIN, HOST_SPI_SCLK_PIN, HOST_SPI_CS_PIN,
                  static_cast<unsigned long>(HOST_SPI_CLOCK_HZ));

    // Configure handshake lines (host side)
    spiHandshakeHostInit();

    // --- Debug assert: verify REQUEST idles LOW after init ---
    const bool requestIdle = (digitalRead(SPI_HANDSHAKE_REQUEST_PIN) == SPI_HANDSHAKE_IDLE);
    if (!requestIdle) {
        Serial.println("[HS] ASSERT FAIL: REQUEST not idle after init — check wiring");
    } else {
        Serial.println("[HS] REQUEST line: idle LOW — OK");
    }

    // --- Bring-up test: check READY pin reads LOW when display is not ready ---
    const bool readyIdle = !spiHandshakeHostIsDisplayReady();
    if (!readyIdle) {
        Serial.println("[HS] WARNING: READY is HIGH at startup — display may already be signalling or line is floating");
    } else {
        Serial.println("[HS] READY line: reads LOW at startup — OK");
    }

    Serial.println("[HOST] host display-link ready");
}

void loop() {
    static uint32_t lastTx = 0;
    static uint8_t  txSeq  = 0;

    const uint32_t now = millis();

    // Send a STATE packet to the display every 50 ms
    if (now - lastTx >= 50) {
        lastTx = now;

        uint8_t txBuf[sizeof(DisplayPacketHeader)] = {};
        uint8_t rxBuf[sizeof(DisplayPacketHeader)] = {};

        const size_t txLen = buildStatePacket(txBuf, sizeof(txBuf), txSeq);

        if (txLen > 0) {
            const bool ok = runMasterTransaction(txBuf, txLen, rxBuf, sizeof(rxBuf));
            if (ok) {
                txSeq++;
                Serial.printf("[HOST] tx STATE seq=%u OK\n", static_cast<unsigned>(txSeq - 1));
            }
        }
    }

    // Periodic heartbeat log (once per second)
    static uint32_t lastHeartbeat = 0;
    if (now - lastHeartbeat >= 1000) {
        lastHeartbeat = now;
        Serial.printf("[HOST] alive uptime=%lums REQUEST=%u READY=%u\n",
                      static_cast<unsigned long>(now),
                      digitalRead(SPI_HANDSHAKE_REQUEST_PIN),
                      digitalRead(SPI_HANDSHAKE_READY_PIN));
    }
}
