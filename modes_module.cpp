#include "modes_module.h"
#include "synth_module.h"
#include "drum_module.h"
#include "effects_module.h"
#include "controls_module.h"
#include "crunchos_module.h"

// ==================== VARIABLES ====================
AppMode currentMode = MODE_INSTRUMENT;
AppMode previousMode = MODE_INSTRUMENT;

uint8_t currentScaleIndex = 0;

unsigned long tapTimes[4] = {0};
uint8_t tapCount = 0;
unsigned long lastTapMs = 0;

unsigned long holdStartShapeMs = 0;
unsigned long holdStartArpMs = 0;
unsigned long holdStartFxMs = 0;
bool holdDoneShape = false;
bool holdDoneArp = false;
bool holdDoneFx = false;

bool selectionOverlayActive = false;
uint8_t selectionPageIndex = 0;
bool drumBankTempoMenuActive = false;
bool noteRecordArmed = false;
bool notePlaybackRunning = false;
bool loopTrackLocked = false;
bool lockedEffectMask[EFFECT_COUNT] = {false};
uint8_t lockedFxAmount = 128;
uint8_t performanceStep = 0;
uint8_t performanceLengthIndex = 1;
uint8_t masterTrackGainQ8[3] = {255, 176, 255};
bool masterTrackFxEnabled[3] = {true, false, true};
bool instrumentSplitEnabled = false;
uint8_t splitShapeLeft = 0;
uint8_t splitShapeRight = 1;
uint8_t splitEditSide = 0;

static constexpr uint8_t kSplitToggleSlot = SHAPE_COUNT;
static constexpr uint8_t kSplitEditLeftSlot = SHAPE_COUNT + 1;
static constexpr uint8_t kSplitEditRightSlot = SHAPE_COUNT + 2;

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

static inline bool isSelectableEffectSlot(int idx) {
  return idx >= 0 && idx < EFFECT_COUNT && idx != 8 && idx != 13 && idx != 26 && idx != 27;
}

// Déclarées dans input_module (sera réorganisé)
extern bool pressed[TOTAL_BUTTONS];
extern bool justPressed[TOTAL_BUTTONS];
extern bool justReleased[TOTAL_BUTTONS];
extern bool ledRefreshRequested;
extern bool displayRefreshRequested;

static unsigned long lastPerformanceStepMs = 0;
static bool recordedNotes[32][MAIN_BUTTONS] = {false};  // 32 steps, notes enregistrées librement
static float recordedBaseFreq[32][MAIN_BUTTONS] = {{0.0f}};
static float recordedPlayFreq[32][MAIN_BUTTONS] = {{0.0f}};
static bool playbackHeld[MAIN_BUTTONS] = {false};
static bool previewNoteActive = false;
static unsigned long previewNoteOffMs = 0;
static constexpr uint8_t PREVIEW_NOTE_KEY = 0xFF;
static constexpr uint8_t MAX_RECORDED_LOOP_NOTES = 96;

static inline bool splitUiSlotsAvailable() {
  return (kSplitEditRightSlot < (MAIN_BUTTONS - 4));
}

struct RecordedLoopNote {
  bool active;
  bool playing;
  bool hasEnd;
  uint8_t key;
  uint16_t startPhaseQ16;
  uint16_t endPhaseQ16;
  float baseFreq;
  float playFreq;
};

static RecordedLoopNote recordedLoopNotes[MAX_RECORDED_LOOP_NOTES] = {};
static int recordOpenSlotByKey[MAIN_BUTTONS] = {0};
static bool recordedLoopStateInitialized = false;
static bool drumTransportBootstrapped = false;
static unsigned long performanceLoopStartMs = 0;
uint8_t drumInstrSelectedRow = 0;
static const uint8_t kPerformanceLengths[3] = {4, 8, 16};

struct FreezePatch {
  uint8_t shape;
  uint8_t envMode;
  int8_t octave;
  uint8_t arp;
  uint8_t fxLevel;
  bool effectMask[EFFECT_COUNT];
};

static FreezePatch freezePatch = {0, 0, 0, 0, 128, {false}};

static void syncArpToPerformanceStep();

static void syncPerformanceClockToNow() {
  performanceLoopStartMs = millis();
  lastPerformanceStepMs = millis();
  performanceStep = 0;
  syncArpToPerformanceStep();
}

static void stopPreviewNote() {
  if (!previewNoteActive) return;
  noteOff(PREVIEW_NOTE_KEY);
  previewNoteActive = false;
}

static void servicePreviewNote() {
  if (!previewNoteActive) return;
  if ((long)(millis() - previewNoteOffMs) >= 0) {
    stopPreviewNote();
  }
}

static void triggerInstrumentPreview() {
  stopPreviewNote();
  float baseFreq = keyToFreqColumnOrder(0);
  noteOnPatched(PREVIEW_NOTE_KEY, baseFreq, baseFreq, (uint8_t)cachedShape, (uint8_t)cachedEnvIndex, false);
  uint16_t previewMs = (uint16_t)constrain((60000UL / bpm) / 2UL, 90UL, 600UL);
  previewNoteOffMs = millis() + previewMs;
  previewNoteActive = true;
}

static void triggerInstrumentPreviewForShape(uint8_t shape) {
  stopPreviewNote();
  float baseFreq = keyToFreqColumnOrder(0);
  noteOnPatched(PREVIEW_NOTE_KEY, baseFreq, baseFreq, shape, (uint8_t)cachedEnvIndex, false);
  uint16_t previewMs = (uint16_t)constrain((60000UL / bpm) / 2UL, 90UL, 600UL);
  previewNoteOffMs = millis() + previewMs;
  previewNoteActive = true;
}

enum SelectionCategory : uint8_t {
  SEL_NONE = 0,
  SEL_MODE,
  SEL_ARP,
  SEL_BPM,
  SEL_SCALE,
  SEL_ENV,
  SEL_INSTRUMENT,
  SEL_EFFECT,
  SEL_DRUM_DIV,
  SEL_DRUM_BANK
};

enum SelectionPage : uint8_t {
  SEL_PAGE_INSTR = 0,
  SEL_PAGE_EFFECTS = 1,
  SEL_PAGE_ARP_ENV = 2
};

static SelectionCategory selectionCategoryForKey(int key) {
  if (key < 0 || key >= MAIN_BUTTONS) return SEL_NONE;
  int row = key / COLS;
  int col = key % COLS;

  // Les 4 premiers boutons restent le choix de mode sur toutes les pages.
  if (row == 0 && col < 4) return SEL_MODE;

  switch ((SelectionPage)(selectionPageIndex % 3)) {
    case SEL_PAGE_INSTR:
      return SEL_INSTRUMENT;
    case SEL_PAGE_EFFECTS:
      return SEL_EFFECT;
    case SEL_PAGE_ARP_ENV:
      if (row == 0 && col >= 4) return SEL_ARP;
      if (row == 1 && col < 4) return SEL_BPM;
      if (row == 1 && col >= 4) return SEL_SCALE;
      return (row >= 2) ? SEL_ENV : SEL_NONE;
    default:
      return SEL_NONE;
  }
}

static int effectUiSlotToIndex(int uiSlot) {
  if (uiSlot < 0) return -1;
  if (uiSlot >= (int)(sizeof(kFxUiOrder) / sizeof(kFxUiOrder[0]))) return -1;
  return (int)kFxUiOrder[uiSlot];
}

static int selectionSlotForKey(int key) {
  if (key < 0 || key >= MAIN_BUTTONS) return -1;
  int row = key / COLS;
  int col = key % COLS;
  int flat = row * COLS + col;

  if (row == 0 && col < 4) return col;

  switch ((SelectionPage)(selectionPageIndex % 3)) {
    case SEL_PAGE_INSTR: {
      int slot = flat - 4;
      return slot;
    }
    case SEL_PAGE_EFFECTS: {
      return effectUiSlotToIndex(flat - 4);
    }
    case SEL_PAGE_ARP_ENV:
      if (row == 0 && col >= 4) return flat - 4;
      if (row == 1 && col < 4) return col;         // BPM slots
      if (row == 1 && col >= 4) return col - 4;    // SCALE slots 0..3
      return (flat - (2 * COLS));                  // ENV slots 0..15
    default:
      return -1;
  }
}

static int firstEnabledEffectSlot() {
  for (int i = 1; i < EFFECT_COUNT; i++) {
    if (isSelectableEffectSlot(i) && effectEnabled[i]) return i;
  }
  return 0;
}

static void ensureRecordedLoopState() {
  if (recordedLoopStateInitialized) return;
  for (int key = 0; key < MAIN_BUTTONS; key++) {
    recordOpenSlotByKey[key] = -1;
  }
  recordedLoopStateInitialized = true;
}

static uint32_t currentPerformanceLoopMs() {
  // Base recording quantization on drum tempo: 1/32 by default, 1/64 in 16-step drum mode.
  uint32_t drumStepMs = (uint32_t)constrain((60000UL / max((uint16_t)1, bpm)) / 4UL, 20UL, 2000UL);
  uint8_t recSubDiv = (currentDrumSteps() >= 16) ? 4 : 2;
  uint32_t stepMs = max<uint32_t>(5UL, drumStepMs / recSubDiv);
  return max<uint32_t>(stepMs * currentPerformanceLength(), 1UL);
}

static uint16_t loopPhaseQ16(unsigned long now, uint32_t loopMs) {
  if (loopMs == 0) return 0;
  unsigned long elapsed = (performanceLoopStartMs == 0) ? 0 : ((now - performanceLoopStartMs) % loopMs);
  uint32_t phase = (uint32_t)(((uint64_t)elapsed * 65536ULL) / (uint64_t)loopMs);
  return (uint16_t)constrain((int)phase, 0, 65535);
}

static bool loopNoteShouldBeActive(const RecordedLoopNote& note, uint16_t phaseQ16) {
  if (!note.active || !note.hasEnd) return false;
  if (note.startPhaseQ16 == note.endPhaseQ16) return false;
  if (note.startPhaseQ16 < note.endPhaseQ16) {
    return phaseQ16 >= note.startPhaseQ16 && phaseQ16 < note.endPhaseQ16;
  }
  return phaseQ16 >= note.startPhaseQ16 || phaseQ16 < note.endPhaseQ16;
}

static void startRecordedLoopNote(RecordedLoopNote& note) {
  uint8_t shape = loopTrackLocked ? freezePatch.shape : (uint8_t)cachedShape;
  uint8_t envMode = loopTrackLocked ? freezePatch.envMode : (uint8_t)cachedEnvIndex;
  noteOnPatched(note.key, note.baseFreq, note.playFreq, shape, envMode, true);
  note.playing = true;
}

static void clearRecordedLoop() {
  ensureRecordedLoopState();
  for (int step = 0; step < 32; step++) {
    for (int key = 0; key < MAIN_BUTTONS; key++) {
      recordedNotes[step][key] = false;
      recordedBaseFreq[step][key] = 0.0f;
      recordedPlayFreq[step][key] = 0.0f;
    }
  }
  for (int i = 0; i < MAX_RECORDED_LOOP_NOTES; i++) {
    recordedLoopNotes[i] = {false, false, false, 0, 0, 0, 0.0f, 0.0f};
  }
  for (int key = 0; key < MAIN_BUTTONS; key++) {
    recordOpenSlotByKey[key] = -1;
    playbackHeld[key] = false;
  }
}

static bool hasRecordedContent() {
  ensureRecordedLoopState();
  for (int i = 0; i < MAX_RECORDED_LOOP_NOTES; i++) {
    if (recordedLoopNotes[i].active) return true;
  }
  return false;
}

static void captureFreezePatchFromCurrentState() {
  freezePatch.shape = (uint8_t)constrain((int)cachedShape, 0, SHAPE_COUNT - 1);
  freezePatch.envMode = (uint8_t)constrain((int)cachedEnvIndex, 0, ENV_PRESET_COUNT - 1);
  freezePatch.octave = octaveShift;
  freezePatch.arp = (uint8_t)constrain((int)cachedArpIndex, 0, ARP_PRESET_COUNT - 1);
  freezePatch.fxLevel = fxAmount;
  for (int i = 0; i < EFFECT_COUNT; i++) {
    freezePatch.effectMask[i] = effectEnabled[i];
    lockedEffectMask[i] = effectEnabled[i];
  }
  lockedFxAmount = fxAmount;
}

static void bakeRecordedPitchForLock() {
  ensureRecordedLoopState();
  for (int i = 0; i < MAX_RECORDED_LOOP_NOTES; i++) {
    if (!recordedLoopNotes[i].active) continue;
    float baseFreq = keyToFreqColumnOrder(recordedLoopNotes[i].key);
    float playFreq = baseFreq;
    int8_t semitone = 0;
    if (freezePatch.arp > 0) {
      uint8_t stepCount = arpPresets[freezePatch.arp].stepCount;
      if (stepCount > 0) {
        uint8_t step = (uint8_t)(((uint32_t)recordedLoopNotes[i].startPhaseQ16 * currentPerformanceLength()) >> 16);
        semitone = arpPresets[freezePatch.arp].steps[step % stepCount];
      }
    }
    recordedLoopNotes[i].baseFreq = baseFreq;
    recordedLoopNotes[i].playFreq = baseFreq * powf(2.0f, (float)semitone / 12.0f);
  }
}

static void applyFrozenEffects() {
  fxAmount = freezePatch.fxLevel;
  cachedEffectIndex = 0;
  clearAllEffects();
  for (int i = 1; i < EFFECT_COUNT; i++) {
    if (isSelectableEffectSlot(i) && freezePatch.effectMask[i]) {
      effectEnabled[i] = true;
      if (cachedEffectIndex == 0) cachedEffectIndex = i;
    }
  }
}

static void releasePlaybackNotes() {
  ensureRecordedLoopState();
  for (int i = 0; i < MAX_RECORDED_LOOP_NOTES; i++) {
    if (!recordedLoopNotes[i].playing) continue;
    noteOffLoopKey(recordedLoopNotes[i].key);
    recordedLoopNotes[i].playing = false;
  }
  for (int key = 0; key < MAIN_BUTTONS; key++) {
    playbackHeld[key] = false;
  }
}

static void retuneHeldNotesToArpGrid() {
  int8_t frozenSemi = 0;
  if (loopTrackLocked && freezePatch.arp > 0) {
    uint8_t sc = arpPresets[freezePatch.arp].stepCount;
    if (sc > 0) frozenSemi = arpPresets[freezePatch.arp].steps[performanceStep % sc];
  }

  for (int i = 0; i < VOICE_COUNT; i++) {
    if (voices[i].active && voices[i].gate) {
      float playFreq = voices[i].baseFreq;
      if (loopTrackLocked && voices[i].loopVoice) {
        playFreq *= powf(2.0f, (float)frozenSemi / 12.0f);
      } else if (cachedArpIndex > 0) {
        playFreq *= powf(2.0f, (float)currentArpSemitone() / 12.0f);
      }
      setVoicePlayFreq(i, playFreq);
    }
  }
}

static void syncLoopPlaybackAtPhase(uint16_t phaseQ16) {
  ensureRecordedLoopState();
  for (int i = 0; i < MAX_RECORDED_LOOP_NOTES; i++) {
    if (!recordedLoopNotes[i].active) continue;
    bool shouldPlay = loopNoteShouldBeActive(recordedLoopNotes[i], phaseQ16);
    if (recordedLoopNotes[i].playing && !shouldPlay) {
      noteOffLoopKey(recordedLoopNotes[i].key);
      recordedLoopNotes[i].playing = false;
      playbackHeld[recordedLoopNotes[i].key] = false;
    } else if (!recordedLoopNotes[i].playing && shouldPlay) {
      startRecordedLoopNote(recordedLoopNotes[i]);
      playbackHeld[recordedLoopNotes[i].key] = true;
    }
  }
}

static void recordLoopNoteOn(uint8_t key, float baseFreq, float playFreq) {
  ensureRecordedLoopState();
  if (key >= MAIN_BUTTONS) return;

  uint32_t loopMs = currentPerformanceLoopMs();
  if (performanceLoopStartMs == 0) performanceLoopStartMs = millis();
  uint16_t phaseQ16 = loopPhaseQ16(millis(), loopMs);

  int slot = recordOpenSlotByKey[key];
  if (slot < 0) {
    for (int i = 0; i < MAX_RECORDED_LOOP_NOTES; i++) {
      if (!recordedLoopNotes[i].active) {
        slot = i;
        break;
      }
    }
  }
  if (slot < 0) {
    slot = 0;
  }

  recordedLoopNotes[slot] = {true, false, false, key, phaseQ16, phaseQ16, baseFreq, playFreq};
  recordOpenSlotByKey[key] = slot;
}

static void recordLoopNoteOff(uint8_t key) {
  ensureRecordedLoopState();
  if (key >= MAIN_BUTTONS) return;
  int slot = recordOpenSlotByKey[key];
  if (slot < 0 || slot >= MAX_RECORDED_LOOP_NOTES || !recordedLoopNotes[slot].active) return;

  uint32_t loopMs = currentPerformanceLoopMs();
  if (performanceLoopStartMs == 0) performanceLoopStartMs = millis();
  recordedLoopNotes[slot].endPhaseQ16 = loopPhaseQ16(millis(), loopMs);
  recordedLoopNotes[slot].hasEnd = true;
  recordOpenSlotByKey[key] = -1;
}

static void rebuildLoopPlaybackFromCurrentPhase() {
  if (!notePlaybackRunning) {
    releasePlaybackNotes();
    return;
  }
  uint32_t loopMs = currentPerformanceLoopMs();
  if (performanceLoopStartMs == 0) performanceLoopStartMs = millis();
  syncLoopPlaybackAtPhase(loopPhaseQ16(millis(), loopMs));
}

// Silence only what is currently sounding in instrument/loop layers,
// without wiping the whole recorded instrument track.
static void silenceCurrentInstrumentNotes() {
  ensureRecordedLoopState();

  uint32_t loopMs = currentPerformanceLoopMs();
  if (performanceLoopStartMs == 0) performanceLoopStartMs = millis();
  uint16_t phaseQ16 = loopPhaseQ16(millis(), loopMs);

  // Stop currently held live notes and close their open record slots.
  for (int key = 0; key < MAIN_BUTTONS; key++) {
    if (!pressed[key]) continue;
    noteOff((uint8_t)key);
    recordLoopNoteOff((uint8_t)key);
  }

  // Remove and stop only notes active at current phase.
  for (int i = 0; i < MAX_RECORDED_LOOP_NOTES; i++) {
    if (!recordedLoopNotes[i].active) continue;

    bool activeNow = loopNoteShouldBeActive(recordedLoopNotes[i], phaseQ16) || recordedLoopNotes[i].playing;
    if (!activeNow) continue;

    uint8_t key = recordedLoopNotes[i].key;
    noteOffLoopKey(key);
    recordedLoopNotes[i].playing = false;
    recordedLoopNotes[i].active = false;
    recordedLoopNotes[i].hasEnd = false;
    playbackHeld[key] = false;
    if (recordOpenSlotByKey[key] == i) {
      recordOpenSlotByKey[key] = -1;
    }
  }

  // Safety: drop any dangling open slot (prevents endless held note bugs).
  for (int key = 0; key < MAIN_BUTTONS; key++) {
    int slot = recordOpenSlotByKey[key];
    if (slot < 0 || slot >= MAX_RECORDED_LOOP_NOTES) continue;
    if (!recordedLoopNotes[slot].active) {
      recordOpenSlotByKey[key] = -1;
      continue;
    }
    if (!recordedLoopNotes[slot].hasEnd && !pressed[key]) {
      recordedLoopNotes[slot].playing = false;
      recordedLoopNotes[slot].active = false;
      recordOpenSlotByKey[key] = -1;
      playbackHeld[key] = false;
    }
  }

  if (notePlaybackRunning) {
    rebuildLoopPlaybackFromCurrentPhase();
  }
}

static void snapshotRecordedStep() {
}

static void syncArpToPerformanceStep() {
  if (cachedArpIndex <= 0) {
    arpStep = 0;
    return;
  }
  uint8_t stepCount = arpPresets[cachedArpIndex].stepCount;
  if (stepCount == 0) {
    arpStep = 0;
    return;
  }
  arpStep = performanceStep % stepCount;
}

static void applySelectionChoice(uint8_t key) {
  SelectionCategory category = selectionCategoryForKey(key);
  int slot = selectionSlotForKey(key);
  if (slot < 0) return;

  switch (category) {
    case SEL_MODE:
      if (slot < APP_MODE_COUNT) currentMode = (AppMode)slot;
      break;
    case SEL_ARP:
      if (slot < ARP_PRESET_COUNT) {
        cachedArpIndex = slot;
        syncArpToPerformanceStep();
        retuneHeldNotesToArpGrid();
      }
      break;
    case SEL_BPM:
      if (slot == 0) setBpmExternal((uint16_t)max((int)BPM_MIN, (int)bpm - 5));
      else if (slot == 1) setBpmExternal((uint16_t)max((int)BPM_MIN, (int)bpm - 1));
      else if (slot == 2) setBpmExternal((uint16_t)min((int)BPM_MAX, (int)bpm + 1));
      else if (slot == 3) setBpmExternal((uint16_t)min((int)BPM_MAX, (int)bpm + 5));
      resetDrumTransport(true);
      syncPerformanceClockToNow();
      break;
    case SEL_SCALE:
      if (slot < 4) {
        currentScaleIndex = (uint8_t)slot;
      }
      break;
    case SEL_ENV:
      if (slot < ENV_PRESET_COUNT) {
        cachedEnvIndex = slot;
      }
      break;
    case SEL_INSTRUMENT:
      if (slot < SHAPE_COUNT) {
        if (instrumentSplitEnabled) {
          if (splitEditSide == 0) splitShapeLeft = (uint8_t)slot;
          else splitShapeRight = (uint8_t)slot;
          cachedShape = (splitEditSide == 0) ? splitShapeLeft : splitShapeRight;
        } else {
          cachedShape = slot;
          splitShapeLeft = (uint8_t)slot;
        }
        if (currentMode == MODE_INSTRUMENT) {
          triggerInstrumentPreviewForShape((uint8_t)slot);
        }
      } else if (splitUiSlotsAvailable() && slot == kSplitToggleSlot) {
        instrumentSplitEnabled = !instrumentSplitEnabled;
        if (instrumentSplitEnabled) {
          splitShapeLeft = (uint8_t)constrain((int)cachedShape, 0, SHAPE_COUNT - 1);
          if (splitShapeRight >= SHAPE_COUNT || splitShapeRight == splitShapeLeft) {
            splitShapeRight = (uint8_t)((splitShapeLeft + 1) % SHAPE_COUNT);
          }
          splitEditSide = 0;
          cachedShape = splitShapeLeft;
        } else {
          splitEditSide = 0;
          cachedShape = splitShapeLeft;
        }
      } else if (splitUiSlotsAvailable() && slot == kSplitEditLeftSlot) {
        splitEditSide = 0;
        cachedShape = splitShapeLeft;
      } else if (splitUiSlotsAvailable() && slot == kSplitEditRightSlot) {
        splitEditSide = 1;
        cachedShape = splitShapeRight;
      }
      break;
    case SEL_EFFECT:
      if (isSelectableEffectSlot(slot) || slot == 0) {
        bool isLFOSlot = (slot == 16 || slot == 24);
        // When enabling an LFO, auto-attach to last enabled non-LFO effect
        if (isLFOSlot && !effectEnabled[slot]) {
          if (slot == 16) lfoSineTargetEffect = lastNonLFOEffectEnabled;
          else            lfoSquareTargetEffect = lastNonLFOEffectEnabled;
        }
        toggleEffectSlot(slot);
        // Track last non-LFO effect just enabled
        if (!isLFOSlot && slot > 0 && effectEnabled[slot]) {
          lastNonLFOEffectEnabled = slot;
        }
        if (slot > 0 && !effectEnabled[slot] && cachedEffectIndex == 0) {
          cachedEffectIndex = firstEnabledEffectSlot();
        }
      }
      break;
    default:
      break;
  }

  ledRefreshRequested = true;
  displayRefreshRequested = true;
}

// ==================== MODE NAME ====================
const char* modeName(AppMode m) {
  switch (m) {
    case MODE_INSTRUMENT: return "INSTRUMENT";
    case MODE_DRUMBOX: return "DRUM";
    case MODE_DRUM_INSTRUMENT: return "DRUM";
    case MODE_MASTER: return "MASTER";
    default: return "?";
  }
}

// ==================== TAP TEMPO ====================
void registerTapTempo() {
  unsigned long now = millis();
  if (lastTapMs != 0 && (now - lastTapMs) >= 180 && (now - lastTapMs) <= 1200) {
    tapTimes[tapCount & 0x03] = now - lastTapMs;
    if (tapCount < 4) tapCount++;
    unsigned long total = 0;
    for (uint8_t i = 0; i < tapCount; i++) total += tapTimes[i];
    if (total > 0) {
      float avg = (float)total / (float)tapCount;
      setBpmExternal((uint16_t)constrain((int)lroundf(60000.0f / avg), (int)BPM_MIN, (int)BPM_MAX));
      resetDrumTransport(true);
      syncPerformanceClockToNow();
    }
  } else {
    tapCount = 0;
  }
  lastTapMs = now;
}

bool isSelectionModifierHeld() {
  return pressed[MAIN_BUTTONS + EXTRA_MODE_INSTRUMENT];
}

uint8_t currentPerformanceLength() {
  // Recording grid follows drum tempo options:
  // - default: 32 micro-steps per loop (1/32)
  // - 16-step drum option: 64 micro-steps per loop (1/64)
  return (currentDrumSteps() >= 16) ? 64 : 32;
}

// ==================== EXTRA BUTTONS HANDLER ====================
void handleExtraButtons() {
  // MODE_DRUMBOX vs MODE_INSTRUMENT: différent comportement du bouton mode
  if (justPressed[MAIN_BUTTONS + EXTRA_MODE_DRUMBOX]) {
    if (currentMode == MODE_DRUMBOX || currentMode == MODE_DRUM_INSTRUMENT) {
      // En mode DRUMBOX: toggle sequencer drum play/stop
      drumRun = !drumRun;
      if (drumRun) lastStepMs = millis();
      ledRefreshRequested = true;
      displayRefreshRequested = true;
    } else if (currentMode == MODE_INSTRUMENT || currentMode == MODE_MASTER) {
      // In instrument/master mode: B2 toggles CrunchOS REC ON/OFF.
      crunchTracker.SetCommand('P', 0);
      noteRecordArmed = crunchTracker.isPlaying;
      ledRefreshRequested = true;
      displayRefreshRequested = true;
    }
  }

  if (justPressed[MAIN_BUTTONS + EXTRA_DRUM_PLAY]) {
    if (currentMode == MODE_DRUMBOX || currentMode == MODE_DRUM_INSTRUMENT) {
      // En mode DRUM: bouton 3 = reset total du pattern.
      extern bool drumPattern[DRUM_ROWS][DRUM_MAX_STEPS];
      for (int r = 0; r < DRUM_ROWS; r++) {
        for (int c = 0; c < DRUM_MAX_STEPS; c++) {
          drumPattern[r][c] = false;
        }
      }
      drumStep = 0;
      drumEditPage = 0;
      ledRefreshRequested = true;
      displayRefreshRequested = true;
    } else if (currentMode == MODE_INSTRUMENT || currentMode == MODE_MASTER) {
      // In instrument/master mode: B3 toggles CrunchOS START/STOP transport.
      crunchTransportRunning = !crunchTransportRunning;
      notePlaybackRunning = crunchTransportRunning;
      ledRefreshRequested = true;
      displayRefreshRequested = true;
    }
  }

  if (justPressed[MAIN_BUTTONS + EXTRA_DRUM_CLEAR]) {
    if (currentMode == MODE_DRUMBOX || currentMode == MODE_DRUM_INSTRUMENT) {
      // Dernier bouton: ouvre/ferme le sous-menu DrumBank + Tempo.
      drumBankTempoMenuActive = !drumBankTempoMenuActive;
      selectionOverlayActive = false;
      ledRefreshRequested = true;
      displayRefreshRequested = true;
    } else if (currentMode == MODE_INSTRUMENT || currentMode == MODE_MASTER) {
      // In instrument/master mode: B4 silences current note(s) only.
      silenceCurrentInstrumentNotes();
      ledRefreshRequested = true;
      displayRefreshRequested = true;
    }
  }
}

void updatePerformanceTransport() {
  // Silence hold: tant que B4 est maintenu en mode instrument, on re-silence en continu.
  if (currentMode == MODE_INSTRUMENT || currentMode == MODE_MASTER) {
    if (pressed[MAIN_BUTTONS + EXTRA_DRUM_CLEAR]) {
      static unsigned long lastSilenceHoldMs = 0;
      unsigned long nowH = millis();
      if (nowH - lastSilenceHoldMs >= 50) {
        lastSilenceHoldMs = nowH;
        silenceCurrentInstrumentNotes();
        ledRefreshRequested = true;
        displayRefreshRequested = true;
      }
    }
  }

  if (!notePlaybackRunning && !noteRecordArmed) return;

  unsigned long now = millis();
  uint32_t loopMs = currentPerformanceLoopMs();
  uint32_t drumStepMs = (uint32_t)constrain((60000UL / max((uint16_t)1, bpm)) / 4UL, 20UL, 2000UL);
  uint8_t recSubDiv = (currentDrumSteps() >= 16) ? 4 : 2;
  uint16_t stepMs = (uint16_t)max<uint32_t>(5UL, drumStepMs / recSubDiv);
  if (performanceLoopStartMs == 0) performanceLoopStartMs = now;
  if (now - performanceLoopStartMs >= loopMs) {
    performanceLoopStartMs += ((now - performanceLoopStartMs) / loopMs) * loopMs;
  }

  unsigned long elapsed = now - performanceLoopStartMs;
  performanceStep = (uint8_t)min((uint32_t)(currentPerformanceLength() - 1), elapsed / max<uint16_t>(stepMs, 1));
  lastPerformanceStepMs = now;
  syncArpToPerformanceStep();
  retuneHeldNotesToArpGrid();
  if (notePlaybackRunning) {
    syncLoopPlaybackAtPhase(loopPhaseQ16(now, loopMs));
  }
  ledRefreshRequested = true;
  displayRefreshRequested = true;
}

void handleMasterMode() {
  // Rows 0..2: instrument/drum/loop mastering.
  // Cols 0..6: gain from 0..2x, col 7: FX on/off for the row.
  for (int key = 0; key < MAIN_BUTTONS; key++) {
    if (!justPressed[key]) continue;

    int row = key / COLS;
    int col = key % COLS;
    if (row > 2) continue;

    if (col == 7) {
      masterTrackFxEnabled[row] = !masterTrackFxEnabled[row];
      continue;
    }

    uint16_t gain = (uint16_t)((col * 512) / 6);  // 0..512
    if (gain > 510) gain = 510;
    masterTrackGainQ8[row] = (uint8_t)((gain + 1) >> 1);  // 0..255
  }
}

// ==================== INSTRUMENT MODE ====================
static bool liveKeyUsesFullEnvelopeMode(uint8_t key) {
  for (int i = 0; i < VOICE_COUNT; i++) {
    if (!voices[i].active || voices[i].loopVoice) continue;
    if (voices[i].key != key) continue;
    uint8_t env = voices[i].envMode;
    if (env == ENV_MODE_PIANO || env == ENV_MODE_CRYSTAL2) return true;
  }
  return false;
}

static inline bool isLeftSplitKey(uint8_t key) {
  return (key % COLS) < (COLS / 2);
}

static float splitKeyToFreqColumnMirror(int key) {
  int row = key / COLS;
  int localCol = key % (COLS / 2);
  int mirroredKey = row * COLS + localCol;
  return keyToFreqColumnOrder(mirroredKey);
}

static uint8_t splitShapeForKey(uint8_t key) {
  return isLeftSplitKey(key) ? splitShapeLeft : splitShapeRight;
}

void handleInstrumentMode() {
  for (int key = 0; key < MAIN_BUTTONS; key++) {
    if (justPressed[key]) {
      float baseFreq = instrumentSplitEnabled ? splitKeyToFreqColumnMirror(key) : keyToFreqColumnOrder(key);
      float playFreq = baseFreq;
      if (cachedArpIndex > 0) {
        playFreq *= powf(2.0f, (float)currentArpSemitone() / 12.0f);
      }
      if (instrumentSplitEnabled) {
        noteOnPatched((uint8_t)key, baseFreq, playFreq, splitShapeForKey((uint8_t)key), (uint8_t)cachedEnvIndex, false);
      } else {
        noteOn((uint8_t)key, baseFreq, playFreq);
      }
      if (noteRecordArmed) {
        recordLoopNoteOn((uint8_t)key, baseFreq, playFreq);
      }
    }
    if (justReleased[key]) {
      if (!liveKeyUsesFullEnvelopeMode((uint8_t)key)) {
        noteOff(key);
      }
      if (noteRecordArmed) {
        recordLoopNoteOff((uint8_t)key);
      }
    }
  }
}

// ==================== DRUM MODE ====================
void handleDrumMode() {
  crunchHandleInput();
}

// ==================== DRUM INSTRUMENT MODE ====================
void handleDrumInstrumentMode() {
  crunchHandleInput();
}

// ==================== INPUT PROCESSING ====================
void processInputActions() {
  if (currentMode == MODE_DRUM_INSTRUMENT) {
    currentMode = MODE_DRUMBOX;
  }

  const bool wasDrumMode = (previousMode == MODE_DRUMBOX || previousMode == MODE_DRUM_INSTRUMENT);
  const bool isDrumModeNow = (currentMode == MODE_DRUMBOX || currentMode == MODE_DRUM_INSTRUMENT);

  if (isDrumModeNow && !wasDrumMode) {
    crunchTransportRunning = true;
    notePlaybackRunning = true;
    crunchTracker.isPlaying = true;
    drumTransportBootstrapped = true;
  }

  const bool drumCrunchMode = isDrumModeNow;

  // In CrunchOS drum modes, keep B1..B4 as direct access to the 4 main modes.
  // This keeps the top row F1..F8 free for CrunchOS controls.
  if (drumCrunchMode) {
    AppMode targetMode = currentMode;
    if (justPressed[MAIN_BUTTONS + EXTRA_MODE_INSTRUMENT]) targetMode = MODE_INSTRUMENT;
    else if (justPressed[MAIN_BUTTONS + EXTRA_MODE_DRUMBOX]) targetMode = MODE_DRUMBOX;
    else if (justPressed[MAIN_BUTTONS + EXTRA_DRUM_PLAY]) targetMode = MODE_MASTER;

    if (targetMode != currentMode) {
      bool leavingDrum = (currentMode == MODE_DRUMBOX || currentMode == MODE_DRUM_INSTRUMENT)
                      && !(targetMode == MODE_DRUMBOX || targetMode == MODE_DRUM_INSTRUMENT);
      if (leavingDrum) {
        // Leaving Crunch drum control: force-stop drum and tracker audio paths.
        drumRun = false;
        stopAllDrumVoices();
        // Keep Crunch transport state so composed drum tracks remain audible
        // when returning to instrument mode.
        notePlaybackRunning = crunchTransportRunning;
        drumTransportBootstrapped = false;
      }
      selectionOverlayActive = false;
      drumBankTempoMenuActive = false;
      currentMode = targetMode;
      ledRefreshRequested = true;
      displayRefreshRequested = true;
      return;
    }

    // Disable legacy overlays while in CrunchOS drum control.
    if (selectionOverlayActive || drumBankTempoMenuActive) {
      selectionOverlayActive = false;
      drumBankTempoMenuActive = false;
      ledRefreshRequested = true;
      displayRefreshRequested = true;
    }
  }

  // Appui unique sur le bouton principal -> entrer/sortir du mode sélection.
  if (!drumCrunchMode && justPressed[MAIN_BUTTONS + EXTRA_MODE_INSTRUMENT]) {
    selectionOverlayActive = !selectionOverlayActive;
    if (selectionOverlayActive) {
      drumBankTempoMenuActive = false;
    }
    ledRefreshRequested = true;
    displayRefreshRequested = true;
  }

  servicePreviewNote();

  // Keep looper transport alive even while menus/overlays are open.
  updatePerformanceTransport();

  if (drumBankTempoMenuActive) {
    if (justPressed[MAIN_BUTTONS + EXTRA_DRUM_CLEAR]) {
      drumBankTempoMenuActive = false;
      ledRefreshRequested = true;
      displayRefreshRequested = true;
      return;
    }

    for (int key = 0; key < MAIN_BUTTONS; key++) {
      if (!justPressed[key]) continue;
      int row = key / COLS;
      int col = key % COLS;

      if (row == 0) {
        // Mesure partagée Instrument/Drumbox: 1/4, 1/8, 1/16.
        if (col < 3) {
          drumDivisionIndex = (uint8_t)col;
          performanceLengthIndex = (uint8_t)col;
          if (drumStep >= currentDrumSteps()) drumStep = 0;
          if (currentDrumSteps() != 16) drumEditPage = 0;
          resetDrumTransport(true);
        }
      } else {
        int bankSlot = (row - 1) * COLS + col;
        if (bankSlot >= 0 && bankSlot < DRUM_BANK_COUNT) {
          currentDrumBank = bankSlot;
        }
      }
    }

    ledRefreshRequested = true;
    displayRefreshRequested = true;
    return;
  }

  // Navigation des pages de sélection via les 3 autres boutons.
  if (selectionOverlayActive) {
    // En menu principal, on laisse vivre les notes en cours et on traite les relâchements
    // pour éviter les notes tenues indéfiniment.
    if (currentMode == MODE_INSTRUMENT || currentMode == MODE_MASTER) {
      for (int key = 0; key < MAIN_BUTTONS; key++) {
        if (justReleased[key] && !liveKeyUsesFullEnvelopeMode((uint8_t)key)) noteOff((uint8_t)key);
      }
    }

    if (justPressed[MAIN_BUTTONS + EXTRA_MODE_DRUMBOX]) {
      selectionPageIndex = 0;
      ledRefreshRequested = true;
      displayRefreshRequested = true;
    }
    if (justPressed[MAIN_BUTTONS + EXTRA_DRUM_PLAY]) {
      selectionPageIndex = 1;
      ledRefreshRequested = true;
      displayRefreshRequested = true;
    }
    if (justPressed[MAIN_BUTTONS + EXTRA_DRUM_CLEAR]) {
      selectionPageIndex = 2;
      ledRefreshRequested = true;
      displayRefreshRequested = true;
    }

    for (int key = 0; key < MAIN_BUTTONS; key++) {
      if (justPressed[key]) applySelectionChoice((uint8_t)key);
    }
    return;
  }

  if (!drumCrunchMode) {
    handleExtraButtons();
  }

  if (currentMode != previousMode) {
    bool leavingDrum = (previousMode == MODE_DRUMBOX || previousMode == MODE_DRUM_INSTRUMENT)
                    && !(currentMode == MODE_DRUMBOX || currentMode == MODE_DRUM_INSTRUMENT);
    if (leavingDrum) {
      drumRun = false;
      stopAllDrumVoices();
      notePlaybackRunning = crunchTransportRunning;
      drumTransportBootstrapped = false;
    }
    if (previousMode == MODE_INSTRUMENT) {
      allNotesOff();
    }
    previousMode = currentMode;
    ledRefreshRequested = true;
    displayRefreshRequested = true;
  }

  if (currentMode == MODE_INSTRUMENT) handleInstrumentMode();
  else if (currentMode == MODE_DRUMBOX) handleDrumMode();
  else if (currentMode == MODE_MASTER) handleMasterMode();
}
