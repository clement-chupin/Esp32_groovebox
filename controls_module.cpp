#include "controls_module.h"
#include "synth_module.h"
#include "effects_module.h"
#include "drum_module.h"
#include "crunchos_module.h"

// ==================== VARIABLES ====================
int potRaw[5] = {0};
int potFilt[5] = {0};
float potSmooth[5] = {2047.0f, 2047.0f, 2047.0f, 2047.0f, 2047.0f};
unsigned long lastPotReadMs = 0;

float masterVolume = 160000.0f;
uint16_t bpm = BPM_DEFAULT;

// ==================== OCTAVE QUANTIZATION ====================
int8_t quantizeOctaveFromPot(int potValue) {
  int v = constrain(potValue, 0, 4095);
  if (v < 585) return -4;
  if (v < 1170) return -3;
  if (v < 1755) return -2;
  if (v < 2340) return -1;
  if (v < 2925) return 0;
  if (v < 3510) return 1;
  return 2;
}

int8_t clampOctave(int8_t o) {
  if (o < -4) return -4;
  if (o > 2) return 2;
  return o;
}

int invertPotValue(int raw) {
  return 4095 - constrain(raw, 0, 4095);
}

// ==================== POTENTIOMETER READING ====================
void readPots() {
  extern AppMode currentMode;
  static uint32_t lastPotDebugMs = 0;
  static int lastCrunchOctave = -1;

  for (int i = 0; i < 5; i++) {
    int raw = analogRead(POT_PINS[i]);
    potRaw[i] = raw;
    float inverted = (float)invertPotValue(raw);
    potSmooth[i] = inverted;
    potFilt[i] = (int)inverted;
  }

  // Perceptual taper: less jump at low volume, more resolution in loud range.
  float volNorm = (float)potFilt[0] / 4095.0f;
  if (volNorm < 0.0f) volNorm = 0.0f;
  if (volNorm > 1.0f) volNorm = 1.0f;
  float volCurve = volNorm * volNorm;
  masterVolume = volCurve * 320000.0f;

  // Shape/FX/ENV sont gérés par les pages de sélection (pas par pot en drum pour FX/ENV).
  if (currentMode != MODE_DRUMBOX && currentMode != MODE_DRUM_INSTRUMENT) {
    fxAmount = (uint8_t)constrain(map(potFilt[1], 0, 4095, 0, 255), 0, 255);
    fxParam1 = (uint8_t)constrain(map(potFilt[2], 0, 4095, 0, 255), 0, 255);
    fxParam2 = (uint8_t)constrain(map(potFilt[3], 0, 4095, 0, 255), 0, 255);

    // Keep LFO controls independent from regular effect controls.
    if (cachedEffectIndex == 16) {
      lfoSineFreqParam = fxParam1;
      lfoSineDepthParam = fxParam2;
    } else if (cachedEffectIndex == 24) {
      lfoSquareFreqParam = fxParam1;
      lfoSquareDepthParam = fxParam2;
    }
  }

  // Mapping direct sans filtre - réactivité maximale
  octaveShift = quantizeOctaveFromPot(potFilt[4]);

  // In drum mode, route octave pot to CrunchOS tracker and show popup on changes.
  if (currentMode == MODE_DRUMBOX || currentMode == MODE_DRUM_INSTRUMENT) {
    int oct = map(potFilt[4], 0, 3800, 0, 3);
    oct = constrain(oct, 0, 3);
    crunchTracker.ApplyPotControls(-1, potFilt[4], -1, -1, -1);
    if (oct != lastCrunchOctave) {
      lastCrunchOctave = oct;
      crunchScreenMgr.ShowPotFeedback("OCT", oct, 3);
    }
  }

  // En drumbox et drum-instrument, POT_PARAM_1 contrôle l'amplitude des drums (0.5 - 1.5)
  if (currentMode == MODE_DRUMBOX || currentMode == MODE_DRUM_INSTRUMENT) {
    extern float drumAmplitude;
    drumAmplitude = constrain(map(potFilt[1], 0, 4095, 5000, 15000) / 10000.0f, 0.5f, 1.5f);  // Map 0-4095 to 0.5-1.5
  }

  uint32_t now = millis();
#if ENABLE_POT_DEBUG
  if (now - lastPotDebugMs >= 350) {
    lastPotDebugMs = now;
    Serial.println("[POT DEBUG] idx pin raw filt");
    for (int i = 0; i < 5; i++) {
      Serial.print("[POT DEBUG] ");
      Serial.print(i);
      Serial.print(" ");
      Serial.print(potNames[i]);
      Serial.print(" GPIO");
      Serial.print(POT_PINS[i]);
      Serial.print(" raw=");
      Serial.print(potRaw[i]);
      Serial.print(" filt=");
      Serial.print(potFilt[i]);
      Serial.println();
    }
    Serial.print("[POT DEBUG] octave=");
    Serial.println(octaveShift);
  }
#else
  (void)now;
  (void)lastPotDebugMs;
#endif
}

void setBpmExternal(uint16_t newBpm) {
  bpm = (uint16_t)constrain((int)newBpm, BPM_MIN, BPM_MAX);
}
