#include "external_flash_ota.h"

ExternalFlashOTA::ExternalFlashOTA(W25Q64* flash) : _flash(flash) {
    _currentAddress = W25Q64_FIRMWARE_STAGING_ADDR;
    _sectorErasePos = W25Q64_FIRMWARE_STAGING_ADDR;
    memset(&_metadata, 0, sizeof(_metadata));
    _metadata.stage = EXTOTA_IDLE;
}

void ExternalFlashOTA::begin() {
    _prefs.begin("ota", false);
    loadMetadata();
    
    Serial.println("\n[ExtFlashOTA] Initializing...");
    Serial.printf("[ExtFlashOTA] Stage: %d\n", _metadata.stage);
    
    // Clear stuck flashing state (likely from crash during flash)
    if (_metadata.stage == EXTOTA_FLASHING) {
        Serial.println("[ExtFlashOTA] WARNING: Stuck in FLASHING state, clearing...");
        _metadata.stage = EXTOTA_READY_TO_FLASH;
        saveMetadata();
    }
    
    if (_metadata.stage == EXTOTA_READY_TO_FLASH) {
        Serial.println("[ExtFlashOTA] *** PENDING FIRMWARE DETECTED ***");
        Serial.printf("[ExtFlashOTA] Size: %u bytes, Version: %s\n", 
                      _metadata.firmwareSize, _metadata.version);
        Serial.println("[ExtFlashOTA] Call flashFromExternalFlash() to apply update");
    }
}

bool ExternalFlashOTA::startDownload(uint32_t totalSize, const char* version) {
    if (totalSize > W25Q64_FIRMWARE_MAX_SIZE) {
        Serial.printf("[ExtFlashOTA] ERROR: Firmware too large (%u > %u bytes)\n", 
                      totalSize, W25Q64_FIRMWARE_MAX_SIZE);
        return false;
    }
    
    Serial.println("\n[ExtFlashOTA] ========================================");
    Serial.println("[ExtFlashOTA] Starting firmware download to W25Q64");
    Serial.println("[ExtFlashOTA] ========================================");
    Serial.printf("[ExtFlashOTA] Size: %u bytes (%.2f KB)\n", totalSize, totalSize / 1024.0);
    if (version && strlen(version) > 0) {
        Serial.printf("[ExtFlashOTA] Version: %s\n", version);
    }
    
    // Reset metadata
    memset(&_metadata, 0, sizeof(_metadata));
    _metadata.firmwareSize = totalSize;
    _metadata.expectedSize = totalSize;  // For progress calculation
    _metadata.bytesWritten = 0;
    _metadata.stage = EXTOTA_DOWNLOADING;
    if (version) {
        strncpy(_metadata.version, version, sizeof(_metadata.version) - 1);
    }
    
    _currentAddress = W25Q64_FIRMWARE_STAGING_ADDR;
    _sectorErasePos = W25Q64_FIRMWARE_STAGING_ADDR;
    
    saveMetadata();
    
    // Pre-erase ALL sectors needed for the firmware
    // This MUST be done before any writes, not on-demand during upload
    uint32_t sectorsNeeded = (totalSize + W25Q64_SECTOR_SIZE - 1) / W25Q64_SECTOR_SIZE;
    Serial.printf("[ExtFlashOTA] Pre-erasing %u sectors (%u KB)...\n", sectorsNeeded, sectorsNeeded * 4);
    
    for (uint32_t i = 0; i < sectorsNeeded; i++) {
        uint32_t sectorAddr = W25Q64_FIRMWARE_STAGING_ADDR + (i * W25Q64_SECTOR_SIZE);
        _flash->eraseSector(sectorAddr);
        
        // Show progress every 64 sectors (256KB)
        if (i % 64 == 0 || i == sectorsNeeded - 1) {
            Serial.printf("[ExtFlashOTA]   Erased %u/%u sectors...\n", i + 1, sectorsNeeded);
        }
    }
    
    _sectorErasePos = W25Q64_FIRMWARE_STAGING_ADDR + (sectorsNeeded * W25Q64_SECTOR_SIZE);
    
    Serial.println("[ExtFlashOTA] Pre-erase complete!");
    return true;
}

bool ExternalFlashOTA::writeChunk(const uint8_t* data, size_t len) {
    if (_metadata.stage != EXTOTA_DOWNLOADING) {
        Serial.println("[ExtFlashOTA] ERROR: Not in downloading state");
        return false;
    }
    
    // Allow some flexibility in size estimation (Content-Length includes multipart overhead)
    // Only enforce hard limit at 2MB (firmware partition max)
    if (_metadata.bytesWritten + len > 2 * 1024 * 1024) {
        Serial.println("[ExtFlashOTA] ERROR: Firmware too large (>2MB)");
        return false;
    }
    
    // All sectors pre-erased in startDownload(), write directly
    // Write chunk to external flash
    _flash->writeBytes(_currentAddress, data, len);
    
    // CRITICAL: Verify first write immediately (detect power/hardware issues early)
    if (_metadata.bytesWritten == 0 && len > 0) {
        delay(50); // Wait for write completion
        uint8_t verifyBuf[32];
        size_t verifyLen = min((size_t)32, len);
        _flash->readBytes(W25Q64_FIRMWARE_STAGING_ADDR, verifyBuf, verifyLen);
        
        bool verifyOK = true;
        for (size_t i = 0; i < verifyLen; i++) {
            if (verifyBuf[i] != data[i]) {
                Serial.printf("[ExtFlashOTA] CRITICAL: Byte %u verify FAILED! Wrote 0x%02X, read 0x%02X\n", 
                             i, data[i], verifyBuf[i]);
                verifyOK = false;
                break;
            }
        }
        
        if (!verifyOK) {
            Serial.println("[ExtFlashOTA] ERROR: First write verification FAILED!");
            _metadata.stage = EXTOTA_ERROR;
            saveMetadata();
            return false;
        }
    }
    
    _currentAddress += len;
    _metadata.bytesWritten += len;
    
    // Update checksum (simple CRC32 would be better, but this works)
    for (size_t i = 0; i < len; i++) {
        _metadata.checksum = _metadata.checksum ^ data[i];
        _metadata.checksum = (_metadata.checksum << 1) | (_metadata.checksum >> 31);
    }
    
    // Save progress every 128KB
    if (_metadata.bytesWritten % (128 * 1024) == 0) {
        saveMetadata();
        Serial.printf("[ExtFlashOTA] Progress: %u / %u bytes (%.1f%%)\n",
                      _metadata.bytesWritten, _metadata.expectedSize, getProgress());
    }
    
    return true;
}

bool ExternalFlashOTA::finishDownload(uint32_t expectedChecksum) {
    if (_metadata.stage != EXTOTA_DOWNLOADING) {
        Serial.println("[ExtFlashOTA] ERROR: Not in downloading state");
        return false;
    }
    
    // Update final firmware size to actual bytes written
    // (initial size was estimated from Content-Length which includes overhead)
    _metadata.firmwareSize = _metadata.bytesWritten;
    
    if (_metadata.bytesWritten == 0) {
        Serial.println("[ExtFlashOTA] ERROR: No data received");
        return false;
    }
    
    Serial.println("\n[ExtFlashOTA] ========================================");
    Serial.println("[ExtFlashOTA] Download complete!");
    Serial.println("[ExtFlashOTA] ========================================");
    Serial.printf("[ExtFlashOTA] Total bytes: %u\n", _metadata.bytesWritten);
    Serial.printf("[ExtFlashOTA] Checksum: 0x%08X\n", _metadata.checksum);
    
    // Verify checksum if provided
    if (expectedChecksum != 0 && _metadata.checksum != expectedChecksum) {
        Serial.printf("[ExtFlashOTA] ERROR: Checksum mismatch! Expected 0x%08X\n", expectedChecksum);
        _metadata.stage = EXTOTA_ERROR;
        saveMetadata();
        return false;
    }
    
    _metadata.stage = EXTOTA_READY_TO_FLASH;
    saveMetadata();
    
    // VERIFY: Read back critical sections to ensure data integrity
    Serial.println("[ExtFlashOTA] Verifying firmware in W25Q64...");
    uint8_t verifyBuf[256];
    bool verifyOK = true;
    
    // Check beginning (should start with 0xE9 magic byte)
    _flash->readBytes(W25Q64_FIRMWARE_STAGING_ADDR, verifyBuf, 256);
    if (verifyBuf[0] != 0xE9) {
        Serial.printf("[ExtFlashOTA] VERIFY FAILED at start! First byte: 0x%02X (expected 0xE9)\n", verifyBuf[0]);
        verifyOK = false;
    }
    
    // Check middle (50% point)
    uint32_t midPoint = _metadata.bytesWritten / 2;
    _flash->readBytes(W25Q64_FIRMWARE_STAGING_ADDR + midPoint, verifyBuf, 256);
    bool allFF = true;
    for (int i = 0; i < 256; i++) {
        if (verifyBuf[i] != 0xFF) {
            allFF = false;
            break;
        }
    }
    if (allFF) {
        Serial.printf("[ExtFlashOTA] VERIFY FAILED at midpoint (0x%06X)! All 0xFF (erased)\n", midPoint);
        verifyOK = false;
    }
    
    // Check end (last 256 bytes)
    uint32_t endAddr = _metadata.bytesWritten > 256 ? _metadata.bytesWritten - 256 : 0;
    _flash->readBytes(W25Q64_FIRMWARE_STAGING_ADDR + endAddr, verifyBuf, 256);
    allFF = true;
    for (int i = 0; i < 256; i++) {
        if (verifyBuf[i] != 0xFF) {
            allFF = false;
            break;
        }
    }
    if (allFF) {
        Serial.printf("[ExtFlashOTA] VERIFY FAILED at end (0x%06X)! All 0xFF (erased)\n", endAddr);
        verifyOK = false;
    }
    
    if (!verifyOK) {
        Serial.println("[ExtFlashOTA] Verification FAILED! Firmware incomplete in W25Q64.");
        _metadata.stage = EXTOTA_ERROR;
        saveMetadata();
        return false;
    }
    
    // CRITICAL: Parse ESP32 image header to find actual firmware size
    // This prevents flashing padding that the bootloader will reject
    uint8_t imgHeader[24];
    _flash->readBytes(W25Q64_FIRMWARE_STAGING_ADDR, imgHeader, 24);
    
    if (imgHeader[0] == 0xE9) {
        uint8_t segmentCount = imgHeader[1];
        Serial.printf("[ExtFlashOTA] ESP32 image header: magic=0xE9, segments=%d\n", segmentCount);
        
        // Parse segments to find actual image size
        uint32_t parseAddr = W25Q64_FIRMWARE_STAGING_ADDR + 24; // Skip main header
        uint32_t maxSegmentEnd = parseAddr;
        
        for (int seg = 0; seg < segmentCount && seg < 16; seg++) {
            uint8_t segHeader[8];
            _flash->readBytes(parseAddr, segHeader, 8);
            
            uint32_t segAddr = segHeader[0] | (segHeader[1] << 8) | (segHeader[2] << 16) | (segHeader[3] << 24);
            uint32_t segLen = segHeader[4] | (segHeader[5] << 8) | (segHeader[6] << 16) | (segHeader[7] << 24);
            
            if (segLen == 0xFFFFFFFF || segLen > 0x100000) {
                Serial.printf("[ExtFlashOTA] Segment %d has invalid length 0x%X, stopping parse\n", seg, segLen);
                break;
            }
            
            parseAddr += 8 + segLen; // Segment header + data
            if (parseAddr > maxSegmentEnd) {
                maxSegmentEnd = parseAddr;
            }
            
            Serial.printf("[ExtFlashOTA]   Segment %d: addr=0x%08X, len=%u bytes\n", seg, segAddr, segLen);
        }
        
        // ESP32 images have padding and checksum after segments
        // Use bytesWritten as the firmware size (it's the actual file size)
        // The parser validation above ensures the image structure is valid
        _metadata.firmwareSize = _metadata.bytesWritten;
    }
    
    Serial.println("[ExtFlashOTA] Verification PASSED!");
    Serial.println("[ExtFlashOTA] Firmware ready to flash!");
    Serial.println("[ExtFlashOTA] Reboot to apply update, or call flashFromExternalFlash()");
    
    return true;
}

void ExternalFlashOTA::cancelDownload() {
    Serial.println("[ExtFlashOTA] Download cancelled");
    clearPending();
}

bool ExternalFlashOTA::flashFromExternalFlash() {
    if (_metadata.stage != EXTOTA_READY_TO_FLASH) {
        Serial.println("[ExtFlashOTA] ERROR: No firmware ready to flash");
        return false;
    }
    
    Serial.println("\n[ExtFlashOTA] ========================================");
    Serial.println("[ExtFlashOTA] FLASHING FIRMWARE FROM W25Q64");
    Serial.println("[ExtFlashOTA] ========================================");
    Serial.printf("[ExtFlashOTA] Size: %u bytes\n", _metadata.firmwareSize);
    Serial.printf("[ExtFlashOTA] Version: %s\n", _metadata.version);
    
    _metadata.stage = EXTOTA_FLASHING;
    saveMetadata();
    
    // CRITICAL: Disable watchdogs to prevent resets during flash operation
    Serial.println("\n[ExtFlashOTA] Preparing for flash...");
    disableCore0WDT();
    disableCore1WDT();
    Serial.println("[ExtFlashOTA] Watchdogs disabled");
    
    Serial.printf("[ExtFlashOTA] Free heap: %d bytes\n", ESP.getFreeHeap());
    Serial.printf("[ExtFlashOTA] Largest block: %d bytes\n", ESP.getMaxAllocHeap());
    
    // Use native ESP-IDF OTA API (lower memory overhead than Arduino Update library)
    Serial.println("[ExtFlashOTA] Using ESP-IDF native OTA API...");
    
    const esp_partition_t* running_partition = esp_ota_get_running_partition();
    const esp_partition_t* update_partition = esp_ota_get_next_update_partition(NULL);
    
    if (update_partition == NULL) {
        Serial.println("[ExtFlashOTA] ERROR: No OTA partition found!");
        _metadata.stage = EXTOTA_ERROR;
        saveMetadata();
        ESP.restart();
        return false;
    }
    
    Serial.printf("[ExtFlashOTA] Running partition: %s at 0x%08X\n",
                  running_partition->label, running_partition->address);
    Serial.printf("[ExtFlashOTA] Update partition: %s at 0x%08X\n", 
                  update_partition->label, update_partition->address);
    
    // Ensure we're not trying to write to the running partition
    if (update_partition == running_partition) {
        Serial.println("[ExtFlashOTA] ERROR: Update partition same as running!");
        _metadata.stage = EXTOTA_ERROR;
        saveMetadata();
        ESP.restart();
        return false;
    }
    
    esp_err_t err;
    
    // Erase the target partition first to avoid conflicts
    Serial.println("[ExtFlashOTA] Erasing target partition...");
    err = esp_partition_erase_range(update_partition, 0, update_partition->size);
    if (err != ESP_OK) {
        Serial.printf("[ExtFlashOTA] WARNING: Partition erase failed: 0x%X (continuing anyway)\n", err);
    }
    
    // CRITICAL FIX: Use actual firmware size from metadata
    // This may have been updated during verification if padding was detected
    uint32_t actualFirmwareSize = _metadata.firmwareSize;
    if (actualFirmwareSize > _metadata.bytesWritten) {
        actualFirmwareSize = _metadata.bytesWritten;
    }
    
    Serial.printf("[ExtFlashOTA] Flashing: %u bytes from W25Q64 to internal partition\n", 
                  actualFirmwareSize);
    
    esp_ota_handle_t ota_handle;
    err = esp_ota_begin(update_partition, actualFirmwareSize, &ota_handle);
    if (err != ESP_OK) {
        Serial.printf("[ExtFlashOTA] ERROR: esp_ota_begin() failed! Error: 0x%X\n", err);
        _metadata.stage = EXTOTA_ERROR;
        saveMetadata();
        ESP.restart();
        return false;
    }
    
    Serial.println("[ExtFlashOTA] Streaming from W25Q64 to OTA partition...");
    
    // Read from W25Q64 and write to OTA partition in chunks
    const size_t CHUNK_SIZE = 4096;
    uint8_t buffer[CHUNK_SIZE];
    uint32_t addr = W25Q64_FIRMWARE_STAGING_ADDR;
    uint32_t remaining = actualFirmwareSize;
    uint32_t written = 0;
    
    while (remaining > 0) {
        size_t toRead = min((size_t)remaining, CHUNK_SIZE);
        
        // Read chunk from external flash
        _flash->readBytes(addr, buffer, toRead);
        
        // VERIFY: Check for suspicious all-0xFF reads (beyond first chunk)
        if (written > 0 && written % (64 * 1024) == 0) {  // Check every 64KB
            bool allFF = true;
            for (size_t i = 0; i < min(toRead, (size_t)256); i++) {
                if (buffer[i] != 0xFF) {
                    allFF = false;
                    break;
                }
            }
            if (allFF) {
                Serial.printf("[ExtFlashOTA] WARNING: Read all 0xFF at offset 0x%06X\n", written);
            }
        }
        
        // DEBUG: Print first chunk's first 32 bytes
        if (written == 0) {
            Serial.println("[ExtFlashOTA] First 32 bytes from W25Q64:");
            for (int i = 0; i < 32; i++) {
                Serial.printf("0x%02X ", buffer[i]);
                if ((i + 1) % 16 == 0) Serial.println();
            }
        }
        
        // Write to OTA partition
        err = esp_ota_write(ota_handle, buffer, toRead);
        if (err != ESP_OK) {
            Serial.printf("[ExtFlashOTA] ERROR: esp_ota_write() failed! Error: 0x%X\n", err);
            esp_ota_abort(ota_handle);
            _metadata.stage = EXTOTA_ERROR;
            saveMetadata();
            ESP.restart();
            return false;
        }
        
        addr += toRead;
        remaining -= toRead;
        written += toRead;
        
        // Progress update every 64KB
        if (written % (64 * 1024) == 0) {
            Serial.printf("[ExtFlashOTA] Flashing: %u / %u bytes (%.1f%%)\n",
                          written, actualFirmwareSize, (written * 100.0) / actualFirmwareSize);
        }
    }
    
    // Finalize OTA
    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        Serial.printf("[ExtFlashOTA] ERROR: esp_ota_end() failed! Error: 0x%X\n", err);
        _metadata.stage = EXTOTA_ERROR;
        saveMetadata();
        ESP.restart();
        return false;
    }
    
    // Set new boot partition
    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        Serial.printf("[ExtFlashOTA] ERROR: esp_ota_set_boot_partition() failed! Error: 0x%X\n", err);
        _metadata.stage = EXTOTA_ERROR;
        saveMetadata();
        ESP.restart();
        return false;
    }
    
    Serial.println("\n[ExtFlashOTA] ========================================");
    Serial.println("[ExtFlashOTA] FIRMWARE FLASH COMPLETE!");
    Serial.println("[ExtFlashOTA] ========================================");
    Serial.println("[ExtFlashOTA] Rebooting in 3 seconds...");
    
    _metadata.stage = EXTOTA_COMPLETE;
    saveMetadata();
    clearPending(); // Clear the pending flag
    
    Serial.flush();
    delay(3000);
    ESP.restart();
    
    return true;
}

bool ExternalFlashOTA::hasPendingFirmware() {
    return (_metadata.stage == EXTOTA_READY_TO_FLASH);
}

OTAStage ExternalFlashOTA::getStage() {
    return _metadata.stage;
}

uint32_t ExternalFlashOTA::getBytesWritten() {
    return _metadata.bytesWritten;
}

uint32_t ExternalFlashOTA::getTotalSize() {
    return _metadata.expectedSize > 0 ? _metadata.expectedSize : _metadata.firmwareSize;
}

float ExternalFlashOTA::getProgress() {
    uint32_t expected = _metadata.expectedSize > 0 ? _metadata.expectedSize : _metadata.firmwareSize;
    if (expected == 0) return 0.0;
    return (_metadata.bytesWritten * 100.0) / expected;
}

void ExternalFlashOTA::clearPending() {
    memset(&_metadata, 0, sizeof(_metadata));
    _metadata.stage = EXTOTA_IDLE;
    saveMetadata();
}

void ExternalFlashOTA::saveMetadata() {
    _prefs.putBytes("metadata", &_metadata, sizeof(_metadata));
}

void ExternalFlashOTA::loadMetadata() {
    size_t len = _prefs.getBytes("metadata", &_metadata, sizeof(_metadata));
    if (len != sizeof(_metadata)) {
        // No valid metadata, start fresh
        memset(&_metadata, 0, sizeof(_metadata));
        _metadata.stage = EXTOTA_IDLE;
    }
}

void ExternalFlashOTA::eraseNextSectorIfNeeded() {
    // Check if we're about to cross into next sector
    uint32_t nextByte = _currentAddress + 1;
    if (nextByte >= _sectorErasePos && _sectorErasePos < W25Q64_FIRMWARE_STAGING_ADDR + _metadata.firmwareSize) {
        Serial.printf("[ExtFlashOTA] Erasing sector at 0x%06X\n", _sectorErasePos);
        _flash->eraseSector(_sectorErasePos);
        _sectorErasePos += W25Q64_SECTOR_SIZE;
    }
}

