/*
 * External Flash OTA Integration Example
 * 
 * Add this code to your main_firmware.cpp to enable the two-stage OTA system
 */

#include "external_flash_ota.h"
#include "ota_http_server.h"

// Global instances (add near top of file with other globals)
ExternalFlashOTA* extOTA = nullptr;
OTAHttpServer* otaServer = nullptr;

// In setup() function, after WiFi connects and W25Q64 is initialized:
void setupExternalFlashOTA() {
    Serial.println("\n========================================");
    Serial.println("  External Flash OTA Initialization");
    Serial.println("========================================");
    
    // Create OTA manager (using existing externalFlash instance)
    extOTA = new ExternalFlashOTA(&externalFlash);
    extOTA->begin();
    
    // Check for pending firmware on boot
    if (extOTA->hasPendingFirmware()) {
        Serial.println("\n*** PENDING FIRMWARE DETECTED ***");
        Serial.println("OPTIONS:");
        Serial.println("  1. Auto-flash now (uncomment line below)");
        Serial.println("  2. Flash via web UI at http://192.168.0.198/");
        Serial.println("  3. Send 'F' via serial to flash manually");
        
        // Option 1: Auto-flash on boot (UNCOMMENT TO ENABLE)
        // extOTA->flashFromExternalFlash();  // Will reboot after flashing
        
        // Option 2: Let user trigger via web UI (default)
        // (User navigates to http://192.168.0.198/ and clicks "Flash Now!")
    }
    
    // Start HTTP server for web-based OTA
    otaServer = new OTAHttpServer(extOTA, 80);
    otaServer->begin();
    
    Serial.println("\n[OTA] HTTP server ready!");
    Serial.println("[OTA] Upload firmware at: http://192.168.0.198/upload");
    Serial.println("[OTA] Status page at: http://192.168.0.198/");
    Serial.println("========================================\n");
}

// In loop() function, add this to handle HTTP requests:
void loopExternalFlashOTA() {
    if (otaServer != nullptr) {
        otaServer->handleClient();
    }
}

// Optional: Add serial command to trigger flash
void handleSerialCommands() {
    if (Serial.available()) {
        char cmd = Serial.read();
        
        if (cmd == 'F' || cmd == 'f') {
            Serial.println("\n[Serial] Flash command received!");
            if (extOTA != nullptr && extOTA->hasPendingFirmware()) {
                extOTA->flashFromExternalFlash();
            } else {
                Serial.println("[Serial] ERROR: No pending firmware to flash");
            }
        }
        else if (cmd == 'S' || cmd == 's') {
            Serial.println("\n[Serial] OTA Status:");
            if (extOTA != nullptr) {
                Serial.printf("  Stage: %d\n", extOTA->getStage());
                Serial.printf("  Progress: %.1f%%\n", extOTA->getProgress());
                Serial.printf("  Bytes: %u / %u\n", 
                             extOTA->getBytesWritten(), 
                             extOTA->getTotalSize());
                Serial.printf("  Pending: %s\n", 
                             extOTA->hasPendingFirmware() ? "YES" : "NO");
            }
        }
    }
}

/*
 * INTEGRATION STEPS:
 * 
 * 1. In setup(), after WiFi connects and externalFlash.begin():
 *    setupExternalFlashOTA();
 * 
 * 2. In loop(), add:
 *    loopExternalFlashOTA();
 *    handleSerialCommands();  // Optional
 * 
 * 3. Build and upload via USB one final time
 * 
 * 4. From now on, update firmware via:
 *    - Web UI: http://192.168.0.198/
 *    - Or use Python script to automate
 * 
 * PYTHON UPLOAD SCRIPT:
 * 
 *   import requests
 *   
 *   # Upload firmware
 *   url = "http://192.168.0.198/upload"
 *   files = {'firmware': open('.pio/build/combined_rtos/firmware.bin', 'rb')}
 *   print("Uploading firmware...")
 *   r = requests.post(url, files=files)
 *   
 *   if r.status_code == 200:
 *       print("Upload complete!")
 *       # Trigger flash
 *       print("Flashing firmware...")
 *       requests.post("http://192.168.0.198/flash")
 *       print("Device will reboot with new firmware")
 *   else:
 *       print(f"Upload failed: {r.status_code}")
 * 
 * USAGE:
 * 
 * 1. Make code changes
 * 2. Build: platformio run
 * 3. Navigate to http://192.168.0.198/
 * 4. Upload firmware.bin
 * 5. Click "Flash Now!"
 * 6. Device reboots with new firmware
 * 
 * NO MORE USB CABLES! 🎉
 */
