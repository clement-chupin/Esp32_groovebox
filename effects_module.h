#pragma once

#include <Arduino.h>
#include <Oscil.h>
#include <LowPassFilter.h>
#include <StateVariable.h>
#include <ResonantFilter.h>
#include <tables/cos512_int8.h>
#include <tables/cos2048_int8.h>
#include "config_module.h"

// ==================== INIT ====================
void initEffects();

// ==================== EFFECT STATE ====================
extern int cachedEffectIndex;
extern bool effectEnabled[EFFECT_COUNT];
extern uint8_t fxAmount;
extern uint8_t fxParam1;
extern uint8_t fxParam2;
extern uint8_t effectLFOMode[EFFECT_COUNT];  // which LFO mode modulates each effect
extern uint8_t lfoSineTargetEffect;           // which effect does LFO_Sin modulate (0=none)
extern uint8_t lfoSquareTargetEffect;         // which effect does LFO_Sqr modulate (0=none)
extern uint8_t lastNonLFOEffectEnabled;       // last non-LFO effect that was enabled
extern uint8_t lfoSineFreqParam;              // dedicated frequency parameter for LFO_Sin
extern uint8_t lfoSineDepthParam;             // dedicated depth parameter for LFO_Sin
extern uint8_t lfoSquareFreqParam;            // dedicated frequency parameter for LFO_Sqr
extern uint8_t lfoSquareDepthParam;           // dedicated depth parameter for LFO_Sqr

extern LowPassFilter lpf;
extern StateVariable<LOWPASS> resoEchoFilter;
extern ResonantFilter<LOWPASS> acidFilter;
extern Oscil<COS512_NUM_CELLS, AUDIO_RATE> fxLfo;
extern Oscil<COS2048_NUM_CELLS, AUDIO_RATE> flangerLfo;
extern Oscil<COS512_NUM_CELLS, AUDIO_RATE> lfoModSine;
extern Oscil<COS512_NUM_CELLS, AUDIO_RATE> lfoModSquare;

extern int16_t delayBuffer[DELAY_BUFFER_SIZE];
extern int delayWriteIndex;

// ==================== FUNCTIONS ====================
void clearAllEffects();
bool anyEffectEnabled();
bool isEffectActive(int idx);
void toggleEffectSlot(int idx);
const char* effectParam1Name(int idx);
const char* effectParam2Name(int idx);

// Compression audio
int16_t compress16(int32_t sample, int16_t threshold, uint8_t ratioShift);
