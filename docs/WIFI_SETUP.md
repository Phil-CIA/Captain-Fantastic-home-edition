# WiFi & OTA Upload Setup

## Initial Setup (One Time via USB)

### 1. Configure WiFi Credentials

Edit `src/main_firmware.cpp` lines 21-24:

```cpp
const char* wifi_ssid = "YourWiFiSSID";      // <-- Change to your WiFi name
const char* wifi_password = "YourWiFiPassword";  // <-- Change to your WiFi password
const char* ota_hostname = "captain-fantastic";
const char* ota_password = "pinball2026";
```

### 2. Upload via USB (First Time)

Upload normally via USB to configure WiFi:
```
PlatformIO: Upload (USB)
```

### 3. Verify WiFi Connection

Check serial monitor after upload:
```
WiFi connected! IP: 192.168.1.xxx
[OTA] Ready! Hostname: captain-fantastic.local
```

## Switch to OTA Upload (Fast!)

### 1. Enable OTA in platformio.ini

Uncomment these lines in `platformio.ini`:
```ini
upload_protocol = espota
upload_port = captain-fantastic.local
upload_flags = 
    --port=3232
    --auth=pinball2026
```

### 2. Upload via OTA

Now uploads will be ~3x faster (10-15 seconds vs 30-40 seconds):
```
PlatformIO: Upload (OTA)
```

## Troubleshooting

### OTA Upload Fails

**Error: "Cannot resolve hostname"**
- Use IP address instead: `upload_port = 192.168.1.xxx`
- Check serial monitor for actual IP after USB upload

**Error: "Authentication failed"**
- Password mismatch - check `ota_password` matches `upload_flags --auth`

**Error: "Connection refused"**
- ESP32 not running or crashed
- Check power supply (solenoids can cause brownouts)
- Re-upload via USB to recover

### WiFi Not Connecting

- Check SSID/password are correct
- Ensure 2.4GHz WiFi (ESP32 doesn't support 5GHz)
- Move ESP32 closer to router
- Check serial monitor for connection status

## Upload Speed Comparison

| Method | Time | When to Use |
|--------|------|-------------|
| **USB** | 30-40s | Initial setup, WiFi config changes, OTA broken |
| **OTA** | 10-15s | Normal testing, frequent code changes |

## Switching Between USB and OTA

**To use USB:**
```ini
upload_port = COM5
; upload_protocol = espota  # commented out
```

**To use OTA:**
```ini
upload_protocol = espota
upload_port = captain-fantastic.local
```

## Tips

- Keep USB cable connected for serial monitor even when using OTA
- OTA works from anywhere on same network (can be across the room!)
- If OTA fails, just plug in USB and upload normally
- ESP32 must be powered on for OTA upload to work
