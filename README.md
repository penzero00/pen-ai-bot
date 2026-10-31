# pen-ai-bot (Cyber-Cube Mini)

> **Autonomous ESP32-S3 Desktop Robot Companion & Embedded Web OS**  
> Full-stack embedded robotics monolith combining a dual-core FreeRTOS C++ firmware with a zero-app glassmorphic Web OS served directly from ESP32-S3 flash memory via LittleFS.

---

## Overview

**pen-ai-bot** is an ultra-compact, low-profile autonomous desktop companion robot built around the **ESP32-S3 (N16R8)** microcontroller (`240MHz Dual-Core Xtensa LX7`, `16MB Quad/Octal Flash`, `8MB Octal PSRAM`). Engineered with a stacked, two-tier structural architecture, it features real-time voice interactions, expressive 60FPS procedural facial animations, dual-sensor obstacle avoidance, cliff drop-off safety, mid-air grab detection, and an embedded responsive web dashboard.

* **Embedded Web OS (No Cloud / No App Required):** Hosts a glassmorphic single-page control panel served directly from internal LittleFS (`http://192.168.4.1` or `http://penbot.local`). Includes live 60FPS Canvas facial mirror, interactive QR onboarding, RC motor control, AI voice tuner, and OTA firmware updater.
* **Dual-Core FreeRTOS Partitioning:** Strict separation between time-critical Wi-Fi / WebSocket server operations (**Core 0**) and real-time 60Hz kinematics, procedural eye rendering, and sensor polling (**Core 1**).
* **Dual-Sensor Spatial Awareness:**
  * **Head Laser (VL53L0X ToF):** Mounted above the LCD screen in the head; pans horizontally with the neck gimbal.
  * **Body Ultrasonic (HC-SR04):** Mounted in the front chassis; constantly monitors the robot's forward movement path.
* **Tabletop Safety & Kinematics:** 2x TCRT5000 IR sensors prevent edge falls (`SCARED` emergency reverse) and detect when the robot is lifted off the desk (`DIZZY` wheel safety shutdown).

---

## Hardware Specifications & Locked Pinout

> [!IMPORTANT]
> Never reassign these GPIO pins or use ESP32-S3 strapping pins (`GPIO 0, 3, 45, 46`) or internal Octal Flash/PSRAM pins (`GPIO 26–32`).

| Subsystem | Hardware Component | GPIO Pin(s) | Power Rail & Electrical Notes |
| :--- | :--- | :--- | :--- |
| **Face Display** | 1.3" IPS TFT (`ST7789` 240×240) | `SCK`: **12**, `MOSI`: **11**, `CS`: **10**, `DC`: **9**, `RST`: **13** | `3.3V` rail. Backlight (`BLK`) tied to `3.3V`. SPI DMA. |
| **Ears (Mic)** | INMP441 / MSM261 I2S Mic | `SD`: **2**, `WS`: **42**, `SCK`: **41** | `3.3V` rail. `L/R` tied to `GND`. |
| **Mouth (Amp)** | MAX98357A I2S Class-D Amp | `BCLK`: **16**, `LRC`: **17**, `DIN`: **18** | **5V Boost Rail** (Never power from ESP32 3.3V). |
| **Touch Sensor** | Top Brass Standoff Screw | `TOUCH4`: **GPIO 4** | Internal ESP32-S3 hardware capacitive touch. |
| **Head Laser (ToF)**| VL53L0X Laser Distance Sensor | `SDA`: **8**, `SCL`: **15** | `3.3V` rail. Mounted **above LCD in head** (pans with neck). |
| **Body Ultrasonic** | HC-SR04 / HC-SR04P Ultrasonic | `TRIG`: **1**, `ECHO`: **5** | `3.3V` or `5V` rail. Mounted on **front of robot chassis**. |
| **Cliff & Grab IR** | 2x Blue TCRT5000 IR Modules | Left `D0`: **6**, Right `D0`: **7** | `3.3V` rail. Single `HIGH` = Cliff. Both `HIGH > 350ms` = Picked up. |
| **Drive Wheels** | 2x 360° Continuous Micro Servos | Left: **35**, Right: **36** | **5V Boost Rail** + `1000µF 16V` smoothing capacitor. |
| **Neck Gimbal** | 1x 180° Micro Servo (Pan Only) | Neck Pan (X): **37** | **5V Boost Rail** + `1000µF 16V` capacitor (Panning only). |

---

## Procedural Eye States (ST7789 240×240 IPS)

All facial expressions are rendered procedurally at **60FPS** inside a 16-bit `TFT_eSprite` allocated in 8MB Octal PSRAM:

| Emotion | Visual Behavior | Description |
| :--- | :--- | :--- |
| **IDLE (`0`)** | Neon Cyan (`#00f2fe`) | Rounded rectangles with natural blinking every ~2.1s and sine-wave breathing. |
| **HAPPY (`1`)** | Emerald Green (`#34d399`) | Clean curved anime arc eyes (`^ ^`) drawn with smooth glowing strokes (zero black cutouts). |
| **ANGRY (`2`)** | Crimson Red (`#f43f5e`) | Angled brow slants with smooth rounded corners drawn directly with zero residue. |
| **THINK (`3`)** | Warm Gold (`#fbbf24`) | Alternating big and small eyes looking above, switching which eye is raised every ~2.6s, centered in place with natural idle blinking (no eyebrows, no mouth). |
| **SLEEP (`4`)** | Dim Slate (`#64748b`) | Closed horizontal slit eye lines with floating, fading `z Z Z` snoring particles. |
| **SCARED (`5`)** | Electric Cyan (`#00f2fe`) | High-frequency shivering anime `> <` wincing chevrons triggered on tabletop drop-off detection. |
| **DIZZY (`6`)** | Neon Magenta (`#e879f9`) | Counter-rotating dual logarithmic spiral eyes triggered when robot is lifted in mid-air. |
| **Battery HUD**| Upper-Right HUD | Compact `27px` battery frame with percentage (`88%`) centered directly inside the battery. |

---

## Project Structure

```text
pen-ai-bot/
├── AGENTS.md                         # Master architecture & AI agent directives
├── platformio.ini                    # PlatformIO ESP32-S3 N16R8 build config
├── partitions_16MB.csv               # 16MB partition layout (4MB App / 8MB LittleFS / NVS)
├── firmware/
│   ├── include/
│   │   ├── config.h                  # Master hardware pinout macros & safety thresholds
│   │   ├── qrcode.h                  # Embedded QR code matrix engine
│   │   └── state.h                   # Thread-safe structs, queues, and telemetry definitions
│   ├── src/
│   │   └── main.cpp                  # Dual-core FreeRTOS orchestrator
│   └── data/                         # Compiled assets uploaded to ESP32 LittleFS
│       └── index.html                # Embedded Cyber-Dashboard SPA
├── scripts/
│   ├── embed_web.py                  # Automatic LittleFS asset sync hook
│   └── fix_ide_intellisense.py       # Clangd sysroot & IntelliSense sync
└── web-dashboard/
    └── index.html                    # Glassmorphic dashboard & 60FPS canvas simulator
```

---

## Quick Start & Flashing Guide

### Prerequisites
* [PlatformIO Core (CLI)](https://platformio.org/install/cli) or PlatformIO IDE extension in VS Code.
* Python 3.9+

### 1. Build & Compile Firmware
```bash
# Compile the dual-core C++ FreeRTOS firmware
python -m platformio run
```

### 2. Upload Web Dashboard to LittleFS
```bash
# Bundles web-dashboard/index.html into littlefs.bin and flashes to ESP32-S3 flash
python -m platformio run --target uploadfs
```

### 3. Flash Firmware via USB
```bash
# Flashes compiled firmware.bin over USB-CDC
python -m platformio run --target upload
```

---

## Initial Setup & Web OS Pairing

1. **Power On:** Connect the robot to a 5V USB power source (or onboard LiPo battery).
2. **Access Point Mode:** If no Wi-Fi credentials are saved, the robot broadcasts a Wi-Fi hotspot:
   * **SSID:** `PenBot-Setup` (or custom name)
   * **Password:** `12345678`
   * **Setup URL:** `http://192.168.4.1` or `http://penbot.local`
3. **Onboarding QR Screen:** The 1.3" IPS screen displays a pairing QR code. Scan it with a smartphone camera to automatically connect to the Wi-Fi AP.
4. **Dashboard:** Configure home Wi-Fi credentials, adjust AI voice personas/pitch/rate, drive wheels via the virtual joystick, and perform OTA firmware updates wirelessly.

---

## License

MIT License. Designed and built with ❤️ for autonomous desktop companion robotics.
