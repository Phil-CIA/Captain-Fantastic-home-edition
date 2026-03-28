// =============================================================================
// display-firmware/src/display_main.cpp
//
// Entry point for the ESP32-C6 display board firmware.
//
// Board role: SPI SLAVE receiving STATE packets from the host captain_control
// ESP32-C6, rendering game state on the ST7796S TFT, reading touch via
// XPT2046, and returning EVENT packets to the host.
//
// ⚠️  First-time bring-up: read FLASHING_NOTES.md before wiring or flashing.
//     GPIO0 is a strapping pin and requires a 10 kΩ pull-up before power-on.
// =============================================================================

#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>

#include "host_link.h"
#include "host_link_config.h"
#include "display_local_config.h"

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
static TFT_eSPI tft;

static uint8_t s_rxPacket[DISPLAY_HOST_SPI_MAX_PACKET_BYTES];
static uint32_t s_lastStatDumpMs = 0;
static uint32_t s_rxCount = 0;

// ---------------------------------------------------------------------------
// initTft()
// ---------------------------------------------------------------------------
static void initTft() {
    tft.init();
    tft.setRotation(DISP_ROTATION);
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);

    // Backlight via LEDC PWM
    ledcSetup(DISP_BACKLIGHT_CHANNEL, DISP_BACKLIGHT_FREQ_HZ, DISP_BACKLIGHT_BITS);
    ledcAttachPin(DISP_BACKLIGHT_PIN, DISP_BACKLIGHT_CHANNEL);
    ledcWrite(DISP_BACKLIGHT_CHANNEL, DISP_BACKLIGHT_DEFAULT);

    tft.drawString("Display FW ready", 10, 10);
    tft.drawString("Waiting for host...", 10, 36);
    Serial.println("[display] TFT initialized");
}

// ---------------------------------------------------------------------------
// renderState()  —  placeholder: decode and display a STATE packet
// ---------------------------------------------------------------------------
static void renderState(const uint8_t* packet) {
    // TODO: parse packet payload (bytes 6..) into game-state fields.
    // For now just flash a receipt counter on screen.
    s_rxCount++;
    tft.fillRect(0, 80, 240, 30, TFT_BLACK);
    tft.setCursor(10, 80);
    tft.printf("STATE rx: %lu", static_cast<unsigned long>(s_rxCount));
}

// ---------------------------------------------------------------------------
// buildTouchEvent()  —  placeholder: read XPT2046 and fill an EVENT payload
// ---------------------------------------------------------------------------
static uint16_t buildTouchEvent(uint8_t* payload) {
    // TODO: integrate XPT2046 library, read coordinates, fill payload.
    // Return 0 if no touch is active (no event to send).
    (void)payload;
    return 0;
}

// ---------------------------------------------------------------------------
// setup()
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("[display] setup start — ESP32-C6 display board");
    Serial.printf("[display] Host-link SPI: SCK=GPIO%d MOSI=GPIO%d MISO=GPIO%d CS=GPIO%d\n",
                  DISPLAY_HOST_SPI_SCK_PIN, DISPLAY_HOST_SPI_MOSI_PIN,
                  DISPLAY_HOST_SPI_MISO_PIN, DISPLAY_HOST_SPI_CS_PIN);
    Serial.printf("[display] Handshake: DISP_READY=GPIO%d  HOST_REQ=GPIO%d\n",
                  DISPLAY_READY_PIN, HOST_REQ_PIN);

    initTft();
    hostLinkInit();

    Serial.println("[display] setup complete");
}

// ---------------------------------------------------------------------------
// loop()
// ---------------------------------------------------------------------------
void loop() {
    // --- Service host-link SPI slave ---
    if (hostLinkService(s_rxPacket, sizeof(s_rxPacket))) {
        const uint8_t msgType = s_rxPacket[2];
        if (static_cast<DisplayMsgType>(msgType) == DisplayMsgType::STATE) {
            renderState(s_rxPacket);
        }

        // Build and queue a touch event if one is pending.
        uint8_t eventPayload[32] = {};
        const uint16_t evtLen = buildTouchEvent(eventPayload);
        if (evtLen > 0) {
            hostLinkQueueEvent(eventPayload, evtLen);
        }
    }

    // --- Periodic stats dump (every 10 s during bring-up) ---
    const uint32_t now = millis();
    if (now - s_lastStatDumpMs >= 10000UL) {
        s_lastStatDumpMs = now;
        hostLinkDumpStats();
    }
}
