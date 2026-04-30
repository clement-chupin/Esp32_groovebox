#include "display_module.h"
#include "config_module.h"
#include "synth_module.h"
#include "drum_module.h"
#include "effects_module.h"
#include "modes_module.h"
#include "controls_module.h"

// External hardware references
extern Adafruit_NeoPixel strip;
extern U8G2_SH1107_PIMORONI_128X128_F_HW_I2C u8g2;
extern bool pressed[TOTAL_BUTTONS];
extern uint32_t columnColors[COLS];

static int selectionRowForKey(int key) {
  if (key < 0 || key >= MAIN_BUTTONS) return -1;
  return key / COLS;
}

static int selectionColForKey(int key) {
  if (key < 0 || key >= MAIN_BUTTONS) return -1;
  return key % COLS;
}

static int selectionPage() {
  return (int)(selectionPageIndex % 3);
}

// FX UI order (28 slots) grouped by type, with 4 shortcut duplicates to avoid empty cells.
static constexpr int8_t kFxUiOrder[28] = {
  // Filters
  0, 1, 2, 6, 14,
  // Delays / space
  3, 4, 7, 15,
  // Motion
  5, 10, 17, 18,
  // Drive / texture
  9, 11, 12,
  // Harmonic / formant
  21, 22, 23, 25,
  // LFO + dynamic filters
  16, 24, 19, 20,
  // Unused slots: keep page without duplicated effects
  -1, -1, -1, -1
};

static int effectUiSlotToIndex(int uiSlot) {
  if (uiSlot < 0) return -1;
  if (uiSlot >= (int)(sizeof(kFxUiOrder) / sizeof(kFxUiOrder[0]))) return -1;
  return (int)kFxUiOrder[uiSlot];
}

static inline bool isSelectableEffectSlot(int idx) {
  return idx >= 0 && idx < EFFECT_COUNT && idx != 8 && idx != 13 && idx != 26 && idx != 27;
}

static uint16_t effectTypeHue(int effectIdx) {
  if (effectIdx <= 0) return 0;
  // Filters / formants
  if (effectIdx == 1 || effectIdx == 2 || effectIdx == 6 || effectIdx == 14 || effectIdx == 19 || effectIdx == 20 || effectIdx == 23 || effectIdx == 25) return 19000;
  // Delay / reverb
  if (effectIdx == 3 || effectIdx == 4 || effectIdx == 7 || effectIdx == 15) return 42000;
  // Motion / modulation
  if (effectIdx == 5 || effectIdx == 10 || effectIdx == 17 || effectIdx == 18) return 32000;
  // Drive / texture
  if (effectIdx == 9 || effectIdx == 11 || effectIdx == 12) return 5000;
  // Harmonic layer
  if (effectIdx == 21 || effectIdx == 22) return 10500;
  // LFO markers
  if (effectIdx == 16) return 23000;
  if (effectIdx == 24) return 3500;
  return 44000;
}

static inline bool splitUiSlotsAvailable() {
  return (SHAPE_COUNT + 2) < (MAIN_BUTTONS - 4);
}

static const char* selectionPageName() {
  switch (selectionPage()) {
    case 0: return "INSTR";
    case 1: return "FX";
    case 2: return "ARP+ENV";
    default: return "?";
  }
}

static uint16_t selectionHueForKey(int key) {
  int row = selectionRowForKey(key);
  int col = selectionColForKey(key);
  if (row < 0 || col < 0) return 0;

  if (row == 0 && col < 4) return 22000;  // modes
  if (selectionPage() == 2 && row == 1 && col < 4) return 52000;  // BPM row
  if (selectionPage() == 2 && row == 1 && col >= 4) {
    if (col == 4) return 3000; // Chrom: rose/red
    return 16000; // other scales: orange-yellow
  }

  // Check if this is an LFO mode effect in FX page
  if (selectionPage() == 1) {
    int flat = row * COLS + col;
    int effectIdx = effectUiSlotToIndex(flat - 4);
    return effectTypeHue(effectIdx);
  }

  if (selectionPage() == 0) {
    int flat = row * COLS + col;
    int slot = flat - 4;
    if (splitUiSlotsAvailable()) {
      if (slot == SHAPE_COUNT) return 14000;
      if (slot == SHAPE_COUNT + 1) return 43000;
      if (slot == SHAPE_COUNT + 2) return 52000;
    }
  }

  switch (selectionPage()) {
    case 0: return 0;       // instruments
    case 1: return 44000;   // effects
    case 2: return (row <= 1) ? 11000 : 30000;  // arp/env
    default: return 0;
  }
}

static bool selectionKeyIsAvailable(int key) {
  int row = selectionRowForKey(key);
  int col = selectionColForKey(key);
  if (row < 0 || col < 0) return false;

  if (row == 0 && col < 4) return true;

  int flat = row * COLS + col;
  switch (selectionPage()) {
    case 0: {
      int slot = flat - 4;
      if (slot >= 0 && slot < SHAPE_COUNT) return true;
      if (splitUiSlotsAvailable() && slot >= SHAPE_COUNT && slot <= SHAPE_COUNT + 2) return true;
      return false;
    }
    case 1: {
      int effectIdx = effectUiSlotToIndex(flat - 4);
      return effectIdx == 0 || isSelectableEffectSlot(effectIdx);
    }
    case 2:
      if (row == 0) {
        int arpSlot = flat - 4;
        return arpSlot >= 0 && arpSlot < ARP_PRESET_COUNT;
      }
      if (row == 1) {
        if (col < 4) return true;                 // BPM
        return col < 8;                           // SCALE
      }
      return (flat - 16) < ENV_PRESET_COUNT;
    default:
      return false;
  }
}

static bool selectionKeyIsActive(int key) {
  int row = selectionRowForKey(key);
  int col = selectionColForKey(key);
  if (row < 0 || col < 0) return false;

  if (row == 0 && col < 4) return col == (int)currentMode;

  int flat = row * COLS + col;

  switch (selectionPage()) {
    case 0: {
      int slot = flat - 4;
      if (slot < 0) return false;
      if (slot < SHAPE_COUNT) {
        if (!instrumentSplitEnabled) return slot == cachedShape;
        return slot == splitShapeLeft || slot == splitShapeRight;
      }
      if (!splitUiSlotsAvailable()) return false;
      if (slot == SHAPE_COUNT) return instrumentSplitEnabled;
      if (slot == SHAPE_COUNT + 1) return instrumentSplitEnabled && splitEditSide == 0;
      if (slot == SHAPE_COUNT + 2) return instrumentSplitEnabled && splitEditSide == 1;
      return false;
    }
    case 1: {
      int effectIdx = effectUiSlotToIndex(flat - 4);
      if (effectIdx != 0 && !isSelectableEffectSlot(effectIdx)) return false;
      if (effectIdx == 0) return !anyEffectEnabled();
      return effectEnabled[effectIdx];
    }
    case 2: {
      if (row == 0) {
        return (flat - 4) == cachedArpIndex;
      }
      if (row == 1) {
        if (col < 4) return false;
        return (col - 4) == (int)(currentScaleIndex % 4);
      }
      int envSlot = flat - 16;
      return envSlot == cachedEnvIndex;
    }
    default:
      return false;
  }
}

static bool selectionKeyIsDefault(int key) {
  int row = selectionRowForKey(key);
  int col = selectionColForKey(key);
  if (row < 0 || col < 0) return false;
  int flat = row * COLS + col;

  if (selectionPage() == 2 && row == 0) return (flat - 4) == 0;
  if (selectionPage() == 2 && row == 1) return col == 1 || col == 4;
  if (selectionPage() == 2 && row >= 2) return (flat - 16) == 0;

  if (selectionPage() == 1) {
    int effectIdx = effectUiSlotToIndex(flat - 4);
    if (effectIdx != 0 && !isSelectableEffectSlot(effectIdx)) return false;
    return effectIdx == 0;
  }

  return false;
}

static int activeEffectCount() {
  int n = 0;
  for (int i = 1; i < EFFECT_COUNT; i++) {
    if (isSelectableEffectSlot(i) && effectEnabled[i]) n++;
  }
  return n;
}

static uint8_t currentDisplayedStep() {
  return (uint8_t)((performanceStep % currentPerformanceLength()) + 1);
}

static void drawSignalPreview(int x, int y, int w, int h) {
  int prevX = x;
  int prevY = y + h / 2;
  for (int i = 0; i < w; i++) {
    float t = (float)i / (float)(w - 1);
    float wave = 0.0f;
    switch (cachedShape) {
      case 0: wave = (t < 0.5f) ? 0.95f : -0.95f; break;
      case 1: wave = (t < 0.5f) ? (-1.0f + 4.0f * t) : (3.0f - 4.0f * t); break;
      case 2: wave = 0.55f * sinf(t * 6.28318f) + 0.24f * sinf(t * 12.56636f); break;
      case 3: wave = 0.75f * sinf(t * 6.28318f) + 0.18f * ((t < 0.5f) ? 1.0f : -1.0f); break;
      case 4: wave = 0.68f * (2.0f * t - 1.0f) + 0.20f * sinf(t * 12.56636f); break;
      case 5: wave = 0.75f * (2.0f * t - 1.0f) + 0.16f * ((t < 0.5f) ? 1.0f : -1.0f); break;
      case 6: wave = 0.72f * (2.0f * t - 1.0f) + 0.18f * sinf(t * 6.28318f); break;
      case 7: wave = 0.34f * sinf(t * 6.28318f) + 0.22f * sinf(t * 12.56636f) + 0.15f * ((t < 0.5f) ? 1.0f : -1.0f); break;
      default: wave = 0.50f * sinf(t * 6.28318f); break;
    }

    switch (cachedEffectIndex) {
      case 1: wave *= 0.72f; break;
      case 2: wave = (wave > 0.0f) ? 0.62f : -0.62f; break;
      case 8:
      case 9:
        if (wave > 0.55f) wave = 0.55f;
        if (wave < -0.55f) wave = -0.55f;
        break;
      case 10: wave += 0.10f * sinf(t * 25.13272f); break;
      case 11: wave = sinf((t + 0.05f * sinf(t * 6.28318f)) * 6.28318f); break;
      case 12: wave = floorf(wave * 4.0f) / 4.0f; break;
      case 13: wave *= 0.45f + 0.55f * sinf(t * 6.28318f); break;
      default: break;
    }

    int px = x + i;
    int py = y + h / 2 - (int)(wave * (float)(h / 2 - 2));
    if (i > 0) u8g2.drawLine(sx(prevX), prevY, sx(px), py);
    prevX = px;
    prevY = py;
  }
}

static void renderSelectionLeds(uint16_t t) {
  bool blinkOn = ((millis() / 90UL) & 0x1U) == 0;
  for (int r = 0; r < ROWS; r++) {
    for (int c = 0; c < COLS; c++) {
      int key = r * COLS + c;
      if (!selectionKeyIsAvailable(key)) {
        lightButton(r, c, dynamicColor(0, 0, 0));
        continue;
      }
      uint16_t hue = selectionHueForKey(key);
      if (selectionKeyIsDefault(key)) {
        if (selectionPage() == 2 && r == 1 && c == 4) {
          hue = 3000;
        } else {
          hue = (key < 8) ? 9000 : 51000;
        }
      }
      int effectIdx = effectUiSlotToIndex(key - 4);
      bool isLfoFxSlot = (selectionPage() == 1) && (effectIdx == 16 || effectIdx == 24);
      uint8_t val = 120;
      if (selectionKeyIsActive(key)) {
        val = blinkOn ? 255 : 96;
      } else if (pressed[key]) {
        val = 235;
      } else if (isLfoFxSlot) {
        val = 170;
      }
      uint8_t sat = isLfoFxSlot ? 220 : 255;
      lightButton(r, c, dynamicColor(hue + t / 12, sat, val));
    }
  }
}

// ============================================================
//  UTILITY FUNCTIONS
// ============================================================
int countPressedMainButtons() {
  int n = 0;
  for (int i = 0; i < MAIN_BUTTONS; i++) if (pressed[i]) n++;
  return n;
}

uint32_t dynamicColor(uint16_t hue, uint8_t sat, uint8_t val) {
  return strip.gamma32(strip.ColorHSV(hue, sat, val));
}

int sx(int x) {
  int v = x + DISPLAY_X_SHIFT;
  if (v < 0) v = 0;
  if (v > 127) v = 127;
  return v;
}

int getButtonLedIndex(int row, int col) {
  int ledsPerRow = COLS * LEDS_PER_BUTTON;
  int btnInRow   = (row % 2 == 0) ? col : (COLS - 1) - col;
  return (row * ledsPerRow) + (btnInRow * LEDS_PER_BUTTON);
}

void lightButton(int row, int col, uint32_t color) {
  int first = getButtonLedIndex(row, col);
  for (int i = 0; i < LEDS_PER_BUTTON; i++) strip.setPixelColor(first + i, color);
}

void lightExtraButton(int idx, uint32_t color) {
  int first = (MAIN_BUTTONS * LEDS_PER_BUTTON) + (idx * LEDS_PER_BUTTON);
  for (int i = 0; i < LEDS_PER_BUTTON; i++) strip.setPixelColor(first + i, color);
}

// ============================================================
//  LED RENDERING
// ============================================================
void renderLeds() {
  strip.clear();
  uint16_t t = (uint16_t)((millis() * (2 + bpm / 30)) & 0xFFFF);
  int pressedCount = countPressedMainButtons();
  uint8_t pressBoost = (uint8_t)constrain(80 + pressedCount * 10, 80, 220);

  if (selectionOverlayActive) {
    renderSelectionLeds(t);
  } else if (drumBankTempoMenuActive) {
    for (int r = 0; r < ROWS; r++) {
      for (int c = 0; c < COLS; c++) {
        int key = r * COLS + c;
        if (r == 0) {
          bool avail = (c < 4);
          if (!avail) {
            lightButton(r, c, dynamicColor(0, 0, 0));
            continue;
          }
          bool selected = ((uint8_t)c == drumDivisionIndex);
          uint16_t hue = (uint16_t)(52000 + c * 3000 + t / 4);
          uint8_t val = selected ? 255 : (pressed[key] ? 180 : 55);
          lightButton(r, c, dynamicColor(hue, 255, val));
        } else {
          int bankSlot = (r - 1) * COLS + c;
          bool avail = (bankSlot >= 0 && bankSlot < DRUM_BANK_COUNT);
          if (!avail) {
            lightButton(r, c, dynamicColor(0, 0, 0));
            continue;
          }
          bool selected = bankSlot == currentDrumBank;
          uint16_t hue = (uint16_t)(drumBanks[bankSlot].hueBase + t / 3);
          uint8_t val = selected ? 255 : (pressed[key] ? 180 : 50);
          lightButton(r, c, dynamicColor(hue, 255, val));
        }
      }
    }
  } else if (currentMode == MODE_INSTRUMENT) {
    for (int r = 0; r < ROWS; r++) {
      for (int c = 0; c < COLS; c++) {
        int k = r * COLS + c;
        uint8_t liveShape = (uint8_t)cachedShape;
        if (instrumentSplitEnabled) {
          liveShape = (c < (COLS / 2)) ? splitShapeLeft : splitShapeRight;
        }
        uint16_t hue = (uint16_t)(t + c * 6000 + liveShape * 2400 + cachedEffectIndex * 900);
        uint8_t val = pressed[k] ? (uint8_t)constrain(pressBoost + fxAmount / 4, 0, 255) : 8;
        lightButton(r, c, dynamicColor(hue, 230, val));
      }
    }
  } else if (currentMode == MODE_DRUMBOX) {
    uint8_t drumSteps = currentDrumSteps();
    for (int r = 0; r < DRUM_ROWS; r++) {
      for (int c = 0; c < COLS; c++) {
        uint8_t step = (uint8_t)c;
        if (drumSteps == 16) step = (uint8_t)(c + ((drumEditPage & 0x01) * 8));
        bool stepInRange = (step < drumSteps);
        uint16_t baseHue = (uint16_t)(drumBanks[currentDrumBank].hueBase + r * 8000 + c * 500 + t / 4);
        uint32_t base = dynamicColor(baseHue, 80, 2);
        if (stepInRange) {
          base = drumPattern[r][step] ? dynamicColor(baseHue, 255, 140) : dynamicColor(baseHue, 120, 4);
          if (step == drumStep) base = dynamicColor(baseHue + 2000, 180, drumRun ? 255 : 90);
        }
        lightButton(r, c, base);
      }
    }
  } else if (currentMode == MODE_DRUM_INSTRUMENT) {
    uint8_t drumSteps = currentDrumSteps();
    for (int r = 0; r < ROWS; r++) {
      for (int c = 0; c < COLS; c++) {
        int k = r * COLS + c;
        if (r < 2) {
          uint8_t step = (uint8_t)c;
          if (drumSteps == 16) step = (uint8_t)(c + r * 8);
          bool inRange = step < drumSteps;
          bool on = inRange ? drumPattern[drumInstrSelectedRow][step] : false;
          uint8_t val = inRange ? (on ? 220 : 25) : 5;
          uint16_t hue = (uint16_t)(8000 + c * 2200 + t / 3);
          if (inRange && step == drumStep) hue += 6000;
          lightButton(r, c, dynamicColor(hue, 220, val));
        } else {
          // Lignes du bas = repère temporel qui défile pour mieux suivre le séquenceur.
          uint8_t step = (uint8_t)c;
          if (drumSteps == 16) {
            step = (uint8_t)(c + ((drumEditPage & 0x01) * 8));
          }
          bool inRange = step < drumSteps;
          bool isCurrent = inRange && (step == drumStep);
          bool beatMark = inRange && ((step & 0x03) == 0);

          uint16_t hue = (uint16_t)(46000 + c * 1200 + t / 5);
          uint8_t val = inRange ? (beatMark ? 70 : 20) : 5;

          if (isCurrent) {
            hue = (uint16_t)(hue + 8000);
            val = drumRun ? 255 : 120;
          }

          // Garder un indice visuel de l'instrument sélectionné sur les 4 premières touches.
          if (c < DRUM_ROWS && c == drumInstrSelectedRow) {
            hue = (uint16_t)(12000 + c * 9000 + t / 2);
            if (val < 160) val = 160;
          }

          // Feedback de pression locale (sélection/trigger).
          if (pressed[k] && val < 240) val = 240;

          lightButton(r, c, dynamicColor(hue, 255, val));
        }
      }
    }
  } else if (currentMode == MODE_MASTER) {
    for (int r = 0; r < ROWS; r++) {
      for (int c = 0; c < COLS; c++) {
        int k = r * COLS + c;
        if (r > 2) {
          lightButton(r, c, dynamicColor(0, 0, 6));
          continue;
        }

        uint16_t hue = (uint16_t)(9000 + r * 12000 + c * 1200 + t / 4);
        if (c == 7) {
          uint8_t val = masterTrackFxEnabled[r] ? 255 : 25;
          if (pressed[k] && val < 245) val = 245;
          lightButton(r, c, dynamicColor(hue + 14000, 255, val));
          continue;
        }

        uint8_t gainCol = (uint8_t)((masterTrackGainQ8[r] * 6) / 255);
        uint8_t val = (c <= gainCol) ? 190 : 18;
        if (pressed[k] && val < 240) val = 240;
        lightButton(r, c, dynamicColor(hue, 220, val));
      }
    }
  } else {
    for (int r = 0; r < ROWS; r++) {
      for (int c = 0; c < COLS; c++) {
        int k = r * COLS + c;
        uint16_t hueBase = (uint16_t)(t + r * 9000 + c * 1500);
        if (c < 4) {
          uint8_t val = pressed[k] ? 255 : 28;
          lightButton(r, c, dynamicColor(hueBase, 180, val));
        } else {
          int slot = r * 4 + (c - 4);
          bool on = (slot < EFFECT_COUNT) ? effectEnabled[slot] : false;
          lightButton(r, c, dynamicColor(hueBase, on ? 255 : 120, on ? 220 : 12));
        }
      }
    }
  }
  bool blinkOn = ((millis() / 90UL) & 0x1U) == 0;
  bool playActive = notePlaybackRunning || drumRun;
  lightExtraButton(EXTRA_MODE_INSTRUMENT, dynamicColor(0, 0, selectionOverlayActive ? (blinkOn ? 255 : 70) : 80));

  if (selectionOverlayActive) {
    lightExtraButton(EXTRA_MODE_DRUMBOX, dynamicColor(50000, 255, selectionPage() == 0 ? 255 : 90)); // page 1
    lightExtraButton(EXTRA_DRUM_PLAY, dynamicColor(16000, 255, selectionPage() == 1 ? 255 : 90));
    lightExtraButton(EXTRA_DRUM_CLEAR, dynamicColor(30000, 255, selectionPage() == 2 ? 255 : 90));
    strip.show();
    return;
  }

  if (drumBankTempoMenuActive) {
    lightExtraButton(EXTRA_MODE_DRUMBOX, dynamicColor(0, 0, 40));
    lightExtraButton(EXTRA_DRUM_PLAY, dynamicColor(0, 0, 40));
    lightExtraButton(EXTRA_DRUM_CLEAR, dynamicColor(8000, 255, blinkOn ? 255 : 90));
    strip.show();
    return;
  }
  
  if (currentMode == MODE_DRUMBOX) {
    // En mode DRUM: EXTRA_MODE_DRUMBOX = Play/Stop, EXTRA_DRUM_PLAY = Reset
    lightExtraButton(EXTRA_MODE_DRUMBOX, dynamicColor(22000, 255, drumRun ? (blinkOn ? 255 : 70) : 100));
    lightExtraButton(EXTRA_DRUM_PLAY, dynamicColor(0, 255, 80));  // Reset button
  } else if (currentMode == MODE_MASTER) {
    lightExtraButton(EXTRA_MODE_DRUMBOX, dynamicColor(18000, 255, noteRecordArmed ? (blinkOn ? 255 : 50) : 80));
    lightExtraButton(EXTRA_DRUM_PLAY, dynamicColor(30000, 255, playActive ? (blinkOn ? 255 : 70) : 100));
  } else {
    // En mode INSTRUMENT: EXTRA_MODE_DRUMBOX = Rec, EXTRA_DRUM_PLAY = Play
    lightExtraButton(EXTRA_MODE_DRUMBOX, dynamicColor(0, 255, noteRecordArmed ? (blinkOn ? 255 : 50) : 80));
    lightExtraButton(EXTRA_DRUM_PLAY, dynamicColor(22000, 255, playActive ? (blinkOn ? 255 : 70) : 100));
  }
  
  lightExtraButton(EXTRA_DRUM_CLEAR, dynamicColor(loopTrackLocked ? 54000 : (7000 + performanceLengthIndex * 2200), 255, loopTrackLocked ? (blinkOn ? 255 : 70) : 130));
  strip.show();
}

// ============================================================
//  OLED DISPLAY RENDERING
// ============================================================
void renderDisplay() {
  if (drumBankTempoMenuActive) {
    u8g2.clearBuffer();
    u8g2.drawFrame(sx(0), 0, 128, 128);
    u8g2.setFont(u8g2_font_7x13B_tf);
    u8g2.setCursor(sx(6), 16); u8g2.print("DRUM CTRL");
    u8g2.setFont(u8g2_font_6x12_tf);
    u8g2.setCursor(sx(6), 33); u8g2.print("Mesure:");
    if (drumDivisionIndex == 0) u8g2.print("1/4");
    else if (drumDivisionIndex == 1) u8g2.print("1/8");
    else u8g2.print("1/16");
    if (drumDivisionIndex == 2) {
      u8g2.print(" P");
      u8g2.print((int)(drumEditPage + 1));
    }
    u8g2.setCursor(sx(6), 46); u8g2.print("BPM:"); u8g2.print((int)bpm);
    u8g2.setCursor(sx(6), 62); u8g2.print("Bank:"); u8g2.print(drumBanks[currentDrumBank].name);
    u8g2.setCursor(sx(6), 78); u8g2.print("Haut: 1/4 1/8 1/16");
    u8g2.setCursor(sx(6), 91); u8g2.print("Bas: drum bank");
    u8g2.setCursor(sx(6), 107); u8g2.print("B3: reset pattern");
    u8g2.setCursor(sx(6), 123); u8g2.print("P1/P2 auto en 1/16");
    u8g2.sendBuffer();
    return;
  }

  if (selectionOverlayActive) {
    int displayFx = cachedEffectIndex;
    if (displayFx < 0 || displayFx >= EFFECT_COUNT) displayFx = 0;
    u8g2.clearBuffer();
    u8g2.drawFrame(sx(0), 0, 128, 128);
    u8g2.setFont(u8g2_font_7x13B_tf);
    u8g2.setCursor(sx(6), 16); u8g2.print("MODE : "); u8g2.print(modeName(currentMode));
    u8g2.setCursor(sx(6), 33); u8g2.print("PAGE : "); u8g2.print(selectionPageName());
    if (selectionPage() == 1) {
      u8g2.setCursor(sx(6), 50); u8g2.print("EFFET: "); u8g2.print(effectNames[cachedEffectIndex]);
      u8g2.setCursor(sx(6), 67); u8g2.print("FXLv : "); u8g2.print((int)fxAmount);
      u8g2.setCursor(sx(6), 84); u8g2.print(effectParam1Name(displayFx)); u8g2.print(": "); u8g2.print((int)fxParam1);
      u8g2.setCursor(sx(6), 101); u8g2.print(effectParam2Name(displayFx)); u8g2.print(": "); u8g2.print((int)fxParam2);
    } else if (selectionPage() == 2) {
      u8g2.setCursor(sx(6), 50); u8g2.print("ARP  : "); u8g2.print(arpPresets[cachedArpIndex].name);
      u8g2.setCursor(sx(6), 67); u8g2.print("ENV  : "); u8g2.print(envPresets[cachedEnvIndex].name);
      u8g2.setCursor(sx(6), 84); u8g2.print("BPM  : "); u8g2.print((int)bpm);
      u8g2.setCursor(sx(6), 101); u8g2.print("SCALE: "); u8g2.print(scaleNames[currentScaleIndex % 4]);
    } else {
      u8g2.setCursor(sx(6), 50); u8g2.print("EFFET: "); u8g2.print(effectNames[cachedEffectIndex]);
      u8g2.setCursor(sx(6), 67); u8g2.print("INSTR: "); u8g2.print(shapeNames[cachedShape]);
      u8g2.setCursor(sx(6), 84); u8g2.print("ARP  : "); u8g2.print(arpPresets[cachedArpIndex].name);
      u8g2.setCursor(sx(6), 101); u8g2.print("ENV  : "); u8g2.print(envPresets[cachedEnvIndex].name);
      u8g2.setCursor(sx(6), 118);
      if (instrumentSplitEnabled) {
        u8g2.print("SPLIT ON L:");
        u8g2.print(shapeNames[splitShapeLeft]);
        u8g2.print(" R:");
        u8g2.print(shapeNames[splitShapeRight]);
      } else {
        u8g2.print("SPLIT OFF");
      }
    }
    u8g2.setFont(u8g2_font_6x12_tf);
    u8g2.setCursor(sx(6), 127); u8g2.print(noteRecordArmed ? "REC " : "    ");
    u8g2.print(notePlaybackRunning ? "PLAY " : "     ");
    u8g2.print(currentDisplayedStep());
    u8g2.print("/");
    u8g2.print(currentPerformanceLength());
    if (loopTrackLocked) u8g2.print(" LOCK");
    u8g2.sendBuffer();
    return;
  }

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_tf);
  u8g2.drawFrame(sx(0), 0, 128, 128);
  u8g2.drawFrame(sx(2), 2, 124, 124);

  const char* modeLabel = modeName(currentMode);
  int tw = u8g2.getStrWidth(modeLabel);
  u8g2.setCursor(sx(64 - tw / 2), 15);
  u8g2.print(modeLabel);

  // Ligne séparatrice
  u8g2.drawHLine(sx(6), 19, 116);

  if (instrumentSplitEnabled) {
    u8g2.setCursor(sx(6), 32); u8g2.print("InstL:"); u8g2.print(shapeNames[splitShapeLeft]);
    u8g2.setCursor(sx(6), 45); u8g2.print("InstR:"); u8g2.print(shapeNames[splitShapeRight]);
    u8g2.setCursor(sx(6), 58); u8g2.print("Env:  "); u8g2.print(envPresets[cachedEnvIndex].name);
    u8g2.setCursor(sx(6), 71); u8g2.print("Scale:"); u8g2.print(scaleNames[currentScaleIndex % 4]);
  } else {
    u8g2.setCursor(sx(6), 32); u8g2.print("Inst: "); u8g2.print(shapeNames[cachedShape]);
    u8g2.setCursor(sx(6), 45); u8g2.print("Env:  "); u8g2.print(envPresets[cachedEnvIndex].name);
    u8g2.setCursor(sx(6), 58); u8g2.print("Scale:"); u8g2.print(scaleNames[currentScaleIndex % 4]);
  }
  u8g2.setCursor(sx(6), 84); u8g2.print("FX:   ");
  int fxCount = activeEffectCount();
  if (fxCount == 0) {
    u8g2.print("None");
  } else {
    int displayFx = cachedEffectIndex;
    if (displayFx <= 0 || displayFx >= EFFECT_COUNT || !effectEnabled[displayFx]) {
      displayFx = 1;
      while (displayFx < EFFECT_COUNT && (!isSelectableEffectSlot(displayFx) || !effectEnabled[displayFx])) displayFx++;
      if (displayFx >= EFFECT_COUNT) displayFx = 0;
    }
    u8g2.print(effectNames[displayFx]);
    if (fxCount > 1) {
      u8g2.print(" +");
      u8g2.print(fxCount - 1);
    }
  }
  u8g2.setCursor(sx(6), 97); u8g2.print("FXLv: "); u8g2.print((int)fxAmount);
  u8g2.setCursor(sx(6), 110); u8g2.print("Oct:  "); u8g2.print(octaveShift);
  u8g2.setCursor(sx(6), 123); u8g2.print("BPM:  "); u8g2.print((int)lroundf((float)bpm));

  if (currentMode == MODE_DRUMBOX) {
    uint8_t drumSteps = currentDrumSteps();
    u8g2.setCursor(sx(6), 110);
    u8g2.print("Pos:  ");
    u8g2.print((int)drumStep + 1);
    u8g2.print("/");
    u8g2.print((int)drumSteps);
    u8g2.print(drumRun ? " RUN" : " STOP");
    u8g2.setCursor(sx(6), 123);
    u8g2.print("Div:");
    if (drumSteps == 4) u8g2.print("1/4 ");
    else if (drumSteps == 8) u8g2.print("1/8 ");
    else u8g2.print("1/16");
    if (drumSteps == 16) {
      u8g2.print(" P");
      u8g2.print((int)(drumEditPage + 1));
      u8g2.print(" ");
    } else {
      u8g2.print(" ");
    }
    u8g2.print("B:");
    u8g2.print(drumBanks[currentDrumBank].name);
  } else if (currentMode == MODE_INSTRUMENT) {
    u8g2.setCursor(sx(6), 110);
    u8g2.print("Arp:  ");
    u8g2.print(arpPresets[cachedArpIndex].name);

    u8g2.setCursor(sx(6), 123);
    u8g2.print(noteRecordArmed ? "R " : "  ");
    u8g2.print(notePlaybackRunning ? "P " : "  ");
    u8g2.print(currentDisplayedStep());
    u8g2.print("/");
    u8g2.print(currentPerformanceLength());
    if (instrumentSplitEnabled) u8g2.print(" SP");
    u8g2.print(loopTrackLocked ? " LK" : " FR");
  } else if (currentMode == MODE_MASTER) {
    const char* trackNames[3] = {"INS", "DRM", "LOOP"};
    for (int i = 0; i < 3; i++) {
      int y = 110 + i * 6;
      u8g2.setCursor(sx(6), y);
      u8g2.print(trackNames[i]);
      u8g2.print(": ");
      uint16_t pct = (uint16_t)((masterTrackGainQ8[i] * 200U) / 255U);
      u8g2.print((int)pct);
      u8g2.print("% ");
      u8g2.print(masterTrackFxEnabled[i] ? "FX" : "DRY");
    }
    u8g2.setCursor(sx(6), 127);
    u8g2.print("L1-3 vol, C8 FX on/off");
  } else {
    u8g2.setCursor(sx(6), 110);
    u8g2.print("L:loop R:drum");
    u8g2.setCursor(sx(6), 123);
    u8g2.print("Bank: ");
    u8g2.print(drumBanks[currentDrumBank].name);
  }

  u8g2.sendBuffer();
}
