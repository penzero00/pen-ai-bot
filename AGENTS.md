# pen-ai-bot (Cyber-Cube Mini) — ESP32-S3 Autonomous Desktop Companion & Web OS

A full-stack embedded robotics monolith combining a **C++ FreeRTOS dual-core firmware** (PlatformIO / Arduino framework) with an **ultra-lightweight responsive Web Dashboard** served directly from the **ESP32-S3 (N16R8)** flash memory via LittleFS.

---

## About The Project

**pen-ai-bot** is an ultra-compact, low-profile autonomous desktop robot built around the **ESP32-S3 (N16R8)** microcontroller (`16MB Quad/Octal Flash`, `8MB Octal PSRAM`). Designed with a stacked, two-tier structural architecture, it acts as a smart desk companion capable of real-time voice conversations, expressive 60FPS procedural facial animations, autonomous tabletop roaming without falling off edges, mid-air grab detection, live web radio streaming, and wireless PC workflow automation.

1. **Zero-App Local Web OS:** Hosts an embedded, glassmorphic single-page application (SPA) via `ESPAsyncWebServer` + `WebSockets` (`ws://penbot.local/ws`). Users configure Wi-Fi, Gemini/Groq API keys, custom AI voice personas/pitch/speed, system prompts, and servo trims directly from any phone or desktop browser.
2. **Dual-Core FreeRTOS Architecture:** Strictly separates time-critical Wi-Fi/I2S audio streaming (**Core 0**) from 60FPS ST7789 procedural eye rendering, VL53L0X laser ToF wall avoidance, TCRT5000 cliff/grab safety, and 4-servo kinematics (**Core 1**).
3. **Cloud AI + PC HID Automation:** Connects to **Google Gemini 2.5 Flash** or **Groq (Llama-3.3-70B)** free-tier APIs for natural voice/text interaction and structured JSON tool-calling, while acting as a **Bluetooth LE / USB HID Keyboard & Mouse** to trigger macros (e.g., `Ctrl+K` in Cursor IDE) on the user's computer.

---

## Tech Stack & Ecosystem

| Layer | Technology | Role / Usage |
| :--- | :--- | :--- |
| **Monolith Orchestration** | PlatformIO + Bun / Node.js | Unified firmware compilation, LittleFS asset bundling, and serial flashing |
| **Microcontroller Brain** | ESP32-S3 DevKitC-1 (**N16R8**) | Dual-core Xtensa LX7 @ 240MHz, 16MB Flash, 8MB Octal PSRAM |
| **Embedded Firmware** | C++17, Arduino Framework, FreeRTOS | Multi-threaded hardware control, I2S DMA audio, and Async Web/WS Server |
| **Embedded Web App** | Single-File HTML5 / Vue 3 / Tailwind CSS | Responsive glassmorphic control panel & live 60FPS canvas simulator |
| **State & Persistence** | ESP32 `Preferences.h` (NVS) + LittleFS | Non-volatile storage for Wi-Fi credentials, API keys, voice profile, and servo trims |
| **Display Engine** | `TFT_eSPI` (ST7789 1.3" 240x240 IPS) | Full-screen `TFT_eSprite` buffered in 8MB PSRAM for tear-free 60FPS eyes |
| **Audio Pipeline (I2S)** | `INMP441` / `MSM261` Mic + `MAX98357A` Amp | Digital I2S microphone capture (`I2S_NUM_0`) and 3W Class-D speaker output (`I2S_NUM_1`) |
| **Cloud AI Brain** | Google Gemini API / Groq LPU API | Free-tier LLM inference with structured JSON output (`reply`, `emotion`, `action`, `voice`) |

---

## Master Hardware Pinout (LOCKED SPECIFICATION)

**CRITICAL DIRECTIVE:** Agents MUST NEVER reassign these pins or use ESP32-S3 strapping pins (`GPIO 0, 3, 45, 46`) or internal Octal Flash/PSRAM pins (`GPIO 26–32`).

| Subsystem | Component | ESP32-S3 GPIO Pin(s) | Power Rail & Notes |
| :--- | :--- | :--- | :--- |
| **Face Display** | 1.3" IPS TFT (`ST7789` 240x240) | `SCK`: **12**, `MOSI`: **11**, `CS`: **10**, `DC`: **9**, `RST`: **13** | `BLK` wired to `3V3`. Driven via SPI DMA. |
| **Ears (Mic)** | INMP441 / MSM261 I2S Mic | `SD`: **2**, `WS`: **42**, `SCK`: **41** | `3V3` rail. `L/R` pin wired to `GND`. |
| **Mouth (Amp)** | MAX98357A I2S Class-D Amp | `BCLK`: **16**, `LRC`: **17**, `DIN`: **18** | **5V Boost Rail** (Never power from ESP32 3V3). |
| **Touch Sensor** | Top Brass Standoff Screw | `TOUCH4`: **GPIO 4** | Internal ESP32-S3 hardware capacitive touch. |
| **Head Laser (ToF)** | VL53L0X Distance Sensor | `SDA`: **8**, `SCL`: **15** | `3V3` rail. Mounted above LCD display in the head (pans with neck). |
| **Body Ultrasonic** | HC-SR04 / HC-SR04P Ultrasonic | `TRIG`: **1**, `ECHO`: **5** | `3V3` or `5V` rail. Mounted in front of the robot body chassis. |
| **Cliff & Grab IR** | 2x Blue TCRT5000 IR Modules | Left `D0`: **6**, Right `D0`: **7** | `3V3` rail. Single `HIGH` = Cliff (`SCARED`). Both `HIGH > 350ms` = Grabbed (`DIZZY`). |
| **Drive Wheels** | 2x 360° Continuous Micro Servos | Left Wheel: **35**, Right Wheel: **36** | **5V Boost Rail** + `1000µF 16V` electrolytic capacitor. |
| **Neck Gimbal** | 1x 180° Micro Servo (Pan Only) | Neck Pan (X): **37** (Panning only, no tilting) | **5V Boost Rail** + `1000µF 16V` electrolytic capacitor. |

---

## Project Structure

```text
pen-ai-bot/
├── AGENTS.md                         # Master Architecture & AI Coding Agent Directives (THIS FILE)
├── platformio.ini                    # PlatformIO ESP32-S3 N16R8 config, build flags & libraries
├── partitions_16MB.csv               # Custom 16MB partition table (4MB App / 8MB LittleFS / NVS)
├── scripts/
│   └── embed_web.py                  # Auto-sync hook: web-dashboard/ -> firmware/data/ (pre:buildfs)
├── firmware/
│   ├── include/
│   │   ├── config.h                  # Locked GPIO pinout macros & safety thresholds
│   │   └── state.h                   # Thread-safe FreeRTOS structs, queues, emotions & VoiceConfig
│   ├── src/
│   │   └── main.cpp                  # Dual-core FreeRTOS orchestrator (Core 0 Net/AI, Core 1 HW/Eyes)
│   └── data/                         # Exact LittleFS flash mirror of web-dashboard/ (100% parity)
│       ├── index.html                # Automatic responsive device dispatcher
│       ├── desktop/
│       │   └── index.html            # Desktop Studio (Widescreen 12-col telemetry & studio)
│       └── mobile/
│           └── index.html            # Mobile Companion OS (Touch-first bottom tabs & thumb RC)
└── web-dashboard/
    ├── index.html                    # Automatic device-responsive router (mobile/ vs desktop/)
    ├── desktop/
    │   └── index.html                # Responsive Desktop Studio
    └── mobile/
        └── index.html                # Responsive Mobile Companion (touch-optimized)
```

---

## Procedural Eye States & HUD Rules (ST7789 240×240)

All facial expressions are rendered procedurally at **60FPS** inside a `240x240` 16-bit `TFT_eSprite` allocated in PSRAM (`firmware/src/main.cpp` and mirrored in `web-dashboard/index.html`):

1. **Upper-Right Battery HUD (`drawBatteryHud`)**: Every frame renders the compact battery icon (reduced length: `27px`) with the percentage number (`88%`) centered directly **inside** the battery in the top-right corner (`x: 204–231, y: 8–22`). **DO NOT** display Pan/Tilt degree text on the LCD.
2. **`EMOTION_IDLE` (`0`)**: Neon cyan rounded rectangles with natural blinking every ~2.1 seconds and subtle sine-wave breathing.
3. **`EMOTION_HAPPY` (`1`)**: Emerald green curved anime arc eyes (`^ ^`) rendered directly with thick curved strokes/arcs, zero black cutout residue. Triggered via AI response or tapping the GPIO 4 brass screw.
4. **`EMOTION_ANGRY` (`2`)**: Crimson red eyes with sharp diagonal upper-brow slants and smooth rounded corners, rendered directly with zero black cutout residue.
5. **`EMOTION_THINKING` (`3`)**: Warm gold alternating big and small eyes looking above, switching which eye is raised every ~2.6 seconds (`tick / 160`), centered in place horizontally (`X: 38, 138`) with natural idle-style blinking (`tick % 130 < 5`). **NO eyebrow, NO mouth, and NO top loading dots.**
6. **`EMOTION_SLEEP` (`4`)**: Dim slate closed eye lines with a floating, fading **`z Z Z` snoring animation** drifting upward from the right eye.
7. **`EMOTION_SCARED_CLIFF` (`5`)**: Triggered immediately when either TCRT5000 sensor detects a table drop-off (`HIGH`). Renders high-frequency shivering electric cyan anime **`> <` wincing chevrons** (thick clean strokes, **no mouth**, **no pupils**) while executing a `450ms` emergency reverse.
8. **`EMOTION_DIZZY_PICKUP` (`6`)**: Triggered when **both** TCRT5000 sensors read empty air (`HIGH`) for `> 350ms` (robot grabbed/lifted off the desk). Immediately stops wheel servos, wobbles the neck gimbal, and renders **counter-rotating magenta spiral eyes only** (**no mouth and no spinning halo stars**).

---

## Agent Directives & Technical Rules (DOs and DON'Ts)

### 1. FreeRTOS Core Separation (STRICT)
- **Core 0 (`TaskCore0Network`):** Handles Wi-Fi connection, Captive Portal fallback (`PenBot-AI-Setup`), `ESPAsyncWebServer`, WebSocket JSON parsing, NVS `VoiceConfig` storage, and HTTPS AI API calls.
- **Core 1 (`TaskCore1Hardware`):** Runs a deterministic `60Hz` (`16ms`) loop polling `TCRT5000` cliff/pickup states first, `VL53L0X` ToF distance second, updating `ESP32Servo` PWM with cubic easing (`easeValue`), and pushing the `240x240` PSRAM sprite to the `ST7789` screen.
- **NO Cross-Core Race Conditions:** Core 0 and Core 1 must communicate exclusively via `cmdQueue` (`xQueueSend` / `xQueueReceive`) and `telemetryMutex` (`xSemaphoreTake`).

### 2. Power & Memory Safety Mandates
- **PSRAM Allocation:** Always enable `-DBOARD_HAS_PSRAM` and `board_build.arduino.memory_type = qio_opi` in `platformio.ini` so the 115KB `240x240` 16-bit display sprite lives in the 8MB Octal PSRAM instead of exhausting internal SRAM.
- **Servo Slew-Rate Limiting:** Never snap neck servos (`GPIO 37, 38`) instantaneously across wide angles; always interpolate via `easeValue()` to prevent current spikes on the MT3608 5V boost rail.
- **Zero `delay()` in Tasks:** Never call blocking `delay()` inside `AsyncWebServer` callbacks. Use `vTaskDelay(pdMS_TO_TICKS(ms))` inside FreeRTOS tasks.

### 3. Web UI Parity & Mirroring Mandate (STRICT)
- **Zero-Drift Rule:** The local preview files in `web-dashboard/` and the flashed LittleFS assets in `firmware/data/` MUST ALWAYS BE 100% IDENTICAL. What the user previews locally in their browser is guaranteed to be what is on the real robot once flashed.
- **Automatic Sync Hook:** Whenever ANY edit is made to `web-dashboard/` (whether in `desktop/index.html`, `mobile/index.html`, or root `index.html`), agents MUST immediately run `python scripts/embed_web.py` to mirror all files into `firmware/data/`.
- **Automatic Browser Responsiveness:** Mobile and Desktop UI are separated into dedicated folders (`mobile/` and `desktop/`). Routing between them is fully automatic based on screen width (`< 768px`) and mobile User-Agent. Do NOT add manual toggle buttons.
- **Build Verification:** Always re-run `python -m platformio run --target buildfs` and `python -m platformio run` after web edits to verify the compiled LittleFS filesystem image contains the latest assets.

### 4. Build & Flash Commands
```bash
# 1. Mirror the latest web dashboard assets into LittleFS data folder
python scripts/embed_web.py

# 2. Compile C++ Firmware
pio run

# 3. Upload LittleFS Web Dashboard Image to ESP32-S3 Flash
pio run --target uploadfs

# 4. Upload Dual-Core Firmware to ESP32-S3
pio run --target upload
```

### 5. Quick Push to `penzero00/pen-ai-bot`
```bash
git init
git add AGENTS.md platformio.ini partitions_16MB.csv firmware/ web-dashboard/
git commit -m "feat(monolith): finalize ESP32-S3 firmware, procedural eyes, battery HUD, and AI voice studio"
git branch -M main
git remote add origin https://github.com/penzero00/pen-ai-bot.git
git push -u origin main
```