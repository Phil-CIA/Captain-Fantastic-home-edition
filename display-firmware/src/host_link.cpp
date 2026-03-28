// =============================================================================
// display-firmware/src/host_link.cpp
//
// SPI slave + handshake implementation for the ESP32-C6 display board.
//
// ⚠️  BRING-UP NOTE (read before flashing):
//   GPIO0 is an ESP32-C6 strapping pin.  Before powering on the board, make
//   sure a 10 kΩ pull-up resistor is fitted between GPIO0 and 3.3 V on the
//   display PCB.  The host must not drive GPIO0 (HOST_REQ) LOW while the
//   display board is being powered on or reset.  See FLASHING_NOTES.md for
//   complete wiring requirements.
// =============================================================================

#include "host_link.h"
#include <SPI.h>
#include <driver/spi_slave.h>

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------
static volatile bool     s_hostReqSeen   = false;
static volatile bool     s_dispReady     = false;

// TX/RX scratch buffers aligned for SPI DMA (4-byte alignment required by
// the ESP-IDF SPI slave driver when DMA is enabled).
static uint8_t WORD_ALIGNED_ATTR s_txBuf[DISPLAY_HOST_SPI_MAX_PACKET_BYTES];
static uint8_t WORD_ALIGNED_ATTR s_rxBuf[DISPLAY_HOST_SPI_MAX_PACKET_BYTES];

// Queued outgoing event (set by hostLinkQueueEvent; cleared after TX).
static uint8_t  s_eventBuf[DISPLAY_HOST_SPI_MAX_PACKET_BYTES];
static uint16_t s_eventLen   = 0;
static bool     s_eventReady = false;

static HostLinkStats s_stats = {};

// ---------------------------------------------------------------------------
// Packet helpers
// ---------------------------------------------------------------------------
static uint8_t computeChecksum(const uint8_t* data, uint16_t len) {
    uint8_t xorVal = 0;
    for (uint16_t i = 0; i < len; i++) {
        xorVal ^= data[i];
    }
    return xorVal;
}

static uint16_t buildPacket(uint8_t* buf, DisplayMsgType type,
                             const uint8_t* payload, uint16_t payloadLen) {
    if (payloadLen + 7 > DISPLAY_HOST_SPI_MAX_PACKET_BYTES) {
        payloadLen = static_cast<uint16_t>(DISPLAY_HOST_SPI_MAX_PACKET_BYTES - 7);
    }
    buf[0] = DISPLAY_PACKET_MAGIC_HI;
    buf[1] = DISPLAY_PACKET_MAGIC_LO;
    buf[2] = static_cast<uint8_t>(type);
    buf[3] = DISPLAY_PROTOCOL_VERSION;
    buf[4] = static_cast<uint8_t>(payloadLen & 0xFF);
    buf[5] = static_cast<uint8_t>((payloadLen >> 8) & 0xFF);
    if (payload != nullptr && payloadLen > 0) {
        memcpy(&buf[6], payload, payloadLen);
    }
    const uint16_t bodyLen = static_cast<uint16_t>(6 + payloadLen);
    buf[bodyLen] = computeChecksum(buf, bodyLen);
    return static_cast<uint16_t>(bodyLen + 1);
}

static bool validatePacket(const uint8_t* buf, uint16_t len) {
    if (len < 7) {
        return false;
    }
    if (buf[0] != DISPLAY_PACKET_MAGIC_HI || buf[1] != DISPLAY_PACKET_MAGIC_LO) {
        return false;
    }
    const uint16_t payloadLen = static_cast<uint16_t>(buf[4] | (buf[5] << 8));
    const uint16_t expectedTotal = static_cast<uint16_t>(7 + payloadLen);
    if (expectedTotal > len) {
        return false;
    }
    const uint8_t rxChecksum = buf[expectedTotal - 1];
    const uint8_t calcChecksum = computeChecksum(buf, static_cast<uint16_t>(expectedTotal - 1));
    return rxChecksum == calcChecksum;
}

// ---------------------------------------------------------------------------
// HOST_REQ interrupt handler (GPIO0, rising edge)
// ---------------------------------------------------------------------------
static void IRAM_ATTR onHostReq() {
    s_hostReqSeen = true;
    s_stats.hostReqCount++;
}

// ---------------------------------------------------------------------------
// Handshake helpers
// ---------------------------------------------------------------------------
static void setDispReady(bool ready) {
    s_dispReady = ready;
    const uint8_t level = ready
                          ? (DISPLAY_HANDSHAKE_ACTIVE_HIGH ? HIGH : LOW)
                          : (DISPLAY_HANDSHAKE_ACTIVE_HIGH ? LOW  : HIGH);
    digitalWrite(DISPLAY_READY_PIN, level);
}

// ---------------------------------------------------------------------------
// hostLinkInit
// ---------------------------------------------------------------------------
void hostLinkInit() {
    // --- DISP_READY output (GPIO1) ---
    // Drive LOW initially; raised only when an SPI buffer is queued.
    pinMode(DISPLAY_READY_PIN, OUTPUT);
    setDispReady(false);

    // --- HOST_REQ input (GPIO0) ---
    // Internal pull-down: the line idles LOW; host drives HIGH to request a
    // transaction.  The external 10 kΩ pull-up fitted on the display PCB
    // ensures GPIO0 reads HIGH at power-on (normal boot mode) even before the
    // host starts driving.  See FLASHING_NOTES.md § "GPIO0 boot-strap wiring".
    pinMode(HOST_REQ_PIN, INPUT_PULLDOWN);
    attachInterrupt(digitalPinToInterrupt(HOST_REQ_PIN), onHostReq, RISING);

    // --- SPI slave peripheral on GPIO4–7 ---
    spi_bus_config_t busCfg = {};
    busCfg.mosi_io_num     = DISPLAY_HOST_SPI_MOSI_PIN;
    busCfg.miso_io_num     = DISPLAY_HOST_SPI_MISO_PIN;
    busCfg.sclk_io_num     = DISPLAY_HOST_SPI_SCK_PIN;
    busCfg.quadwp_io_num   = -1;
    busCfg.quadhd_io_num   = -1;
    busCfg.max_transfer_sz = DISPLAY_HOST_SPI_MAX_PACKET_BYTES;

    spi_slave_interface_config_t slaveCfg = {};
    slaveCfg.mode        = DISPLAY_HOST_SPI_MODE;
    slaveCfg.spics_io_num = DISPLAY_HOST_SPI_CS_PIN;
    slaveCfg.queue_size  = 3;
    slaveCfg.flags       = 0;
    slaveCfg.post_setup_cb  = nullptr;
    slaveCfg.post_trans_cb  = nullptr;

    const esp_err_t err = spi_slave_initialize(SPI2_HOST, &busCfg, &slaveCfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        Serial.printf("[host_link] spi_slave_initialize failed: %d\n", err);
        return;
    }

    Serial.printf("[host_link] SPI slave ready. SCK=%d MOSI=%d MISO=%d CS=%d\n",
                  DISPLAY_HOST_SPI_SCK_PIN,
                  DISPLAY_HOST_SPI_MOSI_PIN,
                  DISPLAY_HOST_SPI_MISO_PIN,
                  DISPLAY_HOST_SPI_CS_PIN);
    Serial.printf("[host_link] Handshake: DISP_READY=GPIO%d HOST_REQ=GPIO%d\n",
                  DISPLAY_READY_PIN, HOST_REQ_PIN);
}

// ---------------------------------------------------------------------------
// hostLinkService  — call from loop() or a FreeRTOS task
// ---------------------------------------------------------------------------
bool hostLinkService(uint8_t* rxBufOut, uint16_t rxBufLen) {
    // Only proceed if the host has requested a transaction or the display has
    // an unsolicited event to push.
    if (!s_hostReqSeen && !s_eventReady) {
        return false;
    }

    // Build the TX packet: queued event or a NOP.
    uint16_t txLen;
    if (s_eventReady) {
        txLen = buildPacket(s_txBuf, DisplayMsgType::EVENT, s_eventBuf, s_eventLen);
    } else {
        txLen = buildPacket(s_txBuf, DisplayMsgType::NOP, nullptr, 0);
    }

    // Zero-pad the TX buffer to a full packet slot so the slave DMA length
    // matches what the host expects.
    if (txLen < DISPLAY_HOST_SPI_MAX_PACKET_BYTES) {
        memset(&s_txBuf[txLen], 0, static_cast<size_t>(DISPLAY_HOST_SPI_MAX_PACKET_BYTES - txLen));
    }

    // Queue the SPI slave transaction.
    spi_slave_transaction_t trans = {};
    trans.length    = static_cast<size_t>(DISPLAY_HOST_SPI_MAX_PACKET_BYTES * 8);  // bits
    trans.tx_buffer = s_txBuf;
    trans.rx_buffer = s_rxBuf;

    const esp_err_t queueErr = spi_slave_queue_trans(SPI2_HOST, &trans, pdMS_TO_TICKS(1));
    if (queueErr != ESP_OK) {
        return false;
    }

    // Assert DISP_READY to tell the host the slave buffer is primed.
    delayMicroseconds(DISPLAY_READY_SETUP_US);
    setDispReady(true);

    // Wait for the transaction to complete (host clocks the bus).
    spi_slave_transaction_t* retTrans = nullptr;
    const esp_err_t waitErr = spi_slave_get_trans_result(SPI2_HOST, &retTrans,
                                                          pdMS_TO_TICKS(100));

    // De-assert DISP_READY and clear HOST_REQ flag regardless of outcome.
    setDispReady(false);
    s_hostReqSeen = false;

    if (waitErr != ESP_OK || retTrans == nullptr) {
        return false;
    }

    // Mark event as sent if we transmitted one.
    if (s_eventReady) {
        s_eventReady = false;
        s_stats.txPackets++;
    }
    // NOP was sent — not a dropped event, just no event was pending.

    // Validate and return the received packet.
    if (!validatePacket(s_rxBuf, DISPLAY_HOST_SPI_MAX_PACKET_BYTES)) {
        s_stats.rxErrors++;
        return false;
    }

    s_stats.rxPackets++;
    const uint16_t copyLen = rxBufLen < DISPLAY_HOST_SPI_MAX_PACKET_BYTES
                             ? rxBufLen
                             : DISPLAY_HOST_SPI_MAX_PACKET_BYTES;
    memcpy(rxBufOut, s_rxBuf, copyLen);
    return true;
}

// ---------------------------------------------------------------------------
// hostLinkQueueEvent
// ---------------------------------------------------------------------------
void hostLinkQueueEvent(const uint8_t* payload, uint16_t payloadLen) {
    if (payloadLen > static_cast<uint16_t>(DISPLAY_HOST_SPI_MAX_PACKET_BYTES - 7)) {
        payloadLen = static_cast<uint16_t>(DISPLAY_HOST_SPI_MAX_PACKET_BYTES - 7);
    }
    memcpy(s_eventBuf, payload, payloadLen);
    s_eventLen   = payloadLen;
    s_eventReady = true;
}

// ---------------------------------------------------------------------------
// hostLinkSignalReady
// ---------------------------------------------------------------------------
void hostLinkSignalReady() {
    setDispReady(true);
}

// ---------------------------------------------------------------------------
// hostLinkGetStats / hostLinkDumpStats
// ---------------------------------------------------------------------------
HostLinkStats hostLinkGetStats() {
    return s_stats;
}

void hostLinkDumpStats() {
    Serial.printf("[host_link] rx=%lu tx=%lu rxErr=%lu txDrop=%lu hostReq=%lu\n",
                  static_cast<unsigned long>(s_stats.rxPackets),
                  static_cast<unsigned long>(s_stats.txPackets),
                  static_cast<unsigned long>(s_stats.rxErrors),
                  static_cast<unsigned long>(s_stats.txDropped),
                  static_cast<unsigned long>(s_stats.hostReqCount));
}
