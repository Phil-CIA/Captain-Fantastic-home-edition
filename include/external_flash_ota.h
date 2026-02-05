#ifndef EXTERNAL_FLASH_OTA_H
#define EXTERNAL_FLASH_OTA_H

#include <Arduino.h>
#include <Update.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include "w25q64.h"

// OTA State tracking
enum OTAStage {
    EXTOTA_IDLE,
    EXTOTA_DOWNLOADING,
    EXTOTA_READY_TO_FLASH,
    EXTOTA_FLASHING,
    EXTOTA_COMPLETE,
    EXTOTA_ERROR
};

struct OTAMetadata {
    uint32_t firmwareSize;     // Actual validated firmware size (used for flashing)
    uint32_t expectedSize;     // Expected size from Content-Length (used for progress)
    uint32_t bytesWritten;
    uint32_t checksum;
    OTAStage stage;
    char version[32];
};

class ExternalFlashOTA {
public:
    ExternalFlashOTA(W25Q64* flash);
    
    // Initialize OTA system
    void begin();
    
    // Stage 1: Download firmware to external flash
    bool startDownload(uint32_t totalSize, const char* version = "");
    bool writeChunk(const uint8_t* data, size_t len);
    bool finishDownload(uint32_t expectedChecksum = 0);
    void cancelDownload();
    
    // Stage 2: Flash from external flash to internal flash
    bool flashFromExternalFlash();
    
    // Check if there's pending firmware to flash
    bool hasPendingFirmware();
    
    // Get current status
    OTAStage getStage();
    uint32_t getBytesWritten();
    uint32_t getTotalSize();
    float getProgress();
    
    // Clear any pending update
    void clearPending();
    
private:
    W25Q64* _flash;
    Preferences _prefs;
    OTAMetadata _metadata;
    uint32_t _currentAddress;
    uint32_t _sectorErasePos;
    
    void saveMetadata();
    void loadMetadata();
    void eraseNextSectorIfNeeded();
    uint32_t calculateChecksum(uint32_t size);
};

#endif // EXTERNAL_FLASH_OTA_H
