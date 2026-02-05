# External Flash OTA Update System

## Overview
This system implements a two-stage OTA (Over-The-Air) update process that works around ESP32 heap fragmentation issues by using the W25Q64 external flash chip as a staging area.

## The Problem
Traditional OTA updates on ESP32 require a large contiguous heap block (~1MB) to buffer the incoming firmware. In RTOS-heavy applications with many tasks, the heap becomes fragmented, making it impossible to allocate such large blocks even when plenty of total memory is available.

**Actual measurements from your system:**
- Total free heap: ~198KB
- Largest contiguous block: **Only 110KB** ❌
- Required for OTA: ~1MB ❌
- **Result: Update.begin() fails with OTA_BEGIN_ERROR**

## The Solution: Two-Stage OTA

### Stage 1: Download to External Flash
1. Receive firmware via HTTP in small chunks (4KB each)
2. Write each chunk directly to W25Q64 external flash
3. **No large heap allocation needed** - just a 4KB buffer
4. Track progress, calculate checksum
5. Store metadata in NVS (Non-Volatile Storage)

### Stage 2: Flash from External Flash
1. On boot or manual trigger, check for pending firmware
2. Delete all RTOS tasks to free memory
3. Read firmware from W25Q64 in chunks
4. Stream directly to `Update.writeStream()`
5. Verify and reboot with new firmware

## Memory Map

```
W25Q64 (8MB total)
├── 0x000000 - 0x1FFFFF (2MB)  : Firmware Staging Area
└── 0x200000 - 0x7FFFFF (6MB)  : MP3 File Storage
```

## How to Use

### Method 1: Web Interface (Easiest)

1. **Navigate to the OTA page:**
   ```
   http://192.168.0.198/
   ```

2. **Upload firmware:**
   - Click "Upload Firmware"
   - Select `firmware.bin` from `.pio/build/combined_rtos/`
   - Upload progress shown in real-time

3. **Flash the firmware:**
   - After upload completes, "Flash Now!" button appears
   - Click to apply update
   - Device reboots with new firmware

### Method 2: Python Script Upload

```python
import requests

url = "http://192.168.0.198/upload"
files = {'firmware': open('firmware.bin', 'rb')}
response = requests.post(url, files=files)

if response.status_code == 200:
    # Trigger flash
    requests.post("http://192.168.0.198/flash")
```

### Method 3: Manual Flash on Boot

If firmware is staged but not yet flashed:
1. Reboot the device
2. System detects pending firmware
3. Optionally auto-flash on boot (configurable)

## API Endpoints

### GET /
Status page showing current OTA state, progress, and controls

### GET /status
JSON response with OTA status:
```json
{
  "stage": 2,
  "bytesWritten": 803665,
  "totalSize": 803665,
  "progress": 100.0,
  "hasPending": true
}
```

### POST /upload
Upload firmware file (multipart/form-data)
- Accepts `.bin` files
- Streams to W25Q64 in chunks
- Returns when complete

### POST /flash
Trigger immediate firmware flash
- Deletes all tasks
- Reads from W25Q64
- Flashes to internal flash
- Reboots

### POST /cancel
Cancel pending OTA update
- Clears metadata
- Returns to IDLE state

## OTA Stages

| Stage | Value | Description |
|-------|-------|-------------|
| `OTA_IDLE` | 0 | No OTA in progress |
| `OTA_DOWNLOADING` | 1 | Receiving firmware chunks |
| `OTA_READY_TO_FLASH` | 2 | Download complete, ready to flash |
| `OTA_FLASHING` | 3 | Currently flashing firmware |
| `OTA_COMPLETE` | 4 | Flash successful |
| `OTA_ERROR` | 5 | Error occurred |

## Code Integration

### In main_firmware.cpp:

```cpp
#include "external_flash_ota.h"
#include "ota_http_server.h"

// Global instances
W25Q64 externalFlash;
ExternalFlashOTA extOTA(&externalFlash);
OTAHttpServer otaServer(&extOTA, 80);

void setup() {
    // Initialize external flash
    externalFlash.begin();
    
    // Initialize OTA system
    extOTA.begin();
    
    // Check for pending firmware on boot
    if (extOTA.hasPendingFirmware()) {
        Serial.println("Pending firmware detected!");
        // Option 1: Auto-flash
        extOTA.flashFromExternalFlash();
        
        // Option 2: Wait for manual trigger via web UI
        // (do nothing, let user click "Flash Now")
    }
    
    // Start HTTP server
    otaServer.begin();
    
    // ... rest of your setup code
}

void loop() {
    otaServer.handleClient();
    // ... rest of your loop code
}
```

## Advantages

✅ **Works around heap fragmentation** - no large allocations needed
✅ **Resumable downloads** - can survive interruptions
✅ **Verifiable** - checksum validation before flashing
✅ **Safe** - old firmware still runs until flash completes
✅ **Progress tracking** - real-time upload/flash progress
✅ **Same approach for MP3 files** - can download large audio files
✅ **Web UI** - easy to use from any device on network
✅ **8MB storage** - plenty of space for firmware + MP3s

## MP3 File Downloads

The same system can be extended for MP3 file downloads:

```cpp
// Download MP3 to W25Q64
extOTA.startDownload(mp3Size, "song.mp3");
// Stream MP3 data in chunks
extOTA.writeChunk(mp3Data, chunkSize);
extOTA.finishDownload();

// MP3 files stored at W25Q64_MP3_STORAGE_ADDR (0x200000)
// Can be played directly from external flash via ESP8266Audio library
```

## Troubleshooting

**Q: Upload stalls at 0%**
- Check WiFi connection strength
- Verify ESP32 is reachable at http://192.168.0.198/
- Check serial output for errors

**Q: Flash fails**
- Ensure firmware size < 2MB
- Check serial output for detailed error messages
- Verify W25Q64 connections

**Q: Device doesn't reboot after flash**
- Check serial monitor - may show error messages
- Try manual reboot
- Re-flash via USB if needed

## Files Added

- `include/external_flash_ota.h` - OTA manager class
- `src/external_flash_ota.cpp` - OTA implementation
- `include/ota_http_server.h` - HTTP server for web UI
- `src/ota_http_server.cpp` - Web UI implementation
- `include/w25q64.h` - Enhanced with bulk operations
- `src/w25q64.cpp` - Bulk read/write functions

## Next Steps

1. Test upload via web interface
2. Verify flash process works
3. Extend for MP3 file management
4. Add firmware version tracking
5. Implement rollback capability
