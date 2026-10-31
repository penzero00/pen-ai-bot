#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <Wire.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <Update.h>
#include <Adafruit_VL53L0X.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include <ESPAsyncWebServer.h>
#include <TFT_eSPI.h>

#include "config.h"
#include "qrcode.h"
#include "state.h"

// Global RTOS synchronization primitives & customizable bot profile
QueueHandle_t cmdQueue;
SemaphoreHandle_t telemetryMutex;
char globalBotName[32] = "PenBot";

RobotTelemetry globalTelemetry = {
    850,                   // tofDistanceMm (Head Laser ToF)
    800,                   // usonicDistanceMm (Front Body Ultrasonic)
    false,                 // cliffLeftTriggered
    false,                 // cliffRightTriggered
    false,                 // pickedUpAirborne
    false,                 // touchActive
    90,                    // currentPan
    90,                    // currentTilt
    0,                     // wheelLeftSpeed
    0,                     // wheelRightSpeed
    EMOTION_IDLE,          // currentEmotion
    MODE_MANUAL,           // currentMode
    0,                     // freePsramBytes
    false,                 // wifiConnected
    "192.168.4.1",         // wifiIp
    "PenBot-Setup",        // wifiSsid
    true,                  // isApMode
    false,                 // showPairingQr
    "PenBot"               // botName
};
VoiceConfig globalVoiceConfig = {"Puck", 1.10f, 1.05f, 85};

// Hardware instances (Core 1) — Panning only, no tilting
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite faceSprite = TFT_eSprite(&tft);
Servo servoWheelL, servoWheelR, servoNeckPan;
Adafruit_VL53L0X tofSensor = Adafruit_VL53L0X();
bool tofOnline = false;

// Network instances (Core 0)
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
Preferences prefs;
DNSServer dnsServer;

// Smooth Servo Easing helper (Prevents 5V Rail Brownouts)
float easeValue(float current, float target, float factor) {
  return current + (target - current) * factor;
}

// ============================================================================
// UPPER-RIGHT BATTERY HUD (COMPACT 27PX LENGTH, NUMBER CENTERED INSIDE)
// ============================================================================
void drawBatteryHud(uint8_t batteryPct) {
  uint16_t batColor = (batteryPct > 20) ? 0x07E0 : 0xF800; // Emerald or Red
  char buf[8];
  snprintf(buf, sizeof(buf), "%u%%", batteryPct);

  // Compact battery outer frame (27x14px at 204,8) + Terminal nub (2x8px at 231,11)
  faceSprite.drawRect(204, 8, 27, 14, batColor);
  faceSprite.fillRect(231, 11, 2, 8, batColor);

  // Proportional inner level fill (0 to 23px wide)
  int16_t fillW = map(constrain(batteryPct, 0, 100), 0, 100, 0, 23);
  if (fillW > 0) {
    uint16_t fillColor = (batteryPct > 20) ? 0x03E0 : 0x7800; // Dim level bar
    faceSprite.fillRect(206, 10, fillW, 10, fillColor);
  }

  // Percentage number rendered strictly INSIDE the battery
  faceSprite.setTextDatum(MC_DATUM);
  faceSprite.setTextColor(TFT_WHITE, TFT_BLACK);
  faceSprite.drawString(buf, 217, 15, 1);
}

// ============================================================================
// ST7789 240x240 SETUP & PAIRING QR CODE SCREEN (CLEAN, NO EMOJIS)
// ============================================================================
void renderPairingScreen(const char *qrText, const char *ssid,
                         const char *pass, const char *url,
                         uint8_t batteryPct = 88) {
  faceSprite.fillSprite(TFT_BLACK);

  // Clean Header banner
  faceSprite.setTextDatum(TC_DATUM);
  faceSprite.setTextColor(0x07FF, TFT_BLACK); // Neon cyan
  char titleBuf[48];
  snprintf(titleBuf, sizeof(titleBuf), "%s SETUP", globalBotName);
  faceSprite.drawString(titleBuf, 120, 12, 2);

  drawBatteryHud(batteryPct);

  // Generate QR code (Version 3: 29x29 matrix)
  QRCode qrcode;
  uint8_t qrcodeData[256];
  if (qrcode_initText(&qrcode, qrcodeData, 3, ECC_LOW, qrText) == 0) {
    const int moduleScale = 3;
    int qrSize = qrcode.size * moduleScale;
    int startX = (240 - qrSize) / 2;
    int startY = 38;

    // White quiet zone border
    faceSprite.fillRoundRect(startX - 8, startY - 8, qrSize + 16, qrSize + 16, 6,
                             TFT_WHITE);

    for (uint8_t y = 0; y < qrcode.size; y++) {
      for (uint8_t x = 0; x < qrcode.size; x++) {
        if (qrcode_getModule(&qrcode, x, y)) {
          faceSprite.fillRect(startX + x * moduleScale,
                              startY + y * moduleScale, moduleScale,
                              moduleScale, TFT_BLACK);
        }
      }
    }
  }

  // Connection Credentials (Clean text, no emojis)
  faceSprite.setTextDatum(TC_DATUM);
  faceSprite.setTextColor(0xFFE0, TFT_BLACK); // Warm Gold
  char buf[48];
  snprintf(buf, sizeof(buf), "SSID: %s", ssid);
  faceSprite.drawString(buf, 120, 144, 2);

  faceSprite.setTextColor(TFT_WHITE, TFT_BLACK);
  snprintf(buf, sizeof(buf), "PASS: %s", pass);
  faceSprite.drawString(buf, 120, 166, 2);

  faceSprite.setTextColor(0x07E0, TFT_BLACK); // Emerald
  snprintf(buf, sizeof(buf), "URL: %s", url);
  faceSprite.drawString(buf, 120, 188, 2);

  faceSprite.setTextColor(0x7BEF, TFT_BLACK); // Dim slate
  faceSprite.drawString("Scan QR to pair phone", 120, 218, 1);

  faceSprite.pushSprite(0, 0);
}

// ============================================================================
// PROCEDURAL 60FPS EYE RENDERER (PSRAM SPRITE BUFFER)
// ============================================================================
void renderProceduralEyes(EyeEmotion emotion, uint32_t tick,
                          uint8_t batteryPct = 88) {
  faceSprite.fillSprite(TFT_BLACK);

  int16_t bounceY = (int16_t)(sin(tick * 0.08f) * 4.0f);
  bool isBlinking = (tick % 130) < 5;

  // 1. DIZZY (GRABBED / HELD AT HEIGHT) -> Counter-rotating spirals only (No
  // mouth, no halo)
  if (emotion == EMOTION_DIZZY_PICKUP) {
    uint16_t dizzyColor = 0xFD20; // Neon Magenta/Orange
    for (int r = 8; r <= 32; r += 8) {
      int16_t offLX = (int16_t)(cos(tick * 0.25f + r) * 5.0f);
      int16_t offLY = (int16_t)(sin(tick * 0.25f + r) * 5.0f);
      int16_t offRX = (int16_t)(cos(-tick * 0.25f + r) * 5.0f);
      int16_t offRY = (int16_t)(sin(-tick * 0.25f + r) * 5.0f);
      faceSprite.drawCircle(70 + offLX, 120 + offLY, r, dizzyColor);
      faceSprite.drawCircle(170 + offRX, 120 + offRY, r, dizzyColor);
    }
    drawBatteryHud(batteryPct);
    faceSprite.pushSprite(0, 0);
    return;
  }

  // 2. SCARED (CLIFF DETECTED) -> Anime "> <" terrified wincing eyes
  if (emotion == EMOTION_SCARED_CLIFF) {
    int16_t jitterX = (tick % 2 == 0) ? -3 : 3;
    int16_t jitterY = (tick % 3 == 0) ? -2 : 2;
    uint16_t scaredColor = 0x07FF; // Electric Cyan

    // Left eye chevron: > (Apex pointing right/inward)
    faceSprite.drawWideLine(46 + jitterX, 94 + jitterY, 92 + jitterX, 120 + jitterY, 10.0f, scaredColor, TFT_BLACK);
    faceSprite.drawWideLine(92 + jitterX, 120 + jitterY, 46 + jitterX, 146 + jitterY, 10.0f, scaredColor, TFT_BLACK);

    // Right eye chevron: < (Apex pointing left/inward)
    faceSprite.drawWideLine(194 - jitterX, 94 - jitterY, 148 - jitterX, 120 - jitterY, 10.0f, scaredColor, TFT_BLACK);
    faceSprite.drawWideLine(148 - jitterX, 120 - jitterY, 194 - jitterX, 146 - jitterY, 10.0f, scaredColor, TFT_BLACK);

    drawBatteryHud(batteryPct);
    faceSprite.pushSprite(0, 0);
    return;
  }

  // 3. THINKING -> Alternating big/small eyes looking above, switching periodically, NO eyebrows
  if (emotion == EMOTION_THINKING) {
    int16_t breathOffset = (int16_t)(sin(tick * 0.05f) * 2.0f);
    // Switch which eye is raised and looking above every ~2.6s (160 ticks @ 60FPS)
    bool switchPhase = ((tick / 160) % 2) == 0;
    uint16_t thinkColor = 0xFFE0; // Warm Gold

    // Big raised eye (looking above: y=72, h=62), small narrowed eye (y=94, h=36)
    int16_t leftH = isBlinking ? 6 : (switchPhase ? 36 : 62);
    int16_t leftY = isBlinking ? 122 : ((switchPhase ? 94 : 72) + breathOffset);
    int16_t leftRadius = isBlinking ? 3 : (switchPhase ? 12 : 16);

    int16_t rightH = isBlinking ? 6 : (switchPhase ? 62 : 36);
    int16_t rightY = isBlinking ? 122 : ((switchPhase ? 72 : 94) + breathOffset);
    int16_t rightRadius = isBlinking ? 3 : (switchPhase ? 16 : 12);

    faceSprite.fillRoundRect(38, leftY, 62, leftH, leftRadius, thinkColor);
    faceSprite.fillRoundRect(138, rightY, 62, rightH, rightRadius, thinkColor);

    drawBatteryHud(batteryPct);
    faceSprite.pushSprite(0, 0);
    return;
  }

  // 4. HAPPY -> Clean curved anime ^ ^ arches with zero residue
  if (emotion == EMOTION_HAPPY) {
    uint16_t happyColor = 0x07E0; // Emerald Green
    if (isBlinking) {
      faceSprite.drawWideLine(42, 122 + bounceY, 98, 122 + bounceY, 6.0f, happyColor, TFT_BLACK);
      faceSprite.drawWideLine(142, 122 + bounceY, 198, 122 + bounceY, 6.0f, happyColor, TFT_BLACK);
    } else {
      faceSprite.drawArc(70, 114 + bounceY, 30, 20, 110, 250, happyColor, TFT_BLACK, true);
      faceSprite.drawArc(170, 114 + bounceY, 30, 20, 110, 250, happyColor, TFT_BLACK, true);
    }
    drawBatteryHud(batteryPct);
    faceSprite.pushSprite(0, 0);
    return;
  }

  // 5. ANGRY -> Direct geometry with slanted brow, zero exterior residue
  if (emotion == EMOTION_ANGRY) {
    uint16_t angryColor = 0xF800; // Crimson Red
    if (isBlinking) {
      faceSprite.fillRoundRect(38, 122 + bounceY, 64, 6, 3, angryColor);
      faceSprite.fillRoundRect(138, 122 + bounceY, 64, 6, 3, angryColor);
    } else {
      int16_t eyeH = 68;
      int16_t eyeY = 85 + bounceY;
      // Left eye: base rounded rect + angled brow quad (slanted down to center)
      faceSprite.fillRoundRect(38, eyeY + 22, 64, eyeH - 22, 16, angryColor);
      faceSprite.fillTriangle(38, eyeY + 6, 38, eyeY + 24, 102, eyeY + 24, angryColor);
      faceSprite.fillRoundRect(38, eyeY + 4, 24, 20, 8, angryColor); // smooth top-left corner

      // Right eye: base rounded rect + angled brow quad (slanted down to center)
      faceSprite.fillRoundRect(138, eyeY + 22, 64, eyeH - 22, 16, angryColor);
      faceSprite.fillTriangle(202, eyeY + 6, 202, eyeY + 24, 138, eyeY + 24, angryColor);
      faceSprite.fillRoundRect(178, eyeY + 4, 24, 20, 8, angryColor); // smooth top-right corner
    }
    drawBatteryHud(batteryPct);
    faceSprite.pushSprite(0, 0);
    return;
  }

  // 6. IDLE (0) & SLEEP (4)
  uint16_t eyeColor = (emotion == EMOTION_SLEEP) ? 0x4208 : 0x07FF;
  int16_t eyeH = (isBlinking || emotion == EMOTION_SLEEP) ? 6 : 68;
  int16_t eyeY =
      ((isBlinking || emotion == EMOTION_SLEEP) ? 122 : 85) + bounceY;

  faceSprite.fillRoundRect(38, eyeY, 64, eyeH, 16, eyeColor);
  faceSprite.fillRoundRect(138, eyeY, 64, eyeH, 16, eyeColor);

  // Floating "z Z Z" snoring animation when in SLEEP state
  if (emotion == EMOTION_SLEEP) {
    for (int i = 0; i < 3; i++) {
      int16_t step = (tick + i * 25) % 75;
      int16_t zx = 150 + i * 12 + (step / 4);
      int16_t zy = 108 - (step * 50) / 75;
      faceSprite.setTextColor(0x7BEF, TFT_BLACK);
      faceSprite.drawString((i == 0) ? "z" : "Z", zx, zy, (i == 2) ? 4 : 2);
    }
  }

  drawBatteryHud(batteryPct);
  faceSprite.pushSprite(0, 0);
}

// ============================================================================
// CORE 1 TASK: REAL-TIME SAFETY, KINEMATICS & 60FPS TFT EYES
// ============================================================================
void TaskCore1Hardware(void *pvParameters) {
  (void)pvParameters;
  // 1. Initialize Cliff & Front Ultrasonic Sensors
  pinMode(PIN_CLIFF_LEFT_D0, INPUT);
  pinMode(PIN_CLIFF_RIGHT_D0, INPUT);
  pinMode(PIN_USONIC_TRIG, OUTPUT);
  pinMode(PIN_USONIC_ECHO, INPUT);
  digitalWrite(PIN_USONIC_TRIG, LOW);

  // 2. Initialize I2C VL53L0X Laser Rangefinder (Mounted above LCD in head)
  Wire.begin(PIN_TOF_SDA, PIN_TOF_SCL);
  if (tofSensor.begin(0x29, false, &Wire)) {
    tofOnline = true;
    tofSensor.startRangeContinuous(50);
  }

  // 3. Attach Wheels and Neck Pan Servos (Panning only, no tilting)
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  servoWheelL.setPeriodHertz(50);
  servoWheelR.setPeriodHertz(50);
  servoNeckPan.setPeriodHertz(50);

  servoWheelL.attach(PIN_SERVO_WHEEL_L, 500, 2400);
  servoWheelR.attach(PIN_SERVO_WHEEL_R, 500, 2400);
  servoNeckPan.attach(PIN_SERVO_NECK_PAN, 500, 2400);

  // 4. Initialize 1.3" ST7789 Display with PSRAM Sprite
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  faceSprite.setColorDepth(16);
  faceSprite.createSprite(240, 240); // Allocated in 8MB PSRAM

  float smoothPan = NECK_PAN_CENTER;
  RobotCommand activeCmd = {
      0,           0,    NECK_PAN_CENTER, NECK_TILT_CENTER, EMOTION_IDLE,
      MODE_MANUAL, false, false};
  uint32_t frameTick = 0;
  uint32_t bothAirStartMs = 0;
  uint16_t usonicMm = 1200;

  for (;;) {
    frameTick++;

    // A. Receive non-blocking commands from Core 0
    RobotCommand incoming;
    if (xQueueReceive(cmdQueue, &incoming, 0) == pdTRUE) {
      activeCmd = incoming;
    }

    // B. CRITICAL SAFETY: Read TCRT5000 Cliff Sensors FIRST (HIGH = Empty Air /
    // Cliff)
    bool cliffL = digitalRead(PIN_CLIFF_LEFT_D0) == HIGH;
    bool cliffR = digitalRead(PIN_CLIFF_RIGHT_D0) == HIGH;

    // C. Read Head Laser Distance (VL53L0X, above LCD)
    uint16_t distMm = 1200;
    if (tofOnline && tofSensor.isRangeComplete()) {
      distMm = tofSensor.readRangeResult();
    }

    // Read Front Body Ultrasonic Distance (HC-SR04, pinged every 6 frames to keep 60Hz loop fast)
    if (frameTick % 6 == 0) {
      digitalWrite(PIN_USONIC_TRIG, LOW);
      delayMicroseconds(2);
      digitalWrite(PIN_USONIC_TRIG, HIGH);
      delayMicroseconds(10);
      digitalWrite(PIN_USONIC_TRIG, LOW);
      unsigned long durationUs = pulseIn(PIN_USONIC_ECHO, HIGH, 20000); // 20ms timeout (~3.4m)
      if (durationUs > 0) {
        usonicMm = (uint16_t)((durationUs * 10) / 58);
      } else {
        usonicMm = 2000;
      }
    }

    // D. Read Capacitive Touch Brass Standoff (Pin 4)
    bool touched = touchRead(PIN_TOUCH_PAD) > TOUCH_THRESHOLD;
    if (touched) {
      activeCmd.showPairingQr = false;
      activeCmd.emotion = EMOTION_HAPPY;
    }

    if (activeCmd.joyX != 0 || activeCmd.joyY != 0) {
      activeCmd.showPairingQr = false;
    }

    // E. Detect "Grabbed / Held at Height" vs "Table Edge Cliff"
    bool pickedUpAirborne = false;
    if (cliffL && cliffR) {
      if (bothAirStartMs == 0)
        bothAirStartMs = millis();
      if (millis() - bothAirStartMs > PICKUP_DEBOUNCE_MS) {
        pickedUpAirborne = true;
      }
    } else {
      bothAirStartMs = 0;
    }

    int leftPWM = WHEEL_STOP_PWM;
    int rightPWM = WHEEL_STOP_PWM;

    if (pickedUpAirborne) {
      // Robot is grabbed in mid-air -> Stop wheels, trigger DIZZY eyes & head wobble
      activeCmd.showPairingQr = false;
      activeCmd.emotion = EMOTION_DIZZY_PICKUP;
      servoWheelL.write(WHEEL_STOP_PWM);
      servoWheelR.write(WHEEL_STOP_PWM);
      activeCmd.targetPan = 90 + (int8_t)(sin(frameTick * 0.2f) * 22.0f);
    } else if (cliffL || cliffR) {
      // Immediate scared reverse maneuver to prevent falling off desk
      activeCmd.showPairingQr = false;
      activeCmd.emotion = EMOTION_SCARED_CLIFF;
      servoWheelL.write(WHEEL_STOP_PWM - 35);
      servoWheelR.write(WHEEL_STOP_PWM + 35);
      renderProceduralEyes(EMOTION_SCARED_CLIFF, frameTick);
      vTaskDelay(pdMS_TO_TICKS(CLIFF_REVERSE_MS));
      servoWheelL.write(WHEEL_STOP_PWM);
      servoWheelR.write(WHEEL_STOP_PWM);
      activeCmd.joyX = 0;
      activeCmd.joyY = 0;
    } else {
      // Forward collision prevention (considers both body ultrasonic and head laser)
      int8_t safeJoyY = activeCmd.joyY;
      uint16_t forwardObstacleMm = (distMm < usonicMm) ? distMm : usonicMm;
      if (safeJoyY > 0 && forwardObstacleMm < WALL_STOP_DIST_MM) {
        safeJoyY = 0; // Block forward motion into wall
      }

      // Differential drive mixing
      int8_t leftMix = constrain(safeJoyY + activeCmd.joyX, -100, 100);
      int8_t rightMix = constrain(safeJoyY - activeCmd.joyX, -100, 100);

      leftPWM = map(leftMix, -100, 100, 45, 135);
      rightPWM = map(rightMix, -100, 100, 135, 45);

      servoWheelL.write(leftPWM);
      servoWheelR.write(rightPWM);
    }

    // F. Smooth Cubic Easing on Neck Pan (Horizontal Panning only)
    smoothPan = easeValue(smoothPan, activeCmd.targetPan, 0.15f);
    servoNeckPan.write((int)smoothPan);

    // G. Render 60FPS Procedural Eyes or Setup QR Screen
    if (activeCmd.showPairingQr) {
      char apSsid[48];
      char qrText[96];
      snprintf(apSsid, sizeof(apSsid), "%s-Setup", globalBotName);
      snprintf(qrText, sizeof(qrText), "WIFI:S:%s;T:WPA;P:12345678;;", apSsid);
      renderPairingScreen(qrText, apSsid, "12345678", "http://192.168.4.1");
    } else {
      renderProceduralEyes(activeCmd.emotion, frameTick);
    }

    // H. Update Shared Telemetry Struct with Mutex Protection
    if (xSemaphoreTake(telemetryMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
      globalTelemetry.tofDistanceMm = distMm;
      globalTelemetry.usonicDistanceMm = usonicMm;
      globalTelemetry.cliffLeftTriggered = cliffL;
      globalTelemetry.cliffRightTriggered = cliffR;
      globalTelemetry.pickedUpAirborne = pickedUpAirborne;
      globalTelemetry.touchActive = touched;
      globalTelemetry.currentPan = (uint8_t)smoothPan;
      globalTelemetry.currentTilt = 90;
      globalTelemetry.currentEmotion = activeCmd.emotion;
      globalTelemetry.showPairingQr = activeCmd.showPairingQr;
      globalTelemetry.freePsramBytes = ESP.getFreePsram();
      strlcpy(globalTelemetry.botName, globalBotName, sizeof(globalTelemetry.botName));
      xSemaphoreGive(telemetryMutex);
    }

    vTaskDelay(pdMS_TO_TICKS(16)); // ~60Hz Real-Time Hardware Loop
  }
}

// ============================================================================
// CORE 0 TASK: WI-FI CAPTIVE PORTAL, ASYNC WEB SERVER & WEBSOCKET HUB
// ============================================================================
void onWsEvent(AsyncWebSocket * /*server*/, AsyncWebSocketClient * /*client*/,
               AwsEventType type, void * /*arg*/, uint8_t *data, size_t len) {
  if (type == WS_EVT_DATA) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, data, len);
    if (!err) {
      // Optional NVS AI Voice Customization update
      if (doc["voiceName"].is<const char *>()) {
        strlcpy(globalVoiceConfig.voiceName, doc["voiceName"],
                sizeof(globalVoiceConfig.voiceName));
        globalVoiceConfig.pitch = doc["voicePitch"] | 1.0f;
        globalVoiceConfig.speed = doc["voiceSpeed"] | 1.0f;
        globalVoiceConfig.volume = doc["voiceVol"] | 85;
        prefs.putString("vName", globalVoiceConfig.voiceName);
        prefs.putFloat("vPitch", globalVoiceConfig.pitch);
        prefs.putFloat("vSpeed", globalVoiceConfig.speed);
        prefs.putUChar("vVol", globalVoiceConfig.volume);
      }

      if (doc["botName"].is<const char *>()) {
        const char *name = doc["botName"];
        if (strlen(name) > 0 && strlen(name) < 32) {
          strlcpy(globalBotName, name, sizeof(globalBotName));
          prefs.putString("botName", name);
        }
      }

      RobotCommand cmd = {(int8_t)(doc["joyX"] | 0),
                          (int8_t)(doc["joyY"] | 0),
                          (uint8_t)(doc["pan"] | NECK_PAN_CENTER),
                          (uint8_t)(doc["tilt"] | NECK_TILT_CENTER),
                          (EyeEmotion)(doc["emotion"] | 0),
                          (DriveMode)(doc["mode"] | 0),
                          (bool)(doc["macro"] | false),
                          (bool)(doc["showPairingQr"] | false)};
      xQueueSend(cmdQueue, &cmd, 0);
    }
  }
}

void TaskCore0Network(void *pvParameters) {
  (void)pvParameters;
  LittleFS.begin(true);
  prefs.begin("penbot", false);

  String savedBotName = prefs.getString("botName", "PenBot");
  strlcpy(globalBotName, savedBotName.c_str(), sizeof(globalBotName));
  strlcpy(globalTelemetry.botName, globalBotName, sizeof(globalTelemetry.botName));

  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");

  bool isAp = false;
  if (ssid.length() > 0) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());
    uint8_t attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      vTaskDelay(pdMS_TO_TICKS(500));
      attempts++;
    }
  }

  if (WiFi.status() != WL_CONNECTED) {
    WiFi.mode(WIFI_AP);
    String apSsid = String(globalBotName) + "-Setup";
    WiFi.softAP(apSsid.c_str(), "12345678");
    isAp = true;
    dnsServer.start(53, "*", WiFi.softAPIP());

    // Display QR Code on ST7789 screen for quick phone onboarding
    RobotCommand cmd = {0, 0, NECK_PAN_CENTER, NECK_TILT_CENTER,
                        EMOTION_IDLE, MODE_MANUAL, false, true};
    xQueueSend(cmdQueue, &cmd, 0);
  }

  // --- Bot Renaming & Information Endpoints ---
  server.on("/api/bot/info", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["name"] = globalBotName;
    doc["version"] = "1.2.0";
    doc["hardware"] = "ESP32-S3 N16R8";
    String resp;
    serializeJson(doc, resp);
    request->send(200, "application/json", resp);
  });

  server.on(
      "/api/bot/rename", HTTP_POST, [](AsyncWebServerRequest * /*request*/) {},
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t /*index*/, size_t /*total*/) {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, data, len);
        if (!err && doc["name"].is<const char *>()) {
          const char *newName = doc["name"];
          if (strlen(newName) > 0 && strlen(newName) < 32) {
            strlcpy(globalBotName, newName, sizeof(globalBotName));
            prefs.putString("botName", newName);
            if (xSemaphoreTake(telemetryMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
              strlcpy(globalTelemetry.botName, newName, sizeof(globalTelemetry.botName));
              xSemaphoreGive(telemetryMutex);
            }
            request->send(200, "application/json", "{\"success\":true,\"name\":\"" + String(newName) + "\"}");
            return;
          }
        }
        request->send(400, "application/json", "{\"error\":\"Invalid bot name\"}");
      });

  // --- OTA Firmware Flash Update REST API ---
  server.on(
      "/api/firmware/update", HTTP_POST,
      [](AsyncWebServerRequest *request) {
        bool shouldReboot = !Update.hasError();
        AsyncWebServerResponse *response = request->beginResponse(
            200, "application/json",
            shouldReboot ? "{\"success\":true,\"message\":\"Firmware update complete. Rebooting robot brain...\"}"
                         : "{\"error\":\"Firmware update failed.\"}");
        response->addHeader("Connection", "close");
        request->send(response);
        if (shouldReboot) {
          vTaskDelay(pdMS_TO_TICKS(1000));
          ESP.restart();
        }
      },
      [](AsyncWebServerRequest * /*request*/, String /*filename*/, size_t index,
         uint8_t *data, size_t len, bool final) {
        if (!index) {
          RobotCommand cmd = {0, 0, NECK_PAN_CENTER, NECK_TILT_CENTER,
                              EMOTION_THINKING, MODE_MANUAL, false, false};
          xQueueSend(cmdQueue, &cmd, 0);

          if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
            Update.printError(Serial);
          }
        }
        if (!Update.hasError()) {
          if (Update.write(data, len) != len) {
            Update.printError(Serial);
          }
        }
        if (final) {
          if (!Update.end(true)) {
            Update.printError(Serial);
          }
        }
      });

  // --- REST API Endpoints for Device Pairing & Network Setup ---
  server.on("/api/wifi/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    bool connected = (WiFi.status() == WL_CONNECTED);
    doc["connected"] = connected;
    doc["mode"] = (WiFi.getMode() == WIFI_STA && connected) ? "STA" : "AP";
    String apSsid = String(globalBotName) + "-Setup";
    doc["ssid"] = connected ? WiFi.SSID() : apSsid;
    doc["ip"] = connected ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
    doc["rssi"] = connected ? WiFi.RSSI() : 0;
    doc["mac"] = WiFi.macAddress();
    doc["botName"] = globalBotName;
    String resp;
    serializeJson(doc, resp);
    request->send(200, "application/json", resp);
  });

  server.on("/api/wifi/scan", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (WiFi.getMode() == WIFI_AP) {
      WiFi.mode(WIFI_AP_STA);
    }
    int n = WiFi.scanNetworks();
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < n; i++) {
      JsonObject obj = arr.add<JsonObject>();
      obj["ssid"] = WiFi.SSID(i);
      obj["rssi"] = WiFi.RSSI(i);
      obj["secure"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    }
    WiFi.scanDelete();
    String resp;
    serializeJson(doc, resp);
    request->send(200, "application/json", resp);
  });

  server.on(
      "/api/wifi/connect", HTTP_POST, [](AsyncWebServerRequest * /*request*/) {},
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t /*index*/, size_t /*total*/) {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, data, len);
        if (!err && doc["ssid"].is<const char *>()) {
          const char *newSsid = doc["ssid"];
          const char *newPass = doc["pass"] | "";
          prefs.putString("ssid", newSsid);
          prefs.putString("pass", newPass);

          request->send(200, "application/json",
                        "{\"success\":true,\"message\":\"Credentials saved. Connecting...\"}");

          // Switch screen from QR to Thinking while negotiating DHCP
          RobotCommand cmd = {0, 0, NECK_PAN_CENTER, NECK_TILT_CENTER,
                              EMOTION_THINKING, MODE_MANUAL, false, false};
          xQueueSend(cmdQueue, &cmd, 0);

          WiFi.mode(WIFI_AP_STA);
          WiFi.begin(newSsid, newPass);
        } else {
          request->send(400, "application/json",
                        "{\"error\":\"Invalid payload\"}");
        }
      });

  server.on("/api/wifi/reset", HTTP_POST, [](AsyncWebServerRequest *request) {
    prefs.remove("ssid");
    prefs.remove("pass");
    request->send(200, "application/json",
                  "{\"success\":true,\"message\":\"Wi-Fi reset to AP mode.\"}");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP);
    String apSsid = String(globalBotName) + "-Setup";
    WiFi.softAP(apSsid.c_str(), "12345678");
    dnsServer.start(53, "*", WiFi.softAPIP());
    RobotCommand cmd = {0, 0, NECK_PAN_CENTER, NECK_TILT_CENTER,
                        EMOTION_IDLE, MODE_MANUAL, false, true};
    xQueueSend(cmdQueue, &cmd, 0);
  });

  // Captive Portal Redirection
  server.onNotFound([](AsyncWebServerRequest *request) {
    if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) {
      request->redirect("http://192.168.4.1/");
    } else {
      request->send(404, "text/plain", "Not Found");
    }
  });

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  // Serve Embedded Dashboard from LittleFS
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  server.begin();

  for (;;) {
    ws.cleanupClients();

    if (isAp) {
      dnsServer.processNextRequest();
    }

    // Broadcast 10Hz Telemetry JSON to connected Web Dashboard clients
    if (ws.count() > 0 &&
        xSemaphoreTake(telemetryMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      JsonDocument doc;
      doc["tof"] = globalTelemetry.tofDistanceMm;
      doc["usonic"] = globalTelemetry.usonicDistanceMm;
      doc["cliffL"] = globalTelemetry.cliffLeftTriggered;
      doc["cliffR"] = globalTelemetry.cliffRightTriggered;
      doc["touch"] = globalTelemetry.touchActive;
      doc["pan"] = globalTelemetry.currentPan;
      doc["tilt"] = globalTelemetry.currentTilt;
      doc["emotion"] = globalTelemetry.currentEmotion;
      doc["psram"] = globalTelemetry.freePsramBytes;
      doc["wifiConnected"] = (WiFi.status() == WL_CONNECTED);
      doc["wifiIp"] = (WiFi.status() == WL_CONNECTED)
                          ? WiFi.localIP().toString()
                          : WiFi.softAPIP().toString();
      String apSsid = String(globalBotName) + "-Setup";
      doc["wifiSsid"] =
          (WiFi.status() == WL_CONNECTED) ? WiFi.SSID() : apSsid;
      doc["isApMode"] = isAp;
      doc["showPairingQr"] = globalTelemetry.showPairingQr;
      doc["botName"] = globalBotName;

      String payload;
      serializeJson(doc, payload);
      ws.textAll(payload);
      xSemaphoreGive(telemetryMutex);
    }

    vTaskDelay(pdMS_TO_TICKS(100)); // 10Hz Telemetry Tick
  }
}

void setup() {
  Serial.begin(115200);
  cmdQueue = xQueueCreate(10, sizeof(RobotCommand));
  telemetryMutex = xSemaphoreCreateMutex();

  // Pin Core 0 to Network/AI and Core 1 to Real-time Sensors/Servos/TFT
  xTaskCreatePinnedToCore(TaskCore0Network, "Core0_Net", 8192, NULL, 1, NULL,
                          0);
  xTaskCreatePinnedToCore(TaskCore1Hardware, "Core1_HW", 8192, NULL, 2, NULL,
                          1);
}

void loop() {
  vTaskDelete(NULL); // Managed entirely by FreeRTOS Tasks
}
