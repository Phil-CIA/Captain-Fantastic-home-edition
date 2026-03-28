// display_main.cpp
//
// Firmware entry point for the display-board ESP32-C6.
//
// Role: SPI SLAVE to the host ESP32-C6 over an IDC-10 ribbon cable.
//       Also SPI MASTER to the TFT display (ST7796S), touch (XPT2046),
//       and SD card (separate CS lines).
//
// Handshake lines (IDC ribbon):
//   GPIO1 (IDC pin 9) = READY   — driven by this board, read by host
//   GPIO0 (IDC pin 5) = REQUEST — driven by host, read by this board
//
// SPI SLAVE bus (from host, IDC ribbon):
//   MOSI  = GPIO4    MISO = GPIO5    SCLK = GPIO6    CS = GPIO7
//
// SPI MASTER bus (to TFT/touch/SD):
//   Configured separately in your TFT_eSPI User_Setup.h.

#include <Arduino.h>
#include <SPI.h>
#include "spi_handshake.h"

// ---------------------------------------------------------------------------
// SPI slave bus pin assignment (IDC ribbon, host → display)
// ---------------------------------------------------------------------------
constexpr uint8_t DISP_SPI_MOSI_PIN = 4;
constexpr uint8_t DISP_SPI_MISO_PIN = 5;
constexpr uint8_t DISP_SPI_SCLK_PIN = 6;
constexpr uint8_t DISP_SPI_CS_PIN   = 7;

// ---------------------------------------------------------------------------
// Protocol — packet layout shared with host_display_main.cpp
// ---------------------------------------------------------------------------
constexpr uint8_t DISP_PACKET_MAGIC   = 0xCF;
constexpr uint8_t DISP_PACKET_VERSION = 1;

enum DisplayMsgType : uint8_t {
    MSG_STATE = 0x01,  // Host → display: full state snapshot
    MSG_EVENT = 0x02,  // Display → host: UI event (touch, setpoint, etc.)
    MSG_ACK   = 0x03,  // Either direction: acknowledge previous packet
    MSG_NOP   = 0xFF,  // Filler / keep-alive
};

struct __attribute__((packed)) DisplayPacketHeader {
    uint8_t  magic;
    uint8_t  version;
    uint8_t  msgType;
    uint8_t  sequence;
    uint16_t payloadLen;
    uint8_t  crc;      // simple XOR of all preceding header bytes + payload
};

// ---------------------------------------------------------------------------
// Receive / transmit buffers
// In a production build these are handed directly to the ESP32SPISlave
// library (hideakitai/ESP32SPISlave) which fills them via DMA.
// ---------------------------------------------------------------------------
constexpr size_t DISP_MAX_PACKET_BYTES = 64;
static uint8_t rxBuffer[DISP_MAX_PACKET_BYTES];
static uint8_t txBuffer[DISP_MAX_PACKET_BYTES];
static volatile bool frameReceived = false;

// ---------------------------------------------------------------------------
// CRC helper — single-byte XOR over the buffer
// ---------------------------------------------------------------------------
static uint8_t calcCrc(const uint8_t* buf, size_t len) {
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc ^= buf[i];
    }
    return crc;
}

// ---------------------------------------------------------------------------
// Prepare a minimal NOP/ACK reply packet in txBuffer
// ---------------------------------------------------------------------------
static void prepareTxNop(uint8_t sequence) {
    DisplayPacketHeader hdr = {};
    hdr.magic      = DISP_PACKET_MAGIC;
    hdr.version    = DISP_PACKET_VERSION;
    hdr.msgType    = MSG_NOP;
    hdr.sequence   = sequence;
    hdr.payloadLen = 0;
    hdr.crc        = calcCrc(reinterpret_cast<const uint8_t*>(&hdr),
                              sizeof(hdr) - 1);
    memcpy(txBuffer, &hdr, sizeof(hdr));
    memset(txBuffer + sizeof(hdr), 0, DISP_MAX_PACKET_BYTES - sizeof(hdr));
}

// ---------------------------------------------------------------------------
// Process a received packet from the host
// ---------------------------------------------------------------------------
static void processRxPacket(const uint8_t* buf, size_t len) {
    if (len < sizeof(DisplayPacketHeader)) {
        Serial.printf("[DISP] rx: packet too short (%u bytes)\n",
                      static_cast<unsigned>(len));
        return;
    }

    const auto* hdr = reinterpret_cast<const DisplayPacketHeader*>(buf);

    if (hdr->magic != DISP_PACKET_MAGIC) {
        Serial.printf("[DISP] rx: bad magic 0x%02X\n", hdr->magic);
        return;
    }

    if (hdr->version != DISP_PACKET_VERSION) {
        Serial.printf("[DISP] rx: unsupported version %u\n", hdr->version);
        return;
    }

    const size_t totalLen = sizeof(DisplayPacketHeader) + hdr->payloadLen;
    if (totalLen > len) {
        Serial.printf("[DISP] rx: truncated (need %u got %u)\n",
                      static_cast<unsigned>(totalLen),
                      static_cast<unsigned>(len));
        return;
    }

    const uint8_t expectedCrc = calcCrc(buf, totalLen - 1);
    if (expectedCrc != hdr->crc) {
        Serial.printf("[DISP] rx: CRC mismatch (got 0x%02X expect 0x%02X)\n",
                      hdr->crc, expectedCrc);
        return;
    }

    Serial.printf("[DISP] rx: type=0x%02X seq=%u payloadLen=%u\n",
                  hdr->msgType, hdr->sequence, hdr->payloadLen);

    // TODO: dispatch to UI renderer based on hdr->msgType
}

// ---------------------------------------------------------------------------
// Perform one SPI slave transaction guarded by the handshake lines.
//
// Production note: replace the body of this function with your chosen
// slave driver.  The recommended approach for ESP32-C6 + Arduino is:
//
//   ESP32SPISlave slave;              // hideakitai/ESP32SPISlave library
//   slave.setDataMode(SPI_MODE0);
//   slave.begin(VSPI, DISP_SPI_SCLK_PIN, DISP_SPI_MISO_PIN,
//               DISP_SPI_MOSI_PIN, DISP_SPI_CS_PIN);
//   slave.queue(rxBuffer, txBuffer, sizeof(txBuffer));
//   // …then signal READY and wait for slave.numTransactionCompleted()…
//
// The handshake calls (spiHandshakeDisplaySignalReady / IsRequestPending)
// are the stable part of this function regardless of driver choice.
// ---------------------------------------------------------------------------
static void runSlaveTransaction() {
    static uint8_t txSequence = 0;

    // 1. Prepare the TX buffer BEFORE signalling READY
    prepareTxNop(txSequence);

    // 2. Signal READY — host may now assert CS and clock data
    spiHandshakeDisplaySignalReady(true);

    // 3. Wait for CS to be asserted (≤ 10 ms).
    //    Replace this polling loop with your driver's blocking queue call.
    const uint32_t csDeadline = millis() + 10;
    while (digitalRead(DISP_SPI_CS_PIN) == HIGH) {
        if (millis() >= csDeadline) {
            Serial.println("[DISP] transaction: CS not asserted within 10ms — skipping");
            spiHandshakeDisplaySignalReady(false);
            return;
        }
        yield();  // allow background tasks while waiting
    }

    // 4. CS is LOW — receive bytes from MOSI, one full byte per 8 clock edges.
    //    This is a minimal bit-bang implementation for bring-up only.
    //    Swap for ESP32SPISlave / esp-idf spi_slave DMA in production.
    size_t bytesReceived = 0;
    while (digitalRead(DISP_SPI_CS_PIN) == LOW &&
           bytesReceived < DISP_MAX_PACKET_BYTES) {
        uint8_t byte = 0;
        for (int8_t bit = 7; bit >= 0; bit--) {
            // Wait for rising edge of SCLK (sample on rising edge = SPI_MODE0)
            while (digitalRead(DISP_SPI_SCLK_PIN) == LOW) {
                if (digitalRead(DISP_SPI_CS_PIN) == HIGH) {
                    goto cs_deasserted;  // CS dropped mid-byte; abort cleanly
                }
            }
            if (digitalRead(DISP_SPI_MOSI_PIN) == HIGH) {
                byte |= static_cast<uint8_t>(1u << bit);
            }
            // Wait for falling edge before next bit
            while (digitalRead(DISP_SPI_SCLK_PIN) == HIGH) {
                if (digitalRead(DISP_SPI_CS_PIN) == HIGH) {
                    goto cs_deasserted;
                }
            }
        }
        rxBuffer[bytesReceived++] = byte;
    }

cs_deasserted:
    // 5. Deassert READY — we are busy processing the received frame
    spiHandshakeDisplaySignalReady(false);
    txSequence++;

    if (bytesReceived > 0) {
        processRxPacket(rxBuffer, bytesReceived);
    }
}

// ---------------------------------------------------------------------------
// Arduino entry points
// ---------------------------------------------------------------------------

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("[DISP] display board setup start");

    // Configure SPI slave CS as input
    pinMode(DISP_SPI_CS_PIN,   INPUT);
    pinMode(DISP_SPI_MOSI_PIN, INPUT);
    pinMode(DISP_SPI_SCLK_PIN, INPUT);
    pinMode(DISP_SPI_MISO_PIN, OUTPUT);

    // Configure handshake lines (display side)
    spiHandshakeDisplayInit();

    // --- Debug assert: verify READY idles LOW after init ---
    const bool readyIdle = (digitalRead(SPI_HANDSHAKE_READY_PIN) == SPI_HANDSHAKE_IDLE);
    if (!readyIdle) {
        Serial.println("[HS] ASSERT FAIL: READY not idle after init — check wiring");
    } else {
        Serial.println("[HS] READY line: idle LOW — OK");
    }

    Serial.printf("[DISP] SPI slave: MOSI=GPIO%u MISO=GPIO%u SCLK=GPIO%u CS=GPIO%u\n",
                  DISP_SPI_MOSI_PIN, DISP_SPI_MISO_PIN, DISP_SPI_SCLK_PIN, DISP_SPI_CS_PIN);
    Serial.println("[DISP] display board ready");
}

void loop() {
    // Check if host is requesting a transaction
    if (spiHandshakeDisplayIsRequestPending()) {
        Serial.println("[DISP] REQUEST detected from host — running slave transaction");
        runSlaveTransaction();
    }

    // Periodic heartbeat log (once per second)
    static uint32_t lastHeartbeat = 0;
    const uint32_t now = millis();
    if (now - lastHeartbeat >= 1000) {
        lastHeartbeat = now;
        Serial.printf("[DISP] alive uptime=%lums READY=%u REQUEST=%u\n",
                      static_cast<unsigned long>(now),
                      digitalRead(SPI_HANDSHAKE_READY_PIN),
                      digitalRead(SPI_HANDSHAKE_REQUEST_PIN));
    }
}
