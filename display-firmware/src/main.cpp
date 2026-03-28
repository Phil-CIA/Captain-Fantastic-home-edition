#include <Arduino.h>
#include "host_link_config.h"
#include "display_local_config.h"

// ---------------------------------------------------------------------------
// Display-board firmware — minimal boot skeleton
// Target: ESP32-C6 DevKitM (Amazon clone, "C6 Mini")
//
// Handshake lines (see host_link_config.h for full rationale):
//   GPIO0 HOST_REQ   — input,  host → display, active-HIGH
//   GPIO1 DISP_READY — output, display → host, active-HIGH
//
// Boot sequence:
//   1. Drive DISP_READY LOW immediately so the host never sees a spurious
//      "ready" pulse during our own initialisation.
//   2. Configure HOST_REQ as input with internal pull-down; the host board
//      holds it LOW (high-Z at boot) so the pull-down keeps the line clean.
//   3. Initialise local peripherals (TFT, touch, SD).
//   4. Assert DISP_READY HIGH once buffers are prepared and the SPI slave
//      is listening.
// ---------------------------------------------------------------------------

static void initHandshakePins() {
    // DISP_READY: output, start LOW (not ready)
    pinMode(HOSTLINK_DISP_READY_PIN, OUTPUT);
    digitalWrite(HOSTLINK_DISP_READY_PIN, LOW);

    // HOST_REQ: input with internal pull-down (idle = LOW = no request)
    pinMode(HOSTLINK_HOST_REQ_PIN, INPUT_PULLDOWN);

    Serial.printf("[hostlink] DISP_READY=GPIO%u (output, LOW)\n",
                  HOSTLINK_DISP_READY_PIN);
    Serial.printf("[hostlink] HOST_REQ=GPIO%u (input, pull-down)\n",
                  HOSTLINK_HOST_REQ_PIN);
}

static void assertDispReady(bool ready) {
    digitalWrite(HOSTLINK_DISP_READY_PIN, ready ? HIGH : LOW);
}

static bool isHostRequesting() {
    return digitalRead(HOSTLINK_HOST_REQ_PIN) == HIGH;
}

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("[display] boot start");

    // Handshake lines first: DISP_READY must be LOW before anything else.
    initHandshakePins();

    // TODO: initialise local SPI bus for TFT/touch/SD
    // SPI.begin(DISP_LOCAL_SPI_CLK_PIN,
    //           DISP_LOCAL_SPI_MISO_PIN,
    //           DISP_LOCAL_SPI_MOSI_PIN);

    // TODO: initialise TFT driver (e.g., TFT_eSPI, ST7796)

    // TODO: initialise touch controller (e.g., XPT2046, TFT_eSPI touch)

    // TODO: initialise host-link SPI slave (e.g., Arduino SPI slave or ESP-IDF
    //        spi_slave_driver_install on HOSTLINK_SPI_* pins)

    Serial.println("[display] init complete — asserting DISP_READY");
    assertDispReady(true);
}

void loop() {
    // Wait for HOST_REQ to go HIGH before processing a SPI transaction.
    if (isHostRequesting()) {
        // De-assert DISP_READY while we service the transaction.
        assertDispReady(false);

        // TODO: read/write SPI slave buffers, update display, etc.

        // Wait for HOST_REQ to go LOW (transaction complete) before advertising
        // readiness for the next one.  Bounded by HOSTLINK_READY_TIMEOUT_MS.
        const uint32_t deadline = millis() + HOSTLINK_READY_TIMEOUT_MS;
        while (isHostRequesting() && millis() < deadline) {
            // Yield to avoid starving the RTOS idle task.
            yield();
        }

        // Small de-glitch gap then advertise ready for the next transaction.
        delay(HOSTLINK_READY_DEGLITCH_MS);
        assertDispReady(true);
    }
}
