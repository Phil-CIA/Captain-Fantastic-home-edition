# ESP32-C6 USB Driver Setup — COM Port Not Appearing

## Symptom

You plug the ESP32-C6 DevKitC-1 into USB and Windows plays the device-connected
sound, but the board does **not** show up as a COM port in Device Manager or the
PlatformIO / Arduino IDE serial monitor.

---

## Why This Happens

The DevKitC-1 board has a USB-to-UART bridge chip between the USB-C connector
and the ESP32-C6.  Windows needs a driver for that bridge chip before it can
create a virtual COM port.  Without the driver, Device Manager shows the device
as **"Unknown Device"** or **"USB Serial Device (with a yellow warning triangle)"**
under *Other Devices* or *Universal Serial Bus controllers*.

There are two common bridge chips used on ESP32-C6 DevKit boards:

| Bridge chip | Common board source | Driver package |
|-------------|---------------------|----------------|
| **CP2102N** | Espressif official / most vendors | Silicon Labs CP210x VCP drivers |
| **CH340G / CH341** | Low-cost Amazon/AliExpress clones | WCH CH340/CH341 driver |

---

## Step 1 — Identify Your Bridge Chip

### Option A: Check Device Manager

1. Open **Device Manager** (`Win + X → Device Manager`).
2. Expand **"Other Devices"** or **"Ports (COM & LPT)"**.
3. Right-click the unknown entry → **Properties** → **Details tab** →
   set the drop-down to **"Hardware Ids"**.
4. Note the VID/PID value:

   | VID:PID | Chip |
   |---------|------|
   | `VID_10C4&PID_EA60` | CP2102N (Silicon Labs) |
   | `VID_1A86&PID_7523` | CH340G (WCH) |
   | `VID_1A86&PID_55D4` | CH343P (WCH, newer variant) |

### Option B: Read the chip markings

Turn the board over and look at the small IC nearest the USB-C port (not the
ESP32-C6 itself).  The chip will be marked **CP2102** or **CH340**.

---

## Step 2 — Install the Correct Driver

### For CP2102N (Silicon Labs)

1. Download the **CP210x Universal Windows Driver** from:
   `https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers`
   - Click **Downloads** → choose **CP210x Universal Windows Driver**.
2. Unzip the downloaded file.
3. **Right-click** `silabser.inf` → **Install**.
   - (Or run the included installer .exe if present.)
4. Unplug and re-plug the ESP32-C6 USB cable.
5. A new **"Silicon Labs CP210x USB to UART Bridge (COMx)"** entry should now
   appear under **Ports (COM & LPT)**.

### For CH340 / CH341 (WCH)

1. Download the driver from:
   `https://www.wch-ic.com/downloads/CH341SER_EXE.html`
   (direct download: `CH341SER.EXE`)
2. Run `CH341SER.EXE` and click **INSTALL**.
3. Unplug and re-plug the USB cable.
4. A new **"USB-SERIAL CH340 (COMx)"** entry appears under **Ports (COM & LPT)**.

---

## Step 3 — Verify the COM Port

1. Open **Device Manager → Ports (COM & LPT)**.
2. Note the **COM number** (e.g., `COM5`, `COM7`).
3. Try unplugging and re-plugging — the entry appears/disappears to confirm it
   is the correct one.

---

## Step 4 — Set the Port in PlatformIO

### Option A: Let PlatformIO auto-detect (recommended)

PlatformIO's upload and monitor commands auto-detect the active port when only
one ESP32-C6 board is connected.  No change to `platformio.ini` is required.

```
pio run -e captain_matrix_idf -t upload
pio device monitor -b 115200
```

### Option B: Pin a specific port

Open `Captain-v2-matrix/platformio.ini` and add the port under the environment:

```ini
[env:captain_matrix_idf]
; ... existing settings ...
upload_port = COM5        ; <-- replace with your actual COM number
monitor_port = COM5       ; <-- same port
```

> **Windows users:** use `COMx` notation (e.g., `COM5`).
> **Linux/macOS users:** use the device path (e.g., `/dev/ttyUSB0` for CH340,
> `/dev/tty.SLAB_USBtoUART` for CP2102N).

---

## Step 5 — Open the Serial Monitor

After flashing, open the serial monitor to verify the firmware is alive:

```
pio device monitor -b 115200
```

Expected output on successful boot:

```
I (xxx) captain_matrix: Matrix firmware starting
I (xxx) captain_matrix: I2C slave initialised at 0x24
I (xxx) captain_matrix: Matrix pins configured
I (xxx) captain_matrix: Ready
```

If nothing appears, press the **RESET** button (EN) on the board once.

---

## Troubleshooting

### Driver installed but still no COM port

- Try a **different USB cable** — many USB-C cables are charge-only and carry
  no data lines.  You need a cable that supports USB 2.0 data.
- Try a **different USB port** on your computer (avoid USB hubs if possible).
- On Windows 11: go to **Settings → Windows Update → Advanced options →
  Optional updates** and install any pending driver updates.

### COM port appears then disappears

- A loose cable or a board that is brown-out resetting.  Check that the USB
  power supply can provide at least **500 mA**.

### "Access denied" when uploading

- Another program (serial monitor, PuTTY, etc.) already has the COM port open.
  Close all serial terminals, then retry the upload.
- On Windows: check that no other PlatformIO process is monitoring the port.

### "Could not open port" in PlatformIO

- Verify the COM number in `platformio.ini` matches Device Manager.
- Run `pio device list` in a terminal to see what PlatformIO can see.

### Two COM ports appear when plugged in

Some DevKitC-1 boards expose both the UART bridge port **and** a native
USB-Serial/JTAG port (from the ESP32-C6's internal USB peripheral).

| Port label | Use for |
|------------|---------|
| `Silicon Labs CP210x (COM5)` | Upload firmware, serial monitor |
| `USB Serial Device (COM6)` | Espressif USB-JTAG / OpenOCD debug |

Use the **CP210x port** for normal flashing and monitoring.  The USB-JTAG port
is for JTAG debugging only.

---

## Quick Reference — Linux / macOS

```bash
# List connected serial devices
ls /dev/tty*

# CH340 typically appears as:
/dev/ttyUSB0

# CP2102N on macOS typically appears as:
/dev/tty.SLAB_USBtoUART

# Monitor at 115200 baud (PlatformIO):
pio device monitor -p /dev/ttyUSB0 -b 115200
```

On Linux you may also need to add yourself to the `dialout` group:

```bash
sudo usermod -aG dialout $USER
# then log out and back in
```

---

## Cross-References

- Matrix board flashing steps: `Captain-v2-matrix/FLASHING_NOTES.md`
- Matrix protocol reference: `Captain-v2-matrix/README.md`
- Display board flashing notes: `display-firmware/FLASHING_NOTES.md`
