/*
 * display-firmware/src/main.cpp
 * ─────────────────────────────────────────────────────────────────────────────
 * Skeleton firmware for the ESP32-C6 display board.
 *
 * Hardware: ESP32-C6 DevKitM Amazon clone ("C6 Mini" / "SuperMini")
 *   Host-link SPI slave : GPIO4-7  (see include/host_link_config.h)
 *   Handshake lines     : GPIO0 HOST_REQ (input), GPIO1 DISP_READY (output)
 *   Local TFT/touch/SD  : GPIO10-23 (see include/display_local_config.h)
 *
 * See display-firmware/FLASHING_NOTES.md before connecting the IDC ribbon.
 */

#include <Arduino.h>
#include "host_link_config.h"
#include "display_local_config.h"

void setup() {
    Serial.begin(115200);

    // ── Handshake pin init ─────────────────────────────────────────────────
    // DISP_READY: output, idle LOW (not ready yet)
    pinMode(HOST_LINK_DISP_READY_PIN, OUTPUT);
    digitalWrite(HOST_LINK_DISP_READY_PIN, LOW);

    // HOST_REQ: input with pull-up.
    // ⚠ GPIO0 is a strapping pin.  The 10kΩ external pull-up on the IDC
    //   ribbon (see FLASHING_NOTES.md) keeps it HIGH during reset, which is
    //   safe.  The internal pull-up is an additional safeguard only.
    pinMode(HOST_LINK_HOST_REQ_PIN, INPUT_PULLUP);

    // TODO: initialize local SPI bus, TFT, touch, SD
    // TODO: initialize host-link SPI slave (ESP32SPISlave)

    // Signal ready to host
    digitalWrite(HOST_LINK_DISP_READY_PIN, HIGH);

    Serial.println("Display board ready");
}

void loop() {
    // TODO: poll HOST_REQ / handle SPI slave transactions
    // TODO: render UI
}
