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

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <ESP_I2S.h>

#include "config.h"
#include "qrcode.h"
#include "state.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef TFT_BLACK
#define TFT_BLACK 0x0000
#endif
#ifndef TFT_WHITE
#define TFT_WHITE 0xFFFF
#endif

#define MC_DATUM 1
#define TC_DATUM 2

class NativeST7789Display : public Adafruit_GFX {
public:
  uint16_t* buffer = nullptr;
  uint8_t textDatum = 0;
  SPIClass* customSpi = nullptr;
  Adafruit_ST7789* driver = nullptr;

  NativeST7789Display() : Adafruit_GFX(240, 240) {}

  void init() {
    pinMode(PIN_TFT_BLK, OUTPUT);
    digitalWrite(PIN_TFT_BLK, HIGH); // Drive Backlight ON (if wired to GPIO 10)

    pinMode(PIN_TFT_RST, OUTPUT);
    digitalWrite(PIN_TFT_RST, HIGH);
    delay(10);
    digitalWrite(PIN_TFT_RST, LOW);
    delay(20);
    digitalWrite(PIN_TFT_RST, HIGH);
    delay(120);

    customSpi = new SPIClass(FSPI);
    customSpi->begin(PIN_TFT_SCK, -1, PIN_TFT_MOSI, -1);
    driver = new Adafruit_ST7789(customSpi, -1, PIN_TFT_DC, PIN_TFT_RST);
    driver->init(240, 240, SPI_MODE3);
    driver->setSPISpeed(20000000);
    driver->setRotation(2); // Physical faceplate mounting orientation
    driver->fillScreen(ST77XX_BLACK);

    if (psramFound()) {
      buffer = (uint16_t*)ps_malloc(240 * 240 * sizeof(uint16_t));
    }
    if (!buffer) {
      buffer = (uint16_t*)malloc(240 * 240 * sizeof(uint16_t));
    }
    if (buffer) {
      memset(buffer, 0, 240 * 240 * sizeof(uint16_t));
    }
  }

  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if (x < 0 || x >= 240 || y < 0 || y >= 240 || !buffer) return;
    buffer[y * 240 + x] = color;
  }

  void fillSprite(uint16_t color) {
    if (!buffer) return;
    for (int i = 0; i < 240 * 240; i++) buffer[i] = color;
  }

  void fillScreen(uint16_t color) override {
    fillSprite(color);
  }

  void setTextDatum(uint8_t d) {
    textDatum = d;
  }

  void drawString(const char* str, int16_t x, int16_t y, uint8_t size = 1) {
    int16_t x1, y1;
    uint16_t w, h;
    setTextSize(size);
    getTextBounds(str, 0, 0, &x1, &y1, &w, &h);
    int16_t px = x;
    int16_t py = y;
    if (textDatum == MC_DATUM) {
      px = x - w / 2;
      py = y - h / 2;
    } else if (textDatum == TC_DATUM) {
      px = x - w / 2;
      py = y;
    }
    setCursor(px, py);
    print(str);
  }

  void drawWideLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, float w, uint16_t color) {
    int thickness = (int)w;
    if (thickness < 1) thickness = 1;
    int half = thickness / 2;
    for (int i = -half; i <= half; i++) {
      drawLine(x0 + i, y0, x1 + i, y1, color);
      drawLine(x0, y0 + i, x1, y1 + i, color);
    }
  }

  void drawArc(int16_t cx, int16_t cy, int16_t r0, int16_t r1, float a0, float a1, uint16_t color) {
    float rad0 = a0 * 0.0174532925f;
    float rad1 = a1 * 0.0174532925f;
    int maxR = max(r0, r1);
    float step = (maxR > 0) ? (1.0f / (float)maxR) : 0.05f;
    for (float a = rad0; a <= rad1; a += step) {
      float ca = cos(a);
      float sa = sin(a);
      for (int16_t r = min(r0, r1); r <= max(r0, r1); r++) {
        drawPixel(cx + (int16_t)(ca * r), cy + (int16_t)(sa * r), color);
      }
    }
  }

  void pushSprite(int32_t = 0, int32_t = 0) {
    if (!driver || !buffer) return;
    driver->drawRGBBitmap(0, 0, buffer, 240, 240);
  }
};

// Global RTOS synchronization primitives & customizable bot profile
QueueHandle_t cmdQueue;
SemaphoreHandle_t telemetryMutex;

// MAX98357A via Arduino ESP_I2S (SD@3V3 = LEFT channel)
static I2SClass ampI2s;
static bool ampReady = false;

static bool ampStart() {
  if (ampReady) return true;

  ampI2s.setPins(PIN_AMP_BCLK, PIN_AMP_LRC, PIN_AMP_DIN);
  // 16 kHz STD stereo is the most reliable first-sound path for MAX98357A
  if (!ampI2s.begin(I2S_MODE_STD, 16000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    Serial.printf("[Amp] ESP_I2S begin failed, err=%d\n", ampI2s.lastError());
    return false;
  }

  ampReady = true;
  Serial.println("[Amp] MAX98357A ESP_I2S ready (16kHz stereo, L+R filled).");
  return true;
}

static void ampPlayTone(int freqHz, int durationMs, int16_t amplitude) {
  if (!ampReady) return;

  const uint32_t sampleRate = 16000;
  const int framesPerBuf = 256;
  int16_t buffer[512];
  float phase = 0.0f;
  const float step = 2.0f * (float)M_PI * (float)freqHz / (float)sampleRate;
  unsigned long startT = millis();

  while ((int)(millis() - startT) < durationMs) {
    for (int i = 0; i < framesPerBuf; i++) {
      int16_t sample = (int16_t)(sinf(phase) * (float)amplitude);
      buffer[i * 2] = sample;      // LEFT  (SD@3V3)
      buffer[i * 2 + 1] = sample;  // RIGHT
      phase += step;
      if (phase >= 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;
    }
    ampI2s.write(buffer, sizeof(buffer));
  }
}

static void ampBootBeep() {
  if (!ampStart()) {
    Serial.println("[Amp] Boot beep skipped — I2S init failed.");
    return;
  }
  // Long loud chime — hard to miss if the amp/speaker path works
  ampPlayTone(440, 600, 25000);
  vTaskDelay(pdMS_TO_TICKS(100));
  ampPlayTone(880, 600, 25000);
  vTaskDelay(pdMS_TO_TICKS(100));
  ampPlayTone(1320, 500, 25000);
  Serial.println("[Amp] Boot beep played (440/880/1320).");
}
char globalBotName[32] = "PenBot";
volatile bool networkReady = false;

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
NativeST7789Display faceSprite;
NativeST7789Display& tft = faceSprite;
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
    faceSprite.drawWideLine(46 + jitterX, 94 + jitterY, 92 + jitterX, 120 + jitterY, 10.0f, scaredColor);
    faceSprite.drawWideLine(92 + jitterX, 120 + jitterY, 46 + jitterX, 146 + jitterY, 10.0f, scaredColor);

    // Right eye chevron: < (Apex pointing left/inward)
    faceSprite.drawWideLine(194 - jitterX, 94 - jitterY, 148 - jitterX, 120 - jitterY, 10.0f, scaredColor);
    faceSprite.drawWideLine(148 - jitterX, 120 - jitterY, 194 - jitterX, 146 - jitterY, 10.0f, scaredColor);

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
      faceSprite.drawWideLine(42, 122 + bounceY, 98, 122 + bounceY, 6.0f, happyColor);
      faceSprite.drawWideLine(142, 122 + bounceY, 198, 122 + bounceY, 6.0f, happyColor);
    } else {
      faceSprite.drawArc(70, 114 + bounceY, 30, 20, 110, 250, happyColor);
      faceSprite.drawArc(170, 114 + bounceY, 30, 20, 110, 250, happyColor);
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
  Serial.println("[Core 1] Starting Hardware Init..."); Serial.flush();

  // 1. Initialize Cliff & Front Ultrasonic Sensors
  pinMode(PIN_CLIFF_LEFT_D0, INPUT);
  pinMode(PIN_CLIFF_RIGHT_D0, INPUT);
  pinMode(PIN_USONIC_TRIG, OUTPUT);
  pinMode(PIN_USONIC_ECHO, INPUT);
  digitalWrite(PIN_USONIC_TRIG, LOW);

  // 2. Initialize I2C VL53L0X Laser Rangefinder (Mounted above LCD in head)
  Wire.begin(PIN_TOF_SDA, PIN_TOF_SCL);
  Wire.setTimeOut(25); // Freeze-proof timeout
  if (tofSensor.begin(0x29, false, &Wire)) {
    tofOnline = true;
    tofSensor.startRangeContinuous(50);
    Serial.println("[Core 1] VL53L0X Online."); Serial.flush();
  } else {
    Serial.println("[Core 1] VL53L0X Offline."); Serial.flush();
  }

  // 3. Attach Neck Pan Servo & Initialize Wheels to Drive-on-Demand (0V LOW)
  ESP32PWM::allocateTimer(0);
  servoNeckPan.setPeriodHertz(50);
  servoNeckPan.attach(PIN_SERVO_NECK_PAN, 500, 2400);

  // Native 14-bit 50Hz LEDC hardware timers for wheels (bypasses library 500us clamp)
  ledcAttach(PIN_SERVO_WHEEL_L, 50, 14);
  ledcAttach(PIN_SERVO_WHEEL_R, 50, 14);
  ledcWrite(PIN_SERVO_WHEEL_L, 0);
  ledcWrite(PIN_SERVO_WHEEL_R, 0);
  Serial.println("[Core 1] Servos ready in Drive-on-Demand mode (0V cutoff)."); Serial.flush();

  float smoothPan = NECK_PAN_CENTER;
  RobotCommand activeCmd = {
      0,           0,    NECK_PAN_CENTER, NECK_TILT_CENTER, EMOTION_IDLE,
      MODE_MANUAL, false, false};
  uint32_t frameTick = 0;
  uint32_t bothAirStartMs = 0;
  uint16_t usonicMm = 1200;

  auto setWheelPulse = [](uint8_t pin, int us) {
    if (us <= 0) {
      ledcWrite(pin, 0); // Solid 0V cutoff (Drive-on-Demand dead stop)
    } else {
      uint32_t duty = (uint32_t)((us * 16384ULL) / 20000ULL);
      ledcWrite(pin, duty);
    }
  };

  // Calibrated Kinematics Helper: Native LEDC Drive-on-Demand for Glued SG90 Potentiometers
  auto setWheels = [&](int8_t leftMix, int8_t rightMix) {
    if (leftMix == 0 && rightMix == 0) {
      setWheelPulse(PIN_SERVO_WHEEL_L, 0);
      setWheelPulse(PIN_SERVO_WHEEL_R, 0);
    } else {
      // Left Wheel: 1000 neutral, 1500 forward, 450 reverse
      int leftUs = 1000;
      if (leftMix > 0) leftUs = map(leftMix, 0, 100, 1000, 1500);
      else if (leftMix < 0) leftUs = map(leftMix, -100, 0, 450, 1000);

      // Right Wheel: 800 neutral, 500 forward, 1200 reverse
      int rightUs = 800;
      if (rightMix > 0) rightUs = map(rightMix, 0, 100, 800, 500);
      else if (rightMix < 0) rightUs = map(rightMix, -100, 0, 1200, 800);

      setWheelPulse(PIN_SERVO_WHEEL_L, leftUs);
      setWheelPulse(PIN_SERVO_WHEEL_R, rightUs);
    }
  };

  for (;;) {
    frameTick++;

    // A. Receive non-blocking commands from Core 0
    RobotCommand incoming;
    if (xQueueReceive(cmdQueue, &incoming, 0) == pdTRUE) {
      activeCmd = incoming;
    }

    // B. Read TCRT5000 Cliff Sensors FIRST (HIGH = Empty Air / Cliff)
    bool cliffL = digitalRead(PIN_CLIFF_LEFT_D0) == HIGH;
    bool cliffR = digitalRead(PIN_CLIFF_RIGHT_D0) == HIGH;

    // C. Read Head Laser Distance (VL53L0X, above LCD)
    uint16_t distMm = 1200;
    if (tofOnline && tofSensor.isRangeComplete()) {
      distMm = tofSensor.readRangeResult();
    }

    // Read Front Body Ultrasonic Distance (HC-SR04, pinged every 6 frames)
    if (frameTick % 6 == 0) {
      digitalWrite(PIN_USONIC_TRIG, LOW);
      delayMicroseconds(2);
      digitalWrite(PIN_USONIC_TRIG, HIGH);
      delayMicroseconds(10);
      digitalWrite(PIN_USONIC_TRIG, LOW);
      unsigned long durationUs = pulseIn(PIN_USONIC_ECHO, HIGH, 20000); // 20ms timeout
      if (durationUs > 0) {
        usonicMm = (uint16_t)((durationUs * 10) / 58);
      } else {
        usonicMm = 2000;
      }
    }

    // D. Capacitive Touch Brass Standoff (Pin 4)
    bool touched = false;
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

    if (pickedUpAirborne) {
      // Robot is grabbed in mid-air -> Stop wheels, trigger DIZZY eyes & head wobble
      activeCmd.showPairingQr = false;
      activeCmd.emotion = EMOTION_DIZZY_PICKUP;
      setWheels(0, 0);
      activeCmd.targetPan = 90 + (int8_t)(sin(frameTick * 0.2f) * 22.0f);
    } else if (cliffL || cliffR) {
      // Immediate scared reverse maneuver to prevent falling off desk
      activeCmd.showPairingQr = false;
      activeCmd.emotion = EMOTION_SCARED_CLIFF;
      setWheels(-100, -100); // Calibrated reverse on both wheels
      renderProceduralEyes(EMOTION_SCARED_CLIFF, frameTick);
      vTaskDelay(pdMS_TO_TICKS(CLIFF_REVERSE_MS));
      setWheels(0, 0); // Complete stop with 0V cutoff
      activeCmd.joyX = 0;
      activeCmd.joyY = 0;
    } else {
      // Forward collision prevention (considers both body ultrasonic and head laser)
      int8_t safeJoyY = activeCmd.joyY;
      uint16_t forwardObstacleMm = (distMm < usonicMm) ? distMm : usonicMm;
      if (safeJoyY > 0 && forwardObstacleMm < WALL_STOP_DIST_MM) {
        safeJoyY = 0; // Block forward motion into obstacle
      }

      // Differential drive mixing
      int8_t leftMix = constrain(safeJoyY + activeCmd.joyX, -100, 100);
      int8_t rightMix = constrain(safeJoyY - activeCmd.joyX, -100, 100);
      setWheels(leftMix, rightMix);
    }

    // F. Smooth Cubic Easing on Neck Pan (Horizontal Panning only)
    smoothPan = easeValue(smoothPan, activeCmd.targetPan, 0.15f);
    servoNeckPan.write((int)smoothPan);

    // G. Render 60FPS Procedural Eyes or Pairing Screen
    if (activeCmd.showPairingQr) {
      char qrTarget[64];
      snprintf(qrTarget, sizeof(qrTarget), "WIFI:S:%s;T:WPA;P:12345678;;", globalTelemetry.wifiSsid);
      renderPairingScreen(qrTarget, globalTelemetry.wifiSsid, "12345678", "http://192.168.4.1", 88);
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
  Serial.println("[Core 0] TaskCore0Network started."); Serial.flush();

  // Mouth amp lives on Core 0 (I2S TX). SD@3V3 => LEFT channel must carry PCM.
  ampBootBeep();

  LittleFS.begin(true);
  Serial.println("[Core 0] LittleFS mounted."); Serial.flush();
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
    Serial.println("[Core 0] Setting up WiFi AP..."); Serial.flush();
    WiFi.mode(WIFI_AP);
    String apSsid = String(globalBotName) + "-Setup";
    WiFi.softAP(apSsid.c_str(), "12345678");
    Serial.println("[Core 0] WiFi AP active: " + apSsid); Serial.flush();
    isAp = true;
    dnsServer.start(53, "*", WiFi.softAPIP());
    Serial.println("[Core 0] DNS Server started."); Serial.flush();

    // Display QR Code on ST7789 screen for quick phone onboarding
    RobotCommand cmd = {0, 0, NECK_PAN_CENTER, NECK_TILT_CENTER,
                        EMOTION_IDLE, MODE_MANUAL, false, true};
    xQueueSend(cmdQueue, &cmd, 0);
  }

  Serial.println("[Core 0] Registering HTTP routes..."); Serial.flush();

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
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");

  server.on("/api/wifi/scan", HTTP_OPTIONS, [](AsyncWebServerRequest *request) {
    request->send(200);
  });
  server.on("/api/wifi/connect", HTTP_OPTIONS, [](AsyncWebServerRequest *request) {
    request->send(200);
  });
  server.on("/api/bot/rename", HTTP_OPTIONS, [](AsyncWebServerRequest *request) {
    request->send(200);
  });

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

  // Direct route aliases for mobile and desktop interfaces
  server.on("/mobile", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/mobile/index.html", "text/html");
  });
  server.on("/desktop", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/desktop/index.html", "text/html");
  });

  // Serve Embedded Dashboard from LittleFS
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  server.begin();
  networkReady = true;
  Serial.println("[Core 0] HTTP WebServer started!"); Serial.flush();

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

  // Give serial terminal time to attach
  for (int i = 0; i < 4; i++) {
    delay(400);
    Serial.print(".");
    Serial.flush();
  }
  Serial.println(" [READY]");

  Serial.println("\n========================================================");
  Serial.println("   CYBER-CUBE MINI (PEN-AI-BOT) — MONOLITH FIRMWARE    ");
  Serial.println("========================================================");
  Serial.printf("Flash: %uMB | PSRAM: %uMB (Free: %uKB)\n",
                (unsigned int)(ESP.getFlashChipSize() / (1024 * 1024)),
                (unsigned int)(ESP.getPsramSize() / (1024 * 1024)),
                (unsigned int)(ESP.getFreePsram() / 1024));
  Serial.println("Core 0: Network / Web OS / AI Cloud Bridge / MAX98357A Amp");
  Serial.println("Core 1: 60FPS Eyes / Kinematics / Safety Sensors");
  Serial.printf("Amp pins: BCLK=%d LRC=%d DIN=%d (I2S_NUM_%d, SD@3V3=LEFT)\n",
                PIN_AMP_BCLK, PIN_AMP_LRC, PIN_AMP_DIN, I2S_AMP_PORT_NUM);
  Serial.println("========================================================\n");
  Serial.flush();

  // 1. Initialize ST7789 IPS Display cleanly in setup()
  Serial.println("[Setup] Initializing ST7789 Display..."); Serial.flush();
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  renderProceduralEyes(EMOTION_IDLE, 0);
  Serial.println("[Setup] ST7789 Display initialized & initial face rendered!"); Serial.flush();

  cmdQueue = xQueueCreate(10, sizeof(RobotCommand));
  telemetryMutex = xSemaphoreCreateMutex();

  // Pin Core 0 to Network/AI and Core 1 to Real-time Sensors/Servos/TFT
  xTaskCreatePinnedToCore(TaskCore0Network, "Core0_Net", 8192, NULL, 1, NULL,
                          0);
  xTaskCreatePinnedToCore(TaskCore1Hardware, "Core1_HW", 8192, NULL, 2, NULL,
                          1);
}

void loop() {
  static uint32_t lastPrint = 0;
  if (millis() - lastPrint >= 1500) {
    lastPrint = millis();
    if (xSemaphoreTake(telemetryMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      Serial.printf("[PEN-BOT] Alive | WiFi: %s | IP: %s | Pan: %d | Emotion: %d | PSRAM Free: %u KB\n",
                    globalTelemetry.wifiSsid[0] ? globalTelemetry.wifiSsid : (WiFi.status() == WL_CONNECTED ? WiFi.SSID().c_str() : "PenBot-Setup"),
                    (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : WiFi.softAPIP().toString().c_str()),
                    globalTelemetry.currentPan,
                    (int)globalTelemetry.currentEmotion,
                    (unsigned int)(ESP.getFreePsram() / 1024));
      Serial.flush();
      xSemaphoreGive(telemetryMutex);
    }
  }
  vTaskDelay(pdMS_TO_TICKS(100)); // Yield to Idle task and feed FreeRTOS watchdog
}
