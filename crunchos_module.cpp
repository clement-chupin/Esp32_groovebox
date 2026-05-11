// ============================================================
// crunchos_module.cpp
// Helpers qui relient le moteur CrunchOS (Tracker/InputManager/ScreenManager)
// aux systèmes d'entrée, d'affichage et de LED de clavier_v2.
// ============================================================

#include "crunchos_module.h"
#include "config_module.h"
#include "modes_module.h"
#include <Adafruit_NeoPixel.h>
#include <U8g2lib.h>

// Reference aux objets hardware globaux de clavier_v2.ino
extern Adafruit_NeoPixel strip;
extern U8G2_SH1107_PIMORONI_128X128_F_HW_I2C u8g2;
extern bool justPressed[TOTAL_BUTTONS];
extern bool pressed[TOTAL_BUTTONS];

// ──────────────────────────────────────────────────
// Helpers de couleur (portés depuis CrunchOS.ino)
// ──────────────────────────────────────────────────
static inline uint32_t trackColorCrunch(uint8_t track) {
  switch (track) {
    case 0: return 0xFF0000;
    case 1: return 0x00FF00;
    case 2: return 0x0000FF;
    case 3: return 0xFFFF00;
    default: return 0xFFFFFF;
  }
}

static inline uint32_t noteColorCrunch(uint8_t note) {
  uint8_t hue = (note * 255) / 16;
  if (hue < 85)        return ((uint32_t)(hue * 3) << 8) | 255;
  else if (hue < 170)  { hue -= 85; return (255U << 8) | (255 - hue * 3); }
  else                 { hue -= 170; return ((uint32_t)(hue * 3) << 16) | ((255 - hue) << 8); }
}

static int getButtonLedIndexSerpentine(int row, int col) {
  int ledsPerRow = COLS * LEDS_PER_BUTTON;
  int buttonInRow = (row % 2 == 0) ? col : (COLS - 1 - col);
  return (row * ledsPerRow) + (buttonInRow * LEDS_PER_BUTTON);
}

static inline int crunchLogicalRow(int physicalRow) {
  return (ROWS - 1 - physicalRow);
}

static bool isCrunchLogicalKeyPressed(int logicalKeyIndex) {
  for (int pos = 0; pos < MAIN_BUTTONS; pos++) {
    if (!pressed[pos]) continue;
    int physicalRow = pos / COLS;
    int col = pos % COLS;
    int logicalKey = crunchLogicalRow(physicalRow) * COLS + col;
    if (logicalKey == logicalKeyIndex) return true;
  }
  return false;
}

// ──────────────────────────────────────────────────
// Input : traduit justPressed[] → InputManager
// ──────────────────────────────────────────────────
void crunchHandleInput() {
  // InputManager accepte un seul événement "new key press" par frame.
  // On parcourt les boutons principaux et on envoie le premier qui vient d'être pressé.
  for (int pos = 0; pos < MAIN_BUTTONS; pos++) {
    if (!justPressed[pos]) continue;

    int row = pos / COLS;
    int col = pos % COLS;

    // Match original CrunchOS layout: F1..F8 are on the top physical row.
    int keyIndex = crunchLogicalRow(row) * COLS + col;

    crunchInputMgr.UpdateInput(keyIndex);

    char trackCmd = crunchInputMgr.trackCommand;
    int  trackArg = crunchInputMgr.trackCommandArgument;
    char ledCmd   = crunchInputMgr.ledCommand;

    if (ledCmd != ' ') crunchLedCmd = ledCmd;

    if (trackCmd != ' ') {
      crunchTracker.SetCommand(trackCmd, trackArg);
    }
    break;  // un seul événement par frame, comme dans CrunchOS
  }

  // Touche « silence » maintenue (position 31 dans InputManager = key 31 physique)
  // → efface la note courante sur le step courant.
  if (isCrunchLogicalKeyPressed(31) && crunchInputMgr.activeMenu == 0) {
    static unsigned long lastSilenceMs = 0;
    unsigned long now = millis();
    if (now - lastSilenceMs >= 35) {
      lastSilenceMs = now;
      crunchTracker.SetCommand('Z', 0);
    }
  }

  crunchInputMgr.EndFrame();
}

// ──────────────────────────────────────────────────
// Display : délègue à ScreenManager CrunchOS
// ──────────────────────────────────────────────────
void crunchRenderDisplay() {
  u8g2.clearBuffer();
  crunchScreenMgr.Update(crunchTracker, u8g2, crunchLedCmd, crunchVolumeBars, crunchNoteChars);
  u8g2.sendBuffer();
}

// ──────────────────────────────────────────────────
// LEDs : portage de renderButtonLeds() de CrunchOS
// ──────────────────────────────────────────────────
void crunchRenderLeds() {
  strip.clear();

  for (int pos = 0; pos < MAIN_BUTTONS; pos++) {
    if (!pressed[pos]) continue;

    int row = pos / COLS;
    int col = pos % COLS;
    int ledFirst = getButtonLedIndexSerpentine(row, col);
    if (ledFirst >= LED_COUNT) continue;

    int logicalRow = crunchLogicalRow(row);

    uint32_t color = 0x202020;
    if (logicalRow == 0) {
      color = trackColorCrunch((uint8_t)(col % 4));
    } else {
      if (logicalRow == 3 && col == 7) {
        color = 0x00FF00;
      } else {
        int noteRow = 3 - logicalRow;
        int noteSlot = (col * 3) + noteRow;
        color = noteColorCrunch((uint8_t)(noteSlot % 16));
      }
    }
    for (int px = 0; px < LEDS_PER_BUTTON; px++) {
      int ledIdx = ledFirst + px;
      if (ledIdx < LED_COUNT) strip.setPixelColor(ledIdx, color);
    }
  }

  // Silence key stays on the bottom-right note key in the original CrunchOS layout.
  {
    int silenceLedFirst = getButtonLedIndexSerpentine(0, 7);
    for (int px = 0; px < LEDS_PER_BUTTON; px++) {
      int ledIdx = silenceLedFirst + px;
      if (ledIdx < LED_COUNT) strip.setPixelColor(ledIdx, 0x00FF00);
    }
  }

  // Clignotement du tempo sur le premier LED
  if (crunchTracker.tempoBlink > 0) {
    strip.setPixelColor(0, trackColorCrunch((uint8_t)crunchTracker.selectedTrack));
  }

  strip.show();
}
