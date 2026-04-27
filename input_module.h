#pragma once

#include <Arduino.h>

int8_t quantizeOctaveFromPot(int potValue);
int8_t clampOctave(int8_t o);
int invertPotValue(int raw);

void scanButtons();
void consumeButtonEvents();
void readPots();

void handleExtraButtons();
void handleInstrumentMode();
void handleDrumMode();
void handleDrumInstrumentMode();
void handleDrumShowMode();
void processInputActions();
