# pen-ai-bot (Cyber-Cube Mini) — Master Hardware Pinout & Direct Wiring Specification

Comprehensive hardware pinout, electrical specification, and direct assembly guide for the **ESP32-S3 DevKitC-1 (N16R8)** microcontroller (16MB Flash, 8MB Octal PSRAM).

This guide is designed for **permanent direct assembly** (point-to-point soldering, perfboard/protoboard, custom PCB, or crimped wire harnesses). It does **not** use a solderless breadboard, which is unsuitable for compact desktop robots due to high contact resistance and servo stall current drops (>1.5A).

---

## 1. ESP32-S3 DevKitC-1 Official Physical Header Pinout

Official 44-pin header layout for the **ESP32-S3 DevKitC-1** board (USB ports facing downward).
- **Left Header (J1)**: 22 pins
- **Right Header (J3)**: 22 pins

> [!CAUTION]
> **Silkscreen Labels vs. Counting Pins:**
> Always read the white text printed on the PCB (`12`, `11`, `9`, etc.).
> Counting pins from the top is NOT the same as the GPIO number (e.g., physical Pin 10 is GPIO 17, while GPIO 10 is physical Pin 16).

```text
                               ESP32-S3 DevKitC-1 (Top View)
                                        ┌───────────┐
                                        │    PCB    │
                                        │  ANTENNA  │
                                        └───────────┘
               LEFT HEADER (J1)                               RIGHT HEADER (J3)
     [Pin 1 ]  3V3  ── 3.3V Power Out            [Pin 1 ]  GND  ── Common Ground
     [Pin 2 ]  3V3  ── 3.3V Power Out            [Pin 2 ]  TX   ── GPIO 43 (UART0 TX)
     [Pin 3 ]  RST  ── Reset (CHIP_PU)           [Pin 3 ]  RX   ── GPIO 44 (UART0 RX)
     [Pin 4 ]  4    ── GPIO 4  [Touch Screw]     [Pin 4 ]  1    ── GPIO 1  [Ultrasonic TRIG]
     [Pin 5 ]  5    ── GPIO 5  [Ultrasonic ECHO] [Pin 5 ]  2    ── GPIO 2  [Mic SD]
     [Pin 6 ]  6    ── GPIO 6  [Cliff Left D0]   [Pin 6 ]  42   ── GPIO 42 [Mic WS]
     [Pin 7 ]  7    ── GPIO 7  [Cliff Right D0]  [Pin 7 ]  41   ── GPIO 41 [Mic SCK]
     [Pin 8 ]  15   ── GPIO 15 [Laser SCL]       [Pin 8 ]  40   ── GPIO 40
     [Pin 9 ]  16   ── GPIO 16 [Amp BCLK]        [Pin 9 ]  39   ── GPIO 39
     [Pin 10]  17   ── GPIO 17 [Amp LRC]         [Pin 10]  38   ── GPIO 38
     [Pin 11]  18   ── GPIO 18 [Amp DIN]         [Pin 11]  37   ── [PSRAM DQS - DO NOT USE]
     [Pin 12]  8    ── GPIO 8  [Laser SDA]       [Pin 12]  36   ── [PSRAM D7  - DO NOT USE]
     [Pin 13]  3    ── [Strapping - DO NOT USE]  [Pin 13]  35   ── [PSRAM D6  - DO NOT USE]
     [Pin 14]  46   ── [Strapping - DO NOT USE]  [Pin 14]  0    ── [BOOT Btn  - DO NOT USE]
     [Pin 15]  9    ── GPIO 9  [TFT DC]          [Pin 15]  45   ── [Strapping - DO NOT USE]
     [Pin 16]  10   ── GPIO 10 [TFT BLK]         [Pin 16]  48   ── GPIO 48
     [Pin 17]  11   ── GPIO 11 [TFT SDA/MOSI]    [Pin 17]  47   ── GPIO 47 [Right Wheel Servo]
     [Pin 18]  12   ── GPIO 12 [TFT SCK/SCL]     [Pin 18]  21   ── GPIO 21 [Left Wheel Servo]
     [Pin 19]  13   ── GPIO 13 [TFT RES/RST]     [Pin 19]  20   ── GPIO 20 (USB D+)
     [Pin 20]  14   ── GPIO 14 [Neck Pan Servo]  [Pin 20]  19   ── GPIO 19 (USB D-)
     [Pin 21]  5V   ── 5V VBUS Input             [Pin 21]  GND  ── Common Ground
     [Pin 22]  GND  ── Common Ground             [Pin 22]  GND  ── Common Ground
                                        ┌───────────┐
                                        │ USB │ USB │
                                        └───────────┘
```

---

## 2. Master Pin Mapping Table (Sorted by GPIO)

| GPIO | Software Macro | Subsystem | Target Component Pin | Board Header Pin | Direction | Signal Type | Power Rail |
| :---: | :--- | :--- | :--- | :--- | :---: | :--- | :---: |
| **GPIO 1** | `PIN_USONIC_TRIG` | Obstacle Sensing | HC-SR04 / HC-SR04P `TRIG` | **Right J3 Pin 4** | Output | 10µs Digital Pulse | 3.3V Logic |
| **GPIO 2** | `PIN_MIC_SD` | Ears (Microphone) | INMP441 / MSM261 `SD` | **Right J3 Pin 5** | Input | I2S0 Serial Data | 3.3V Logic |
| **GPIO 4** | `PIN_TOUCH_PAD` | Touch Sensor | Top Brass Standoff Screw | **Left J1 Pin 4** | Input | `TOUCH4` Analog Capacitive | 3.3V Logic |
| **GPIO 5** | `PIN_USONIC_ECHO` | Obstacle Sensing | HC-SR04 / HC-SR04P `ECHO` | **Left J1 Pin 5** | Input | Digital Pulse Width | 3.3V Logic |
| **GPIO 6** | `PIN_CLIFF_CENTER_D0`| Edge Safety | Center / Left TCRT5000 `D0` | **Left J1 Pin 6** | Input | Digital (`HIGH` = Cliff) | 3.3V Logic |
| **GPIO 7** | `PIN_CLIFF_RIGHT_D0` | Edge Safety | Optional Right TCRT5000 `D0`| **Left J1 Pin 7** | Input | Digital (`HIGH` = Cliff) | 3.3V Logic |
| **GPIO 8** | `PIN_TOF_SDA` | Head Laser ToF | VL53L0X Laser `SDA` | **Left J1 Pin 12** | In/Out | I2C Data (400kHz) | 3.3V Logic |
| **GPIO 9** | `PIN_TFT_DC` | Face Display | 1.3" IPS ST7789 Pin 6 (`DC`) | **Left J1 Pin 15** | Output | SPI Command/Data | 3.3V Logic |
| **GPIO 10** | `PIN_TFT_BLK` | Face Display | 1.3" IPS ST7789 Pin 7 (`BLK`)| **Left J1 Pin 16** | Output | Backlight Enable (HIGH) | 3.3V Logic |
| **GPIO 11** | `PIN_TFT_MOSI` | Face Display | 1.3" IPS ST7789 Pin 4 (`SDA`)| **Left J1 Pin 17** | Output | SPI MOSI Data | 3.3V Logic |
| **GPIO 12** | `PIN_TFT_SCK` | Face Display | 1.3" IPS ST7789 Pin 3 (`SCK`)| **Left J1 Pin 18** | Output | SPI SCLK Clock | 3.3V Logic |
| **GPIO 13** | `PIN_TFT_RST` | Face Display | 1.3" IPS ST7789 Pin 5 (`RES`)| **Left J1 Pin 19** | Output | SPI Reset (Active Low) | 3.3V Logic |
| **GPIO 14** | `PIN_SERVO_NECK_PAN` | Head Gimbal | 180° Micro Servo Pan PWM | **Left J1 Pin 20** | Output | 50Hz PWM Pulse | 3.3V Signal / 5V VDD |
| **GPIO 15** | `PIN_TOF_SCL` | Head Laser ToF | VL53L0X Laser `SCL` | **Left J1 Pin 8** | Output | I2C Clock (400kHz) | 3.3V Logic |
| **GPIO 16** | `PIN_AMP_BCLK` | Mouth (Audio Amp) | MAX98357A `BCLK` | **Left J1 Pin 9** | Output | I2S1 Bit Clock | 3.3V Logic |
| **GPIO 17** | `PIN_AMP_LRC` | Mouth (Audio Amp) | MAX98357A `LRC` | **Left J1 Pin 10** | Output | I2S1 Word Select | 3.3V Logic |
| **GPIO 18** | `PIN_AMP_DIN` | Mouth (Audio Amp) | MAX98357A `DIN` | **Left J1 Pin 11** | Output | I2S1 Audio Data | 3.3V Logic |
| **GPIO 21** | `PIN_SERVO_WHEEL_L` | Drive Kinematics | Left 360° Continuous Servo PWM | **Right J3 Pin 18** | Output | 50Hz PWM Pulse | 3.3V Signal / 5V VDD |
| **GPIO 41** | `PIN_MIC_SCK` | Ears (Microphone) | INMP441 / MSM261 `SCK` | **Right J3 Pin 7** | Output | I2S0 Bit Clock | 3.3V Logic |
| **GPIO 42** | `PIN_MIC_WS` | Ears (Microphone) | INMP441 / MSM261 `WS` | **Right J3 Pin 6** | Output | I2S0 Word Select | 3.3V Logic |
| **GPIO 47** | `PIN_SERVO_WHEEL_R` | Drive Kinematics | Right 360° Continuous Servo PWM| **Right J3 Pin 17** | Output | 50Hz PWM Pulse | 3.3V Signal / 5V VDD |

---

## 3. Two-Tier Physical Structural Breakdown

```text
 ┌────────────────────────────────────────────────────────────────────────┐
 │                      TIER 2: HEAD & TOP SHELL                          │
 │                                                                        │
 │  • ESP32-S3 DevKitC-1 (Core Microcontroller)                           │
 │  • ST7789 1.3" IPS Display (Face) ──── [7 wires to Left Header J1]     │
 │  • VL53L0X Laser ToF Sensor       ──── [4 wires to ESP32-S3]           │
 │  • INMP441 MEMS Microphone (Ears) ──── [5 wires to Right Header J3]    │
 │  • Top Brass Standoff Screw       ──── [1 wire to Left Header J1 Pin 4]│
 └────────────────────────────────────┬───────────────────────────────────┘
                                      │
         FLEXIBLE NECK PASS-THROUGH HARNESS (AWG 28/30 Silicone Wire)
         • 5V High-Current Boost Rail (Red)
         • Common Ground GND (Black)
         • Servo PWM: Wheel L (GPIO 21), Wheel R (GPIO 47), Neck Pan (GPIO 14)
         • I2S Audio Out: Amp BCLK (16), LRC (17), DIN (18)
         • Ultrasonic: TRIG (1), ECHO (5)
         • Cliff IR D0: Center/Left (6), Optional Right (7)
                                      │
 ┌────────────────────────────────────┴───────────────────────────────────┐
 │                      TIER 1: LOWER DRIVE CHASSIS                       │
 │                                                                        │
 │  • 1S LiPo Battery (3.7V) + Power Slide Switch                         │
 │  • TP4056 Type-C Charger & Battery Protection Board                    │
 │  • MT3608 DC-DC Step-Up Converter (Tuned to 5.0V Output)               │
 │  • 1000µF 16V Low-ESR Electrolytic Buffer Capacitor                   │
 │  • 2x 360° Continuous Servos (Left Wheel & Right Wheel)                │
 │  • 1x 180° Positional Micro Servo (Neck Pan Gimbal)                   │
 │  • MAX98357A 3W Class-D Amp + 8Ω 2-3W Miniature Speaker                │
 │  • HC-SR04 / HC-SR04P Ultrasonic Sensor (Front Body Bumper)           │
 │  • 1x or 2x Blue TCRT5000 IR Modules (Single Center D0: 6, or Dual)   │
 └────────────────────────────────────────────────────────────────────────┘
```

---

## 4. Power Architecture & Star Grounding

```text
 [ 1S 3.7V LiPo Battery ]
   │                 │
 (Red +)          (Black -)
   │                 │
   ▼                 ▼
 [ B+ ]            [ B- ]  ◄── TP4056 Charger & Protection Module (USB-C)
 [OUT+]            [OUT-]
   │                 │
 [SWITCH]            │     ◄── Power Slide Switch in-line on OUT+
   │                 │
   ▼                 ▼
 [VIN+]            [VIN-]  ◄── MT3608 Step-Up Boost Module (Tuned to 5.0V)
 [VOUT+]           [VOUT-]
   │                 │
   ├─────────────────┼───► [ 1000µF 16V Buffer Cap ] (Solder across VOUT+ & VOUT-)
   │                 │
   ├─────────────────┼───► 5.0V Servo Bus (Left Wheel 21, Right Wheel 47, Neck 14)
   ├─────────────────┼───► MAX98357A 3W Audio Amp (VIN & GND)
   │                 │
   └─────────────────┴───► ESP32-S3 DevKitC-1 (Pin 5V / VBUS & GND)
```

### Power Rules:
1. **Dedicated 5.0V Servo/Amp Rail:** All three servo positive wires and MAX98357A `VIN` connect to MT3608 `5V OUT`. **NEVER power servos or amplifier from ESP32 3V3.**
2. **1000µF 16V Buffer Capacitor:** Solder directly across `5V OUT` and `GND OUT` of the MT3608 to eliminate brownouts during servo stalls.
3. **Dedicated 3.3V Sensor Rail:** Power the ST7789 display, VL53L0X, TCRT5000, and INMP441 exclusively from ESP32-S3 `3V3` pins.
4. **Single-Point Star Ground:** All ground lines converge at the MT3608/TP4056 output ground pad.

---

## 5. Direct Wiring by Component

### 5.1 Face Display (1.3" IPS TFT ST7789 240×240 — 7-Pin Module)
*Location: Tier 2 Head Faceplate*

On standard 7-pin ST7789 breakout modules, the header pins are in this **exact order from left to right**:
`VCC, GND, SCK, SDA, RES, DC, BLK` (Chip Select `CS` is permanently grounded internally on the display PCB).

#### Direct Pin-by-Pin Wiring Table (Display Pin 1 through 7)

| Display Pin # | Header Label Printed on Display | Wire Target on ESP32-S3 | Physical Position on Left Header (J1) | Signal Function | Required Logic |
| :---: | :---: | :---: | :--- | :--- | :---: |
| **Pin 1** | **`VCC`** | **`3V3`** | **Header Pin 1 or 2** (Top pin on left side) | 3.3V Logic & Driver Power | **3.3V ONLY** (Do NOT use 5V) |
| **Pin 2** | **`GND`** | **`GND`** | **Header Pin 22** (Bottom-most pin on left side) | Power & Logic Ground Bus | 0.0V Reference |
| **Pin 3** | **`SCK`** / `SCL` | **`GPIO 12`** | **Header Pin 18** (5th pin from the bottom, silkscreen labeled `12`) | SPI Hardware Clock | 3.3V High-Speed Clock |
| **Pin 4** | **`SDA`** / `MOSI`| **`GPIO 11`** | **Header Pin 17** (6th pin from the bottom, silkscreen labeled `11`) | SPI Master-Out Slave-In Data | 3.3V Serial Data |
| **Pin 5** | **`RES`** / `RST` | **`GPIO 13`** | **Header Pin 19** (4th pin from the bottom, silkscreen labeled `13`) | Hardware Chip Reset | Active LOW / High in operation |
| **Pin 6** | **`DC`** / `RS`   | **`GPIO 9`**  | **Header Pin 15** (8th pin from the bottom, silkscreen labeled `9`)  | Command (LOW) / Data (HIGH) | 3.3V Control Line |
| **Pin 7** | **`BLK`** / `BL`  | **`GPIO 10`** (or `3V3`) | **Header Pin 16** (7th pin from the bottom, silkscreen labeled `10`)| Backlight LED Power | 3.3V HIGH (Illumination) |

> [!IMPORTANT]
> **Silkscreen Labels vs. Counting from Top (DO NOT COUNT PINS):**
> Look at the tiny white numbers etched directly on the ESP32 board beside each header hole:
> - Do **NOT** count 10 pins down from the top to find "GPIO 10". If you count 10 pins from the top, you hit **GPIO 17**!
> - Find the actual silkscreen printing that says **`9`**, **`10`**, **`11`**, **`12`**, and **`13`** near the bottom-left corner of the ESP32 board.

#### Physical Visual Wiring Diagram

```text
  7-Pin ST7789 Display Module                    ESP32-S3 DevKitC-1 (Left Header J1)
  ┌──────────────────────────┐                    ┌─────────────────────────────────┐
  │ [1] VCC  ────────────────┼────────────────────► [Pin 1 or 2]  3V3 (3.3V Power)   │
  │ [2] GND  ────────────────┼──────────┐         │  ...                            │
  │ [3] SCK  ────────┐       │          │         │ [Pin 15]  GPIO 9  ◄── (Pin 6 DC)│
  │ [4] SDA  ─────┐  │       │          │         │ [Pin 16]  GPIO 10 ◄── (Pin 7 BLK)│
  │ [5] RES  ──┐  │  │       │          │         │ [Pin 17]  GPIO 11 ◄── (Pin 4 SDA)│
  │ [6] DC   ──┼──┼──┼───────┼──────────┼─────────┤ [Pin 18]  GPIO 12 ◄── (Pin 3 SCK)│
  │ [7] BLK  ──┼──┼──┼───────┼──────────┼─────────┤ [Pin 19]  GPIO 13 ◄── (Pin 5 RES)│
  └────────────┼──┼──┼───────┘          │         │ [Pin 20]  GPIO 14 (Neck Servo)  │
               │  │  └──────────────────┼─────────┤ [Pin 21]  5V     (5V Boost In)  │
               │  └─────────────────────┼─────────┤ [Pin 22]  GND  ◄── (Pin 2 GND)  │
               └────────────────────────┼─────────┘                                 │
                                        └───────────────────────────────────────────┘
```

#### Why Does the Screen Stay Grey (Troubleshooting):
1. **Screen glows grey/white with backlight on:**
   - Power (`VCC`, `GND`) and Backlight (`BLK`) are connected, BUT the ST7789 controller is not receiving or decoding SPI commands.
   - **Check SCK vs SDA:** If wire 3 (`SCK`) and wire 4 (`SDA`) are inverted, the chip receives clock on data line and data on clock line. Swap them and see if the screen immediately displays graphics!
   - **Check DC vs RES:** If wire 5 (`RES`) and wire 6 (`DC`) are swapped, the screen stays in permanent reset. Swap them and check.
   - **Check Pin 7 (BLK):** If connected to ESP32 Pin 16 (`GPIO 10`), ensure GPIO 10 is driven HIGH. Alternatively, plug Pin 7 directly into the `3V3` rail.

---

### 5.2 Drive & Neck Servos
*Location: Tier 1 Lower Chassis (Drive Wheels) & Neck Mount*

> [!WARNING]
> **Octal PSRAM Conflict Solved:**
> On the ESP32-S3 N16R8, GPIO 33–37 are internally bonded to the 8MB Octal PSRAM bus.
> Servos MUST be wired to GPIO 21, 47, and 14 as specified below:

| Servo | Wire Color | Connection Target | Control Specification |
| :--- | :---: | :--- | :--- |
| **Left Wheel (360°)** | Brown / Black (GND) | Central Star Ground | Common ground |
| | Red (Power) | **MT3608 5.0V Boost Rail** | 5V high-current rail |
| | Orange / Yellow (Signal) | **ESP32-S3 `GPIO 21`** (Right J3 Pin 18) | 50Hz PWM (Stop = 90°) |
| **Right Wheel (360°)**| Brown / Black (GND) | Central Star Ground | Common ground |
| | Red (Power) | **MT3608 5.0V Boost Rail** | 5V high-current rail |
| | Orange / Yellow (Signal) | **ESP32-S3 `GPIO 47`** (Right J3 Pin 17) | 50Hz PWM (Stop = 90°) |
| **Neck Pan (180°)** | Brown / Black (GND) | Central Star Ground | Common ground |
| | Red (Power) | **MT3608 5.0V Boost Rail** | 5V high-current rail |
| | Orange / Yellow (Signal) | **ESP32-S3 `GPIO 14`** (Left J1 Pin 20) | 50Hz PWM (Center = 90°) |

---

### 5.3 Head Laser Distance Sensor (VL53L0X ToF)
*Location: Tier 2 Head (above LCD screen, pans with neck)*

| Module Pin | Wire Color Recommendation | Solder / Wire Target on ESP32-S3 | Notes |
| :--- | :---: | :--- | :--- |
| **VIN** | Red | **`3V3`** (Left J1 Pin 1 or 2) | 3.3V power |
| **GND** | Black | **`GND`** (Common Ground) | Sensor ground |
| **SDA** | Blue | **`GPIO 8`** (Left J1 Pin 12) | I2C Data line |
| **SCL** | Yellow | **`GPIO 15`** (Left J1 Pin 8) | I2C Clock line |
| **XSHUT** | — | *Unconnected* | Pulled high internally on breakout |
| **GPIO1** | — | *Unconnected* | Unused |

---

### 5.4 Ears — Microphone (INMP441 / MSM261 I2S)
*Location: Tier 2 Head / Side Ear Slot*

| Module Pin | Wire Color Recommendation | Solder / Wire Target on ESP32-S3 | Notes |
| :--- | :---: | :--- | :--- |
| **VDD** | Red | **`3V3`** | Clean 3.3V logic power |
| **GND** | Black | **`GND`** | Audio ground |
| **SD** | Green | **`GPIO 2`** (Right J3 Pin 5) | Serial audio data |
| **WS** | Orange | **`GPIO 42`** (Right J3 Pin 6) | Word select (LRCLK) |
| **SCK** | Yellow | **`GPIO 41`** (Right J3 Pin 7) | Bit clock (BCLK) |
| **L/R** | Black | Bridge directly to `GND` pin | Sets module to Left channel |

---

### 5.5 Mouth — Audio Amplifier (MAX98357A) & Speaker
*Location: Tier 1 Base Chassis*

| Module Pin | Wire Color Recommendation | Solder / Wire Target | Critical Requirement |
| :--- | :---: | :--- | :--- |
| **VIN** | Red (Thick) | **MT3608 5.0V Boost Rail** | **NEVER connect to ESP32 3V3 rail!** Solder/crimp solid — a loose VIN makes the amp go silent or “click” intermittently. |
| **GND** | Black (Thick) | **Central Star Ground (GND)**| Must share ESP32 GND. A floating/loose amp GND is the #1 cause of “I2S OK but no sound.” |
| **DIN** | Blue | **`GPIO 18`** (Left J1 Pin 11) | I2S Audio data stream |
| **BCLK** | Yellow | **`GPIO 16`** (Left J1 Pin 9) | I2S Bit clock |
| **LRC** | Orange | **`GPIO 17`** (Left J1 Pin 10) | I2S Word select |
| **GAIN** | — | **`GND`** (validated on this build) | **12dB** — loudest reliable strap here. Floating = quieter (9dB). `GAIN→VIN` got *quieter* on this module (likely resistive/3dB band) — do not use unless solid soldered and verified louder. |
| **SD_MODE** | — | **`3V3` (required)** | This module goes silent if SD floats. `SD→3V3` = LEFT channel (firmware drives LEFT). Never tie SD to GND (shutdown). |
| **SPK+** | Red | 8Ω Speaker (+) terminal | Connect directly to speaker |
| **SPK-** | Black | 8Ω Speaker (-) terminal | **BTL Output: NEVER connect to GND!** |

---

### 5.6 Front Chassis Ultrasonic Sensor (HC-SR04 / HC-SR04P)
*Location: Tier 1 Lower Front Chassis*

| Sensor Pin | Solder / Wire Target | Notes |
| :--- | :--- | :--- |
| **VCC** | ESP32-S3 **`3V3`** (if HC-SR04P) or MT3608 **`5V`** (if HC-SR04) | Power rail |
| **GND** | Central Star Ground **`GND`** | Ground reference |
| **TRIG** | **`GPIO 1`** (Right J3 Pin 4) | 10µs digital trigger pulse |
| **ECHO** | **`GPIO 5`** (Left J1 Pin 5) | Digital pulse width (3.3V safe) |

---

### 5.7 Edge Cliff & Grab Sensors (2x TCRT5000 IR Modules)
*Location: Tier 1 Underside Front Corners (pointing 45° down toward desk)*

| Module Pin | Target Connection | Behavior & Notes |
| :--- | :--- | :--- |
| **Left TCRT5000 VCC** | 3.3V Sensor Bus | 3.3V power |
| **Left TCRT5000 GND** | Central Star Ground | Ground reference |
| **Left TCRT5000 D0** | **`GPIO 6`** (Left J1 Pin 6) | `LOW` = Surface, `HIGH` = Cliff edge |
| **Right TCRT5000 VCC**| 3.3V Sensor Bus | 3.3V power |
| **Right TCRT5000 GND**| Central Star Ground | Ground reference |
| **Right TCRT5000 D0**| **`GPIO 7`** (Left J1 Pin 7) | `LOW` = Surface, `HIGH` = Cliff edge |

---

### 5.8 Capacitive Touch Standoff
*Location: Tier 2 Top Shell*

- Connect a single **AWG 28** wire directly from the top brass standoff screw to **ESP32-S3 `GPIO 4`** (Left J1 Pin 4).

---

## 6. Forbidden & Restricted Pins Check

When soldering or connecting wires, **NEVER** wire any peripheral to these pins:

- ❌ **GPIO 0, 3, 45, 46**: ESP32-S3 boot strapping pins. Wiring peripherals here causes boot failures.
- ❌ **GPIO 26 through 32**: Internal high-speed Octal SPI Flash bus.
- ❌ **GPIO 33 through 37**: Internal 8MB Octal PSRAM bus (N16R8). Wiring servos/sensors here causes 80MHz memory noise and servo runaway.
- ❌ **GPIO 19, 20**: Reserved for native USB CDC / JTAG serial flashing.
