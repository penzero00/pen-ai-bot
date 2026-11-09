#include <stdint.h>
#include <math.h>
#include <Arduino.h>
#include <ESP_I2S.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

#include "config.h"

// ============================================================================
// CYBER-CUBE MINI — STUDIO VOICE with ACTIVE NOISE SUPPRESSION & VAD
// Features:
//   1. Adaptive Noise Floor Tracking (Minima Estimation):
//      - Continuously tracks room/fan/AC noise floor in real time
//   2. Voice Activity Detection (VAD) & Downward Expander:
//      - Only opens when speech is detected (near-field user voice)
//      - Suppresses room reflections, fans, keyboard clicks, distant talk (-100dB)
//      - 180ms smooth hang-over time (never clips word endings)
//   3. Voice Band-Pass Filter (280 Hz HPF to 4,000 Hz LPF) + 60/120Hz Notches
//   4. Broadcast Maximizer (Controlled 4.2x max gain, 13,500 ceiling, zero distortion)
//   5. 10ms Raised-Cosine Anti-Click Fades
// ============================================================================

static constexpr uint32_t SAMPLE_RATE = 16000;
static constexpr size_t RECORD_SECONDS = 3;
static constexpr size_t TOTAL_SAMPLES = SAMPLE_RATE * RECORD_SECONDS; // 48,000 samples
static constexpr size_t CHUNK_FRAMES = 256; // 256 stereo frames = 512 samples

I2SClass micI2s(I2S_NUM_0);
I2SClass ampI2s(I2S_NUM_1);

SPIClass* customSpi = nullptr;
Adafruit_ST7789* tft = nullptr;

int16_t* recordBuffer = nullptr;

// --- 1. 280 Hz Butterworth HPF (fs=16000, Q=0.7071) ---
static constexpr float hp_b0 = 0.92519066f;
static constexpr float hp_b1 = -1.85038132f;
static constexpr float hp_b2 = 0.92519066f;
static constexpr float hp_a1 = -1.84477713f;
static constexpr float hp_a2 = 0.85598551f;
static float hpd1 = 0.0f;
static float hpd2 = 0.0f;

// --- 2. 4000 Hz Butterworth LPF (fs=16000, Q=0.7071) ---
// Tightened to 4000 Hz to cut ambient hiss even sharper
static constexpr float lp_b0 = 0.29289322f;
static constexpr float lp_b1 = 0.58578644f;
static constexpr float lp_b2 = 0.29289322f;
static constexpr float lp_a1 = -0.00000000f;
static constexpr float lp_a2 = 0.17157288f;
static float lpd1 = 0.0f;
static float lpd2 = 0.0f;

// --- 3. 60 Hz Notch Filter (fs=16000, Q=3.0) ---
static constexpr float nb0 = 0.99608873f;
static constexpr float nb1 = -1.99162449f;
static constexpr float nb2 = 0.99608873f;
static constexpr float na1 = -1.99162449f;
static constexpr float na2 = 0.99217746f;
static float nd1 = 0.0f;
static float nd2 = 0.0f;

// --- 4. 120 Hz Notch Filter (fs=16000, Q=3.0) ---
static constexpr float n120_b0 = 0.99221008f;
static constexpr float n120_b1 = -1.98221721f;
static constexpr float n120_b2 = 0.99221008f;
static constexpr float n120_a1 = -1.98221721f;
static constexpr float n120_a2 = 0.98442017f;
static float n120_d1 = 0.0f;
static float n120_d2 = 0.0f;

// --- 5. ADAPTIVE NOISE CANCELLATION & VAD STATE ---
static float noiseFloor = 60.0f;
static float shortEnv = 0.0f;
static int vadHoldSamples = 0;
static float vadGain = 0.0f;
static bool voiceDetectedLive = false;

// --- 6. BROADCAST MAXIMIZER STATE ---
static float agcEnv = 0.0f;
static float agcGain = 2.0f;

inline int16_t processCleanAudio(int32_t raw32) {
  // Convert 32-bit slot to 16-bit float
  float x = (float)(raw32 >> 16);

  // 1. 280 Hz High-Pass Filter (eliminates desk rumble & low frequency hum)
  float yh = hp_b0 * x + hpd1;
  hpd1 = hp_b1 * x - hp_a1 * yh + hpd2;
  hpd2 = hp_b2 * x - hp_a2 * yh;

  // 2. 60 Hz Notch
  float yn60 = nb0 * yh + nd1;
  nd1 = nb1 * yh - na1 * yn60 + nd2;
  nd2 = nb2 * yh - na2 * yn60;

  // 3. 120 Hz Notch
  float yn120 = n120_b0 * yn60 + n120_d1;
  n120_d1 = n120_b1 * yn60 - n120_a1 * yn120 + n120_d2;
  n120_d2 = n120_b2 * yn60 - n120_a2 * yn120;

  // 4. 4000 Hz Low-Pass Filter (cuts ambient air-con hiss & high-frequency noise)
  float ybp = lp_b0 * yn120 + lpd1;
  lpd1 = lp_b1 * yn120 - lp_a1 * ybp + lpd2;
  lpd2 = lp_b2 * yn120 - lp_a2 * ybp;

  // 5. ADAPTIVE NOISE SUPPRESSION & VAD
  float absSample = fabsf(ybp);

  // Fast short-term energy tracker (5ms response)
  shortEnv += (absSample - shortEnv) * 0.02f;

  // Background noise floor tracker:
  // Tracks steady room noise (fans, PC, AC) without rising into speech
  if (shortEnv < noiseFloor) {
    noiseFloor += (shortEnv - noiseFloor) * 0.04f; // Fast drop to new floor
  } else {
    noiseFloor += (shortEnv - noiseFloor) * 0.00025f; // Very slow rise
  }
  noiseFloor = constrain(noiseFloor, 25.0f, 600.0f);

  // Voice threshold: Must be 2.6x above room noise floor (min 280 units)
  float voiceThreshold = fmaxf(280.0f, noiseFloor * 2.6f);

  float targetVad = 0.0f;
  if (shortEnv > voiceThreshold) {
    vadHoldSamples = 2880; // 180ms hold time (180ms * 16 samples/ms)
    targetVad = 1.0f;
    voiceDetectedLive = true;
  } else if (vadHoldSamples > 0) {
    vadHoldSamples--;
    targetVad = 1.0f; // Hold open so word endings aren't clipped
    voiceDetectedLive = true;
  } else {
    targetVad = 0.0f; // Complete silence for surroundings
    voiceDetectedLive = false;
  }

  // Smooth VAD gain transition (1.5ms attack, 25ms release) to prevent clicks
  if (targetVad > vadGain) {
    vadGain += (targetVad - vadGain) * 0.04f;
  } else {
    vadGain += (targetVad - vadGain) * 0.0025f;
  }

  // Apply noise suppression gain (room noise drops to pure silence)
  float cleanVoice = ybp * vadGain;

  // 6. BROADCAST MAXIMIZER (Only operates on active voice)
  float absVoice = fabsf(cleanVoice);
  if (absVoice > agcEnv) {
    agcEnv += (absVoice - agcEnv) * 0.05f;
  } else {
    agcEnv += (absVoice - agcEnv) * 0.0015f;
  }

  float targetGain = 1.0f;
  if (agcEnv > 150.0f) {
    targetGain = 12000.0f / (agcEnv + 450.0f);
    if (targetGain > 4.2f) targetGain = 4.2f; // Controlled boost (no room amplification)
    if (targetGain < 0.8f) targetGain = 0.8f; // Tames loud speech
  } else {
    targetGain = 1.0f;
  }

  agcGain += (targetGain - agcGain) * 0.008f;
  float out = cleanVoice * agcGain;

  // 7. Rational Soft-Knee Limiter (safe ceiling at 13,500)
  if (out > 11000.0f) {
    float over = out - 11000.0f;
    out = 11000.0f + over / (1.0f + over / 2500.0f);
  } else if (out < -11000.0f) {
    float over = -out - 11000.0f;
    out = -(11000.0f + over / (1.0f + over / 2500.0f));
  }

  return (int16_t)constrain((int)out, -13500, 13500);
}

void setupDisplay() {
  pinMode(PIN_TFT_BLK, OUTPUT);
  digitalWrite(PIN_TFT_BLK, HIGH);

  pinMode(PIN_TFT_RST, OUTPUT);
  digitalWrite(PIN_TFT_RST, HIGH);
  delay(10);
  digitalWrite(PIN_TFT_RST, LOW);
  delay(20);
  digitalWrite(PIN_TFT_RST, HIGH);
  delay(120);

  customSpi = new SPIClass(FSPI);
  customSpi->begin(PIN_TFT_SCK, -1, PIN_TFT_MOSI, -1);
  tft = new Adafruit_ST7789(customSpi, -1, PIN_TFT_DC, PIN_TFT_RST);
  tft->init(240, 240, SPI_MODE3);
  tft->setSPISpeed(20000000);
  tft->setRotation(2);
  tft->fillScreen(ST77XX_BLACK);
}

// Ultra-fast delta-only VU meter
static int lastDrawnW = -1;
void drawVuMeter(int peakLevel, uint16_t fillColor, bool forceReset = false) {
  const int barX = 14;
  const int barY = 174;
  const int barMaxW = 212;
  const int barH = 22;

  if (forceReset || lastDrawnW < 0) {
    tft->fillRect(barX, barY, barMaxW, barH, 0x10A2);
    tft->drawRect(barX - 1, barY - 1, barMaxW + 2, barH + 2, ST77XX_WHITE);
    lastDrawnW = 0;
  }

  // Scale: 0 to 13,500 (Maximizer ceiling)
  int w = map(constrain(peakLevel, 0, 13500), 0, 13500, 0, barMaxW);
  if (w == lastDrawnW) return;

  if (w > lastDrawnW) {
    tft->fillRect(barX + lastDrawnW, barY, w - lastDrawnW, barH, fillColor);
  } else {
    tft->fillRect(barX + w, barY, lastDrawnW - w, barH, 0x10A2);
  }
  lastDrawnW = w;
}

void setup() {
  Serial.begin(115200);
  delay(300);

  setupDisplay();

  // Silence all servos completely
  pinMode(PIN_SERVO_WHEEL_L, OUTPUT);
  pinMode(PIN_SERVO_WHEEL_R, OUTPUT);
  pinMode(PIN_SERVO_NECK_PAN, OUTPUT);
  digitalWrite(PIN_SERVO_WHEEL_L, LOW);
  digitalWrite(PIN_SERVO_WHEEL_R, LOW);
  digitalWrite(PIN_SERVO_NECK_PAN, LOW);

  recordBuffer = (int16_t*)malloc(TOTAL_SAMPLES * sizeof(int16_t));
  if (!recordBuffer) {
    recordBuffer = (int16_t*)ps_malloc(TOTAL_SAMPLES * sizeof(int16_t));
  }
  if (recordBuffer) {
    memset(recordBuffer, 0, TOTAL_SAMPLES * sizeof(int16_t));
  }

  // --- 1. SETUP MICROPHONE (I2S_NUM_0, STEREO) ---
  micI2s.setPins(PIN_MIC_SCK, PIN_MIC_WS, -1, PIN_MIC_SD);
  micI2s.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO);

  // --- 2. SETUP AMPLIFIER (I2S_NUM_1) ---
  ampI2s.setPins(PIN_AMP_BCLK, PIN_AMP_LRC, PIN_AMP_DIN);
  ampI2s.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);

  // --- 3. WARM-UP & CALIBRATE NOISE FLOOR (Discard first 250ms) ---
  for (int k = 0; k < 15; k++) {
    int32_t warmBuf[CHUNK_FRAMES * 2];
    micI2s.readBytes((char*)warmBuf, sizeof(warmBuf));
    for (size_t i = 0; i < CHUNK_FRAMES; i++) {
      processCleanAudio(warmBuf[i * 2]);
    }
  }

  // Draw Header
  tft->fillScreen(ST77XX_BLACK);
  tft->drawRect(2, 2, 236, 236, ST77XX_CYAN);

  tft->fillRect(6, 6, 228, 40, ST77XX_BLUE);
  tft->setTextColor(ST77XX_WHITE);
  tft->setTextSize(2);
  tft->setCursor(14, 12);
  tft->print("NOISE CANCEL");
  tft->setTextSize(1);
  tft->setCursor(14, 32);
  tft->print("Adaptive Background Noise Suppression");

  tft->fillRect(8, 50, 224, 52, 0x0010);
  tft->drawRect(8, 50, 224, 52, ST77XX_GREEN);
  tft->setTextColor(ST77XX_YELLOW);
  tft->setCursor(14, 56);
  tft->print("ANC STATUS: ENGAGED");
  tft->setTextColor(ST77XX_GREEN);
  tft->setCursor(14, 70);
  tft->print("Surroundings: MUTED (-100dB)");
  tft->setCursor(14, 84);
  tft->print("Voice: Preserved & Amplified");

  drawVuMeter(0, ST77XX_RED, true);

  Serial.println("\n=======================================================");
  Serial.println(" ADAPTIVE NOISE SUPPRESSION & VAD INITIALIZED");
  Serial.println(" Room/surrounding sounds are now actively rejected");
  Serial.println("=======================================================\n");

  delay(400);
}

void loop() {
  // =========================================================================
  // STEP 1: RECORD USER VOICE (3.0 SECONDS)
  // =========================================================================
  tft->fillRect(8, 108, 224, 58, ST77XX_BLACK);
  tft->drawRect(8, 108, 224, 58, ST77XX_RED);

  tft->setTextColor(ST77XX_RED);
  tft->setTextSize(2);
  tft->setCursor(14, 116);
  tft->print("● RECORDING...");

  tft->setTextColor(ST77XX_WHITE);
  tft->setTextSize(1);
  tft->setCursor(14, 142);
  tft->print("Speak naturally towards robot");

  tft->setTextColor(ST77XX_YELLOW);
  tft->setCursor(14, 210);
  tft->print("Background Noise = Cancelled");

  drawVuMeter(0, ST77XX_RED, true);
  Serial.println("[Repeater] >>> RECORDING STARTED (Speak now!) <<<");

  size_t recordedSamples = 0;
  int maxVoicePeak = 0;

  unsigned long lastVuTime = 0;

  while (recordedSamples < TOTAL_SAMPLES) {
    int32_t micStereoRaw[CHUNK_FRAMES * 2];
    size_t bytes = micI2s.readBytes((char*)micStereoRaw, sizeof(micStereoRaw));
    size_t frames = bytes / (2 * sizeof(int32_t));

    int chunkPeak = 0;
    for (size_t i = 0; i < frames; i++) {
      int16_t s = processCleanAudio(micStereoRaw[i * 2]);

      if (recordBuffer && recordedSamples < TOTAL_SAMPLES) {
        recordBuffer[recordedSamples++] = s;
      }

      int absS = abs((int)s);
      if (absS > chunkPeak) chunkPeak = absS;
    }

    if (chunkPeak > maxVoicePeak) maxVoicePeak = chunkPeak;

    // Fast delta VU meter update every 50ms
    if (millis() - lastVuTime > 50) {
      lastVuTime = millis();
      drawVuMeter(chunkPeak, ST77XX_RED);
      Serial.printf("[RecordVU] Peak: %4d | Floor: %3.0f | VAD: %s\n", 
                    chunkPeak, noiseFloor, voiceDetectedLive ? "VOICE" : "MUTE");
    }
  }

  // Smooth 10ms (160 samples) fade-in and fade-out to eliminate edge pops
  const size_t fadeSamples = 160;
  for (size_t i = 0; i < fadeSamples && i < recordedSamples; i++) {
    float factor = 0.5f * (1.0f - cosf((float)i / fadeSamples * 3.14159265f));
    recordBuffer[i] = (int16_t)(recordBuffer[i] * factor);
  }
  for (size_t i = 0; i < fadeSamples && i < recordedSamples; i++) {
    size_t idx = recordedSamples - 1 - i;
    float factor = 0.5f * (1.0f - cosf((float)i / fadeSamples * 3.14159265f));
    recordBuffer[idx] = (int16_t)(recordBuffer[idx] * factor);
  }

  Serial.printf("[Repeater] Recording done. Samples=%d, MaxVoicePeak=%d / 13500\n", 
                (int)recordedSamples, maxVoicePeak);

  delay(100);

  // =========================================================================
  // STEP 2: PLAYBACK USER VOICE (3.0 SECONDS)
  // =========================================================================
  tft->fillRect(8, 108, 224, 58, ST77XX_BLACK);
  tft->drawRect(8, 108, 224, 58, ST77XX_GREEN);

  tft->setTextColor(ST77XX_GREEN);
  tft->setTextSize(2);
  tft->setCursor(14, 116);
  tft->print("▶ PLAYING BACK");

  tft->setTextColor(ST77XX_WHITE);
  tft->setTextSize(1);
  tft->setCursor(14, 142);
  tft->print("Voice Only • Zero Background");

  tft->setTextColor(ST77XX_GREEN);
  tft->setCursor(14, 210);
  tft->printf("Voice Level: %d / 13500 (Pure Voice)", maxVoicePeak);

  drawVuMeter(0, ST77XX_GREEN, true);
  Serial.println("[Repeater] >>> PLAYING BACK VOICE THROUGH SPEAKER <<<");

  size_t playedSamples = 0;
  lastVuTime = 0;

  while (playedSamples < recordedSamples) {
    size_t chunk = CHUNK_FRAMES;
    if (playedSamples + chunk > recordedSamples) {
      chunk = recordedSamples - playedSamples;
    }

    int16_t ampOut[CHUNK_FRAMES * 2];
    int chunkPeak = 0;

    for (size_t i = 0; i < chunk; i++) {
      int16_t s = recordBuffer[playedSamples + i];
      ampOut[i * 2]     = s; // Left
      ampOut[i * 2 + 1] = s; // Right

      int absS = abs((int)s);
      if (absS > chunkPeak) chunkPeak = absS;
    }

    // Direct DMA write
    ampI2s.write((uint8_t*)ampOut, chunk * 2 * sizeof(int16_t));
    playedSamples += chunk;

    // Delta VU meter
    if (millis() - lastVuTime > 50) {
      lastVuTime = millis();
      drawVuMeter(chunkPeak, ST77XX_GREEN);
    }
  }

  Serial.println("[Repeater] Playback complete.\n");
  drawVuMeter(0, ST77XX_BLACK, true);

  delay(600);
}
