#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

enum EyeEmotion : uint8_t {
    EMOTION_IDLE = 0,
    EMOTION_HAPPY,
    EMOTION_ANGRY,
    EMOTION_THINKING,
    EMOTION_SLEEP,
    EMOTION_SCARED_CLIFF,
    EMOTION_DIZZY_PICKUP
};

enum DriveMode : uint8_t {
    MODE_MANUAL = 0,
    MODE_AUTONOMOUS,
    MODE_EMERGENCY_CLIFF,
    MODE_SUSPENDED_AIR
};

// Command packet passed from Core 0 (Web/AI) -> Core 1 (Hardware)
struct RobotCommand {
    int8_t joyX;          // -100 to +100
    int8_t joyY;          // -100 to +100
    uint8_t targetPan;    // 30° to 150°
    uint8_t targetTilt;   // 45° to 135°
    EyeEmotion emotion;
    DriveMode mode;
    bool triggerMacro;
    bool showPairingQr;   // When true, renders QR pairing screen on ST7789
};

// Customizable AI Voice Configuration stored in NVS
struct VoiceConfig {
    char voiceName[24];   // e.g., "Kore", "Puck", "Zephyr", "Fenrir", "Leda"
    float pitch;          // 0.5 to 1.5
    float speed;          // 0.7 to 1.5
    uint8_t volume;       // 0 to 100
};

// Live telemetry shared from Core 1 (Sensors) -> Core 0 (WebSocket 10Hz Broadcast)
struct RobotTelemetry {
    uint16_t tofDistanceMm;      // Head Laser ToF Distance (VL53L0X, above LCD)
    uint16_t usonicDistanceMm;   // Front Body Ultrasonic Distance (HC-SR04)
    bool cliffLeftTriggered;
    bool cliffRightTriggered;
    bool pickedUpAirborne;
    bool touchActive;
    uint8_t currentPan;
    uint8_t currentTilt;
    int8_t wheelLeftSpeed;
    int8_t wheelRightSpeed;
    EyeEmotion currentEmotion;
    DriveMode currentMode;
    uint32_t freePsramBytes;
    bool wifiConnected;
    char wifiIp[16];
    char wifiSsid[32];
    bool isApMode;
    bool showPairingQr;
    char botName[32];
};

extern QueueHandle_t cmdQueue;
extern SemaphoreHandle_t telemetryMutex;
extern RobotTelemetry globalTelemetry;
extern VoiceConfig globalVoiceConfig;
