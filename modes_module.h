#pragma once

#include <Arduino.h>
#include "config_module.h"

// ==================== MODE STATE ====================
extern AppMode currentMode;
extern AppMode previousMode;

// Scale helper
extern uint8_t currentScaleIndex;

// Tap tempo
extern unsigned long tapTimes[4];
extern uint8_t tapCount;
extern unsigned long lastTapMs;

// Long-press reset
extern unsigned long holdStartShapeMs;
extern unsigned long holdStartArpMs;
extern unsigned long holdStartFxMs;
extern bool holdDoneShape;
extern bool holdDoneArp;
extern bool holdDoneFx;

// Selection overlay + performance transport
extern bool selectionOverlayActive;
extern uint8_t selectionPageIndex;
extern bool drumBankTempoMenuActive;
extern uint8_t drumInstrSelectedRow;
extern bool noteRecordArmed;
extern bool notePlaybackRunning;
extern bool loopTrackLocked;
extern bool lockedEffectMask[EFFECT_COUNT];
extern uint8_t lockedFxAmount;
extern uint8_t performanceStep;
extern uint8_t performanceLengthIndex;
extern uint8_t masterTrackGainQ8[3];
extern bool masterTrackFxEnabled[3];
extern bool instrumentSplitEnabled;
extern uint8_t splitShapeLeft;
extern uint8_t splitShapeRight;
extern uint8_t splitEditSide;

// Boutons et LED
extern bool pressed[TOTAL_BUTTONS];
extern bool justPressed[TOTAL_BUTTONS];
extern bool justReleased[TOTAL_BUTTONS];
extern bool ledRefreshRequested;
extern bool displayRefreshRequested;

// ==================== FUNCTIONS ====================
const char* modeName(AppMode m);

// Tap tempo
void registerTapTempo();

// Mode handlers
void handleExtraButtons();
bool isSelectionModifierHeld();
uint8_t currentPerformanceLength();
void updatePerformanceTransport();
void handleInstrumentMode();
void handleDrumMode();
void handleDrumInstrumentMode();
void handleMasterMode();
void processInputActions();
