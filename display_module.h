#pragma once

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <U8g2lib.h>
#include "modes_module.h"

int countPressedMainButtons();
const char* modeName(AppMode m);
uint32_t dynamicColor(uint16_t hue, uint8_t sat, uint8_t val);
int sx(int x);

void lightButton(int row, int col, uint32_t color);
void lightExtraButton(int idx, uint32_t color);

void renderLeds();
void renderDisplay();
