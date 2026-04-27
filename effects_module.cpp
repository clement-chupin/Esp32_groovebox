#include "effects_module.h"
#include <LowPassFilter.h>
#include <ResonantFilter.h>
#include <StateVariable.h>
#include <Oscil.h>
#include <tables/cos512_int8.h>
#include <tables/cos2048_int8.h>

uint8_t fxParam1 = 128;
uint8_t fxParam2 = 128;
uint8_t effectLFOMode[EFFECT_COUNT] = {0};  // Initialize all to OFF
uint8_t lfoSineTargetEffect = 0;             // No target by default
uint8_t lfoSquareTargetEffect = 0;           // No target by default
uint8_t lastNonLFOEffectEnabled = 0;         // last non-LFO effect that was enabled
uint8_t lfoSineFreqParam = 128;
uint8_t lfoSineDepthParam = 128;
uint8_t lfoSquareFreqParam = 128;
uint8_t lfoSquareDepthParam = 128;

static const char* kFxParam1Names[EFFECT_COUNT] = {
  "-", "Res", "Cut", "FB", "FB", "Depth", "Depth", "Res",
  "Res", "Drv", "Depth", "Drive", "Hold", "-", "Sens", "Size",
  "Freq", "Bias", "Rate", "Floor", "Peak", "Semi", "Tone", "Vow",
  "Freq", "Rate", "-", "-"
};

static const char* kFxParam2Names[EFFECT_COUNT] = {
  "-", "Drv", "Drv", "Wet", "Tone", "FB", "Res", "FB",
  "Drv", "Blend", "FB", "Tone", "Bits", "-", "Res", "Tone",
  "Depth", "Depth", "Mix", "Res", "Res", "Wet", "Mix", "Move",
  "Depth", "Res", "-", "-"
};

// ==================== INITIALISATION ====================
void initEffects() {
  fxLfo.setFreq(0.3f);
  flangerLfo.setFreq(0.4f);
  lfoModSine.setFreq(1.0f);    // Default 1 Hz sine modulation
  lfoModSquare.setFreq(1.0f);  // Default 1 Hz square modulation
  lpf.setResonance(180);
  acidFilter.setResonance(200);
}

// ==================== VARIABLES (non-Mozzi only - Mozzi objects are in .ino) ====================
// All Mozzi objects (LowPassFilter, StateVariable, ResonantFilter, Oscil) 
// are defined in clavier_v2.ino and declared as extern in effects_module.h

// ==================== EFFECT MANAGEMENT ====================
void clearAllEffects() {
  for (int i = 0; i < EFFECT_COUNT; i++) effectEnabled[i] = false;
}

bool anyEffectEnabled() {
  for (int i = 0; i < EFFECT_COUNT; i++) {
    if (effectEnabled[i]) return true;
  }
  return false;
}

bool isEffectActive(int idx) {
  if (idx == 0) return false; // None
  return effectEnabled[idx];
}

void toggleEffectSlot(int idx) {
  if (idx < 0 || idx >= EFFECT_COUNT) return;
  if (idx == 13 || idx == 26 || idx == 27) return;  // removed effects
  if (idx == 0) {
    cachedEffectIndex = 0;
    clearAllEffects();
    return;
  }
  effectEnabled[idx] = !effectEnabled[idx];
  if (effectEnabled[idx]) {
    cachedEffectIndex = idx;
  } else if (cachedEffectIndex == idx) {
    cachedEffectIndex = 0;
    for (int i = 1; i < EFFECT_COUNT; i++) {
      if (effectEnabled[i]) {
        cachedEffectIndex = i;
        break;
      }
    }
  }
}

const char* effectParam1Name(int idx) {
  if (idx < 0 || idx >= EFFECT_COUNT) return "-";
  return kFxParam1Names[idx];
}

const char* effectParam2Name(int idx) {
  if (idx < 0 || idx >= EFFECT_COUNT) return "-";
  return kFxParam2Names[idx];
}

// ==================== COMPRESSION ====================
int16_t compress16(int32_t sample, int16_t threshold, uint8_t ratioShift) {
  if (sample > threshold) {
    sample = threshold + ((sample - threshold) >> ratioShift);
  } else if (sample < -threshold) {
    sample = -threshold + ((sample + threshold) >> ratioShift);
  }
  return (int16_t)constrain(sample, -32767, 32767);
}
