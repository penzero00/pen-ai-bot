#pragma once

// ============================================================================
// PEN-AI-BOT (CYBER-CUBE MINI) — MASTER ESP32-S3 (N16R8) PINOUT
// ============================================================================
// CRITICAL SAFETY: Never assign to Strapping Pins (0, 3, 45, 46)
// or Octal PSRAM/Flash Pins (26-32).

// --- 1. FACE DISPLAY (1.3" IPS TFT ST7789 240x240 SPI) ---
#define PIN_TFT_SCK         12
#define PIN_TFT_MOSI        11
#define PIN_TFT_CS          10
#define PIN_TFT_DC          9
#define PIN_TFT_RST         13
// Note: BLK (Backlight) is wired directly to 3V3

// --- 2. AUDIO SYSTEM (I2S INMP441 MIC & MAX98357A AMP) ---
#define PIN_MIC_SD          2
#define PIN_MIC_WS          42
#define PIN_MIC_SCK         41

#define PIN_AMP_BCLK        16
#define PIN_AMP_LRC         17
#define PIN_AMP_DIN         18

// --- 3. SAFETY & INTERACTION SENSORS ---
#define PIN_TOUCH_PAD       4   // Capacitive Touch 4 (Top Brass Standoff)
#define PIN_TOF_SDA         8   // VL53L0X Laser I2C Data (Mounted above LCD in head)
#define PIN_TOF_SCL         15  // VL53L0X Laser I2C Clock (Mounted above LCD in head)
#define PIN_USONIC_TRIG     1   // Ultrasonic Trigger (Mounted in front of robot body)
#define PIN_USONIC_ECHO     5   // Ultrasonic Echo (Mounted in front of robot body)
#define PIN_CLIFF_LEFT_D0   6   // Front-Left TCRT5000 Digital Out
#define PIN_CLIFF_RIGHT_D0  7   // Front-Right TCRT5000 Digital Out

// --- 4. SERVO KINEMATICS (WHEELS & PAN/TILT NECK) ---
#define PIN_SERVO_WHEEL_L   35  // 360° Continuous Left Wheel
#define PIN_SERVO_WHEEL_R   36  // 360° Continuous Right Wheel
#define PIN_SERVO_NECK_PAN  37  // 180° Neck Pan (Horizontal Only)
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
