#pragma once

#include <Arduino.h>
#include "config_module.h"

// ==================== CONTROLS STATE ====================
extern int potRaw[5];
extern int potFilt[5];
extern float potSmooth[5];
extern unsigned long lastPotReadMs;

extern float masterVolume;
extern uint16_t bpm;

// ==================== FUNCTIONS ====================
int8_t quantizeOctaveFromPot(int potValue);
int8_t clampOctave(int8_t o);
int invertPotValue(int raw);

void readPots();
void setBpmExternal(uint16_t newBpm);
