#pragma once

// ============================================================================
// PEN-AI-BOT (CYBER-CUBE MINI) — MASTER ESP32-S3 (N16R8) PINOUT
// ============================================================================
// CRITICAL SAFETY: Never assign to Strapping Pins (0, 3, 45, 46)
// or Octal PSRAM/Flash Pins (26-32).

// --- 1. FACE DISPLAY (1.3" IPS TFT ST7789 240x240 SPI - 7-PIN & 8-PIN) ---
#define PIN_TFT_SCK         12  // SCL / SCK Hardware SPI Clock
#define PIN_TFT_MOSI        11  // SDA / MOSI Hardware SPI Data
#define PIN_TFT_DC          9   // DC / Data-Command Selector
#define PIN_TFT_RST         13  // RES / Reset (Active Low)
#define PIN_TFT_BLK         10  // BLK / Backlight (Driven HIGH; or wire BLK directly to 3V3)
#define PIN_TFT_CS          -1  // No CS pin on 7-pin displays (CS is tied to GND internally)

// --- 2. AUDIO SYSTEM (I2S INMP441 MIC & MAX98357A AMP) ---
#define PIN_MIC_SD          2
#define PIN_MIC_WS          42
#define PIN_MIC_SCK         41

#define PIN_AMP_BCLK        16
#define PIN_AMP_LRC         17
#define PIN_AMP_DIN         18

// Amp uses I2S port 1 (mic owns port 0). SD_MODE tied to 3V3 = LEFT channel only
// (datasheet: logic-high SD selects left word). Always put audio on LEFT (and duplicate
// to RIGHT for floating-SD / mono-mix modules).
#define I2S_AMP_PORT_NUM    1
#define AMP_SAMPLE_RATE_HZ  16000
#define AMP_BOOT_BEEP_HZ    880
#define AMP_BOOT_BEEP_MS    600

// --- 3. SAFETY & INTERACTION SENSORS ---
#define PIN_TOUCH_PAD       4   // Capacitive Touch 4 (Top Brass Standoff)
#define PIN_TOF_SDA         8   // VL53L0X Laser I2C Data (Mounted above LCD in head)
#define PIN_TOF_SCL         15  // VL53L0X Laser I2C Clock (Mounted above LCD in head)
#define PIN_USONIC_TRIG     1   // Ultrasonic Trigger (Mounted in front of robot body)
#define PIN_USONIC_ECHO     5   // Ultrasonic Echo (Mounted in front of robot body)

// TCRT5000 Cliff IR Sensor:
// Supports single center sensor (space-saving) or dual left/right sensors.
// Default center sensor is assigned to GPIO 6 (or change to 7 if wired to GPIO 7).
#define PIN_CLIFF_CENTER_D0 6   // Center TCRT5000 Digital Out (Single middle sensor)
#define PIN_CLIFF_LEFT_D0   PIN_CLIFF_CENTER_D0   // Aliased to center sensor
#define PIN_CLIFF_RIGHT_D0  PIN_CLIFF_CENTER_D0   // Aliased to center sensor

// --- 4. SERVO KINEMATICS (WHEELS & PAN/TILT NECK) ---
// CRITICAL HARDWARE SPECIFICATION FOR ESP32-S3 (N16R8):
// GPIO 33 to 37 are permanently bonded to the internal 8MB Octal PSRAM bus!
// Using GPIO 35, 36, or 37 causes servos to spin continuously from 80MHz PSRAM memory traffic.
// Reassigned to safe general-purpose GPIOs:
#define PIN_SERVO_WHEEL_L   21  // 360° Continuous Left Wheel (Right Header J3 Pin 18)
#define PIN_SERVO_WHEEL_R   47  // 360° Continuous Right Wheel (Right Header J3 Pin 17)
#define PIN_SERVO_NECK_PAN  14  // 180° Neck Pan (Left Header J1 Pin 20)
// GPIO 38 reserved / unused (Neck configured for horizontal panning only)

// --- 5. KINEMATICS & SAFETY THRESHOLDS ---
#define WALL_STOP_DIST_MM   120 // Stop forward drive if wall < 120mm
#define TOUCH_THRESHOLD     40000 // ESP32-S3 capacitive touch trigger threshold
#define CLIFF_REVERSE_MS    450 // Emergency reverse duration on table edge (ms)
#define PICKUP_DEBOUNCE_MS  350 // Both TCRT5000 in air > 350ms = Picked up (DIZZY)

// Servo Neutral & Trim Defaults (Microseconds / Degrees)
#define WHEEL_STOP_PWM      90
#define NECK_PAN_CENTER     90
#define NECK_TILT_CENTER    90
