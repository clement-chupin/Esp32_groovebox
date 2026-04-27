// ============================================================
// clavier_v2.ino
// Poly synth + drum sequencer
// Hardware : ESP32, MCP23X17, NeoPixel, SH1107 128x128, I2S DAC
// ============================================================

#include <Arduino.h>
#include "config_module.h"

#include <MozziGuts.h>
#include <Oscil.h>
#include <ADSR.h>
#include <LowPassFilter.h>
#include <StateVariable.h>
#include <ResonantFilter.h>
#include <Sample.h>
#include <tables/sin2048_int8.h>
#include <tables/saw2048_int8.h>
#include <tables/square_no_alias_2048_int8.h>
#include <tables/triangle2048_int8.h>
#include <tables/waveshape_chebyshev_3rd_256_int8.h>
#include <tables/cos512_int8.h>
#include <tables/cos2048_int8.h>
#if ENABLE_SAMPLE_INSTRUMENT_ENGINE || ENABLE_BURROUGHS_KICK_SAMPLE
#include <samples/burroughs1_18649_int8.h>
#endif

#include <Wire.h>
#include <Adafruit_MCP23X17.h>
#include <Adafruit_NeoPixel.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include "esp_bt.h"
#include "esp_bt_main.h"

// ==================== MODULES ====================
#include "synth_module.h"
#include "drum_module.h"
#include "effects_module.h"
#include "controls_module.h"
#include "modes_module.h"
#include "input_module.h"
#include "audio_module.h"
#include "display_module.h"

// ==================== HARDWARE OBJECTS ====================
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_RGB + NEO_KHZ800);
Adafruit_MCP23X17 mcp1;
Adafruit_MCP23X17 mcp2;
Adafruit_MCP23X17 mcpExtra;
TwoWire buttonsWire = TwoWire(1);
U8G2_SH1107_PIMORONI_128X128_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// ==================== COLUMN COLORS ====================
uint32_t columnColors[COLS];

// ==================== MOZZI OBJECTS (must be in .ino with MozziGuts.h) ====================
// Synth objects
Voice voices[VOICE_COUNT] = {};
float voiceCurFreq[VOICE_COUNT] = {0};
float voiceTargetFreq[VOICE_COUNT] = {0};
float voicePitchEnvSemi[VOICE_COUNT] = {0};
float voiceHoldGain[VOICE_COUNT] = {0};
float voiceModPhase[VOICE_COUNT] = {0};
float voiceModAmp[VOICE_COUNT] = {0};

Oscil<SIN2048_NUM_CELLS, AUDIO_RATE> oscSin[VOICE_COUNT];
Oscil<SAW2048_NUM_CELLS, AUDIO_RATE> oscSaw[VOICE_COUNT];
Oscil<SAW2048_NUM_CELLS, AUDIO_RATE> oscSaw2[VOICE_COUNT];
Oscil<SQUARE_NO_ALIAS_2048_NUM_CELLS, AUDIO_RATE> oscSqr[VOICE_COUNT];
Oscil<TRIANGLE2048_NUM_CELLS, AUDIO_RATE> oscTri[VOICE_COUNT];
Oscil<CHEBYSHEV_3RD_256_NUM_CELLS, AUDIO_RATE> oscCheby[VOICE_COUNT];
Oscil<COS2048_NUM_CELLS, AUDIO_RATE> oscCos[VOICE_COUNT];
#if ENABLE_SAMPLE_INSTRUMENT_ENGINE
Sample<BURROUGHS1_18649_NUM_CELLS, AUDIO_RATE> oscSample[VOICE_COUNT];  // Sample instrument
#endif
ADSR<CONTROL_RATE, AUDIO_RATE> envelope[VOICE_COUNT];

volatile int cachedShape = 0;
volatile int cachedEnvIndex = 0;
int8_t octaveShift = 0;
int8_t lastOctaveShift = 0;

int cachedArpIndex = 0;
uint8_t arpStep = 0;
unsigned long arpLastMs = 0;
float arpRateHz = 4.0f;
int8_t userArpSteps[4] = {0, 3, 7, 10};
uint8_t userArpWritePos = 0;

// Drum objects
bool drumActive[DRUM_ROWS] = {false};
unsigned long drumTrigMs[DRUM_ROWS] = {0};
float kickFreqCurrent = 80.0f;
float snareFreqCurrent = 200.0f;
float hatFreqCurrent = 2400.0f;
float clapFreqCurrent = 1600.0f;
uint8_t drumMixAmount = 86;
float drumGlobalGain = 10.0f;  // Gain drums vs instruments

Oscil<SIN2048_NUM_CELLS, AUDIO_RATE> drumSinKick(SIN2048_DATA);
Oscil<SIN2048_NUM_CELLS, AUDIO_RATE> drumSinSnare(SIN2048_DATA);
Oscil<SIN2048_NUM_CELLS, AUDIO_RATE> drumSinHat(SIN2048_DATA);
Oscil<SIN2048_NUM_CELLS, AUDIO_RATE> drumSinClap(SIN2048_DATA);
ADSR<CONTROL_RATE, AUDIO_RATE> drumEnv[DRUM_ROWS];

#if ENABLE_BURROUGHS_KICK_SAMPLE
Sample<BURROUGHS1_18649_NUM_CELLS, AUDIO_RATE> drumSample(BURROUGHS1_18649_DATA);
volatile bool drumSampleActive = false;
volatile uint8_t drumSampleRole = 255;
#endif

bool drumPattern[DRUM_ROWS][DRUM_MAX_STEPS] = {false};
uint8_t drumStep = 0;
unsigned long lastStepMs = 0;
bool drumRun = false;
int currentDrumBank = 0;
float drumPitch = 1.0f;  // 0.5 - 1.5 range
float drumAmplitude = 1.0f;  // 0.5 - 1.5 range (volume)
uint8_t drumDivisionIndex = 1;  // default 1/8
uint8_t drumEditPage = 0;

// Effects objects
int cachedEffectIndex = 0;
bool effectEnabled[EFFECT_COUNT] = {false};
uint8_t fxAmount = 128;

LowPassFilter lpf;
StateVariable<LOWPASS> resoEchoFilter;
ResonantFilter<LOWPASS> acidFilter;
Oscil<COS512_NUM_CELLS, AUDIO_RATE> fxLfo(COS512_DATA);
Oscil<COS2048_NUM_CELLS, AUDIO_RATE> flangerLfo(COS2048_DATA);
Oscil<COS512_NUM_CELLS, AUDIO_RATE> tremoloLfo(COS512_DATA);
Oscil<COS512_NUM_CELLS, AUDIO_RATE> lfoModSine(COS512_DATA);    // LFO modulation sine
Oscil<COS512_NUM_CELLS, AUDIO_RATE> lfoModSquare(COS512_DATA);  // LFO modulation square

int16_t delayBuffer[DELAY_BUFFER_SIZE] = {0};
int delayWriteIndex = 0;

// Independent FX runtime for locked loop layer.
LowPassFilter lockLpf;
StateVariable<LOWPASS> lockResoEchoFilter;
ResonantFilter<LOWPASS> lockAcidFilter;
Oscil<COS512_NUM_CELLS, AUDIO_RATE> lockFxLfo(COS512_DATA);
Oscil<COS2048_NUM_CELLS, AUDIO_RATE> lockFlangerLfo(COS2048_DATA);
Oscil<COS512_NUM_CELLS, AUDIO_RATE> lockTremoloLfo(COS512_DATA);
int16_t lockDelayBuffer[DELAY_BUFFER_SIZE] = {0};
int lockDelayWriteIndex = 0;
int16_t liveHpfX1 = 0;
int16_t liveHpfY1 = 0;
int liveBcCount = 0;
int16_t liveBcHold = 0;
int16_t liveEnvFollow = 0;
int16_t liveSubLast = 0;
int8_t liveSubPol = 1;
int16_t liveSubLp = 0;
int liveRandCount = 0;
int16_t liveRandHold = 0;
uint32_t liveRandSeed = 0x13579BDFUL;
uint32_t liveHarmPhaseQ8 = 0;
int16_t lockHpfX1 = 0;
int16_t lockHpfY1 = 0;
int lockBcCount = 0;
int16_t lockBcHold = 0;
int16_t lockEnvFollow = 0;
int16_t lockSubLast = 0;
int8_t lockSubPol = 1;
int16_t lockSubLp = 0;
int lockRandCount = 0;
int16_t lockRandHold = 0;
uint32_t lockRandSeed = 0x2468ACE1UL;
uint32_t lockHarmPhaseQ8 = 0;

// ==================== BUTTON STATE ====================
bool pressed[TOTAL_BUTTONS] = {false};
bool prevPressed[TOTAL_BUTTONS] = {false};
bool justPressed[TOTAL_BUTTONS] = {false};
bool justReleased[TOTAL_BUTTONS] = {false};
bool ledRefreshRequested = false;
bool ledImmediateRequested = false;
bool displayRefreshRequested = false;

volatile uint32_t scannedMainMask = 0;
volatile uint8_t scannedExtraMask = 0;
volatile bool scannedButtonsDirty = false;
uint32_t consumedMainMask = 0;
uint8_t consumedExtraMask = 0;
portMUX_TYPE buttonMaskMux = portMUX_INITIALIZER_UNLOCKED;

// ==================== TASKS ====================
TaskHandle_t buttonScanTaskHandle = nullptr;
TaskHandle_t uiTaskHandle = nullptr;

unsigned long lastLedMs = 0;
unsigned long lastDisplayMs = 0;
unsigned long lastUserActivityMs = 0;
bool standbyActive = false;
uint8_t noteEnergyQ8 = 0;
uint8_t noteHoldProgressQ8 = 0;

static void setStandby(bool enable) {
  if (standbyActive == enable) return;
  standbyActive = enable;
#if !SAFE_MODE
  if (enable) {
    strip.clear();
    strip.show();
    u8g2.setPowerSave(1);
  } else {
    u8g2.setPowerSave(0);
    ledRefreshRequested = true;
    displayRefreshRequested = true;
  }
#endif
}

// ============================================================
//  BUTTON HANDLING
// ============================================================
void scanButtons() {
  uint16_t b1 = mcp1.readGPIOAB();
  uint16_t b2 = mcp2.readGPIOAB();
  uint32_t nextMainMask = 0;
  for (int i = 0; i < MAIN_BUTTONS; i++) {
    bool raw = (i < 16) ? !(b1 & (1 << i)) : !(b2 & (1 << (i - 16)));
    int row = i / COLS;
    int col = (COLS - 1) - (i % COLS);
    int key = row * COLS + col;
    if (raw) nextMainMask |= (1UL << key);
  }
  uint16_t bx = mcpExtra.readGPIOAB();
  uint8_t nextExtraMask = 0;
  for (int i = 0; i < EXTRA_BUTTONS; i++) {
    bool raw = !(bx & (1 << extraMcpPins[i]));
    if (raw) nextExtraMask |= (1U << i);
  }

  portENTER_CRITICAL(&buttonMaskMux);
  bool changed = (nextMainMask != scannedMainMask) || (nextExtraMask != scannedExtraMask);
  scannedMainMask = nextMainMask;
  scannedExtraMask = nextExtraMask;
  if (changed) scannedButtonsDirty = true;
  portEXIT_CRITICAL(&buttonMaskMux);
}

void consumeButtonEvents() {
  uint32_t latestMainMask;
  uint8_t latestExtraMask;
  bool hadChanges;

  portENTER_CRITICAL(&buttonMaskMux);
  latestMainMask = scannedMainMask;
  latestExtraMask = scannedExtraMask;
  hadChanges = scannedButtonsDirty;
  scannedButtonsDirty = false;
  portEXIT_CRITICAL(&buttonMaskMux);

  uint32_t changedMainMask = latestMainMask ^ consumedMainMask;
  uint8_t changedExtraMask = latestExtraMask ^ consumedExtraMask;

  for (int i = 0; i < TOTAL_BUTTONS; i++) {
    prevPressed[i] = pressed[i];
    justPressed[i] = false;
    justReleased[i] = false;
  }

  for (int key = 0; key < MAIN_BUTTONS; key++) {
    bool isPressed = ((latestMainMask >> key) & 0x1U) != 0;
    bool changed = ((changedMainMask >> key) & 0x1U) != 0;
    pressed[key] = isPressed;
    justPressed[key] = changed && isPressed;
    justReleased[key] = changed && !isPressed;
  }

  for (int i = 0; i < EXTRA_BUTTONS; i++) {
    int key = MAIN_BUTTONS + i;
    bool isPressed = ((latestExtraMask >> i) & 0x1U) != 0;
    bool changed = ((changedExtraMask >> i) & 0x1U) != 0;
    pressed[key] = isPressed;
    justPressed[key] = changed && isPressed;
    justReleased[key] = changed && !isPressed;
  }

  consumedMainMask = latestMainMask;
  consumedExtraMask = latestExtraMask;
  if (hadChanges || changedMainMask != 0 || changedExtraMask != 0) {
    lastUserActivityMs = millis();
    if (standbyActive) setStandby(false);
    ledImmediateRequested = true;
    ledRefreshRequested = true;
  }
}

// ============================================================
//  TASKS
// ============================================================
void buttonScanTask(void* parameter) {
  (void)parameter;
  Serial.println("[TASK] buttonScanTask started");
  for (;;) {
    scanButtons();
    vTaskDelay(standbyActive ? BUTTON_SCAN_STANDBY_DELAY_TICKS : BUTTON_SCAN_TASK_DELAY_TICKS);
  }
}

void uiTask(void* parameter) {
  (void)parameter;
  Serial.println("[TASK] uiTask started");
  int lastPotSnapshot[POT_COUNT] = {0};
  for (;;) {
    unsigned long now = millis();

    if (!standbyActive && (now - lastUserActivityMs >= STANDBY_TIMEOUT_MS)) {
      setStandby(true);
    }

    if (now - lastPotReadMs >= 8) {
      lastPotReadMs = now;
      readPots();
      for (int i = 0; i < POT_COUNT; i++) {
        if (abs(potFilt[i] - lastPotSnapshot[i]) > 28) {
          lastUserActivityMs = now;
          if (standbyActive) setStandby(false);
        }
        lastPotSnapshot[i] = potFilt[i];
      }
    }

    if (standbyActive) {
      vTaskDelay(UI_TASK_DELAY_TICKS);
      continue;
    }

#if !SAFE_MODE
    bool ledEdge = ledImmediateRequested;
    if ((ledEdge || ledRefreshRequested) && (ledEdge || lastLedMs == 0 || (now - lastLedMs >= 2))) {
      lastLedMs = now;
      ledImmediateRequested = false;
      ledRefreshRequested = false;
      renderLeds();
    }

    if (displayRefreshRequested && (now - lastDisplayMs >= 120)) {
      lastDisplayMs = now;
      displayRefreshRequested = false;
      renderDisplay();
    }
#endif

    vTaskDelay(UI_TASK_DELAY_TICKS);
  }
}

// ============================================================
//  MOZZI CALLBACKS
// ============================================================
void updateControl() {
  // Call module update functions
  updateSynthControl();
  updateDrumsControl();
  runDrumSequencer();

  int activeGates = 0;
  unsigned long youngestNoteOnMs = 0UL;
  for (int i = 0; i < VOICE_COUNT; i++) {
    if (voices[i].active && voices[i].gate) {
      activeGates++;
      if (voiceNoteOnMs[i] > youngestNoteOnMs) youngestNoteOnMs = voiceNoteOnMs[i];
    }
  }
  uint8_t target = (uint8_t)constrain(activeGates * 64, 0, 255);
  noteEnergyQ8 = (uint8_t)(((uint16_t)noteEnergyQ8 * 220U + (uint16_t)target * 36U) >> 8);

  // LPF progression driven by age of most recently triggered held note.
  // This retriggers LPF-Up/Down sweep on each new note-on.
  if (activeGates > 0) {
    unsigned long ageMs = millis() - youngestNoteOnMs;
    const unsigned long fullSweepMs = 850UL;
    unsigned long q8 = (ageMs * 255UL) / fullSweepMs;
    if (q8 > 255UL) q8 = 255UL;
    noteHoldProgressQ8 = (uint8_t)q8;
  } else {
    noteHoldProgressQ8 = 0;
  }
  
  // Request periodic LED/display updates
  static uint8_t uiTick = 0;
  static uint8_t dispTick = 0;
  
  if (++uiTick >= UI_TICKS_LEDS) {
    uiTick = 0;
    ledRefreshRequested = true;
  }
  
  if (++dispTick >= UI_TICKS_DISP) {
    dispTick = 0;
    displayRefreshRequested = true;
  }
}

static inline int16_t matchPerceivedLevel(int16_t dry, int16_t wet) {
  int32_t ad = abs((int)dry);
  int32_t aw = abs((int)wet);
  if (aw < 4) return wet;

  // Keep effect timbre, but nudge amplitude toward dry signal loudness.
  int32_t gainQ8 = ((ad + 128) << 8) / (aw + 128);
  gainQ8 = constrain(gainQ8, 96, 320);  // 0.375x .. 1.25x
  int32_t leveled = ((int32_t)wet * gainQ8) >> 8;
  return (int16_t)constrain(leveled, -32767, 32767);
}

static inline int16_t matchPerceivedLevelNoBoost(int16_t dry, int16_t wet) {
  int32_t ad = abs((int)dry);
  int32_t aw = abs((int)wet);
  if (aw < 4) return wet;

  // Same perceptual leveling, but never louder than the dry input.
  int32_t gainQ8 = ((ad + 128) << 8) / (aw + 128);
  gainQ8 = constrain(gainQ8, 96, 256);  // 0.375x .. 1.0x
  int32_t leveled = ((int32_t)wet * gainQ8) >> 8;
  return (int16_t)constrain(leveled, -32767, 32767);
}

static inline int16_t softClipInt16(int32_t x, int16_t knee) {
  int16_t safeKnee = (knee < 512) ? 512 : knee;
  int32_t absx = (x < 0) ? -x : x;
  int32_t shaped = (x * (int32_t)safeKnee) / (absx + (int32_t)safeKnee);
  return (int16_t)constrain(shaped, -32767, 32767);
}

static inline float fastSemitoneRatio(float semi) {
  // Approximation of pow(2, semi/12) via table + linear interpolation.
  // Much lighter than powf() in the audio loop.
  static const float kSemiRatio[13] = {
    1.000000f, 1.059463f, 1.122462f, 1.189207f,
    1.259921f, 1.334840f, 1.414214f, 1.498307f,
    1.587401f, 1.681793f, 1.781797f, 1.887749f,
    2.000000f
  };
  if (semi <= 0.0f) return 1.0f;
  if (semi >= 12.0f) return 2.0f;
  int idx = (int)semi;
  float frac = semi - (float)idx;
  return kSemiRatio[idx] + (kSemiRatio[idx + 1] - kSemiRatio[idx]) * frac;
}

static int16_t applyEffectsChain(
  int16_t input,
  const bool fxMask[EFFECT_COUNT],
  uint8_t fxAmt,
  uint8_t polyLoad,
  LowPassFilter& lpfRef,
  StateVariable<LOWPASS>& resoEchoFilterRef,
  ResonantFilter<LOWPASS>& acidFilterRef,
  Oscil<COS512_NUM_CELLS, AUDIO_RATE>& fxLfoRef,
  Oscil<COS2048_NUM_CELLS, AUDIO_RATE>& flangerLfoRef,
  Oscil<COS512_NUM_CELLS, AUDIO_RATE>& tremoloLfoRef,
  Oscil<COS512_NUM_CELLS, AUDIO_RATE>& lfoModSineRef,
  Oscil<COS512_NUM_CELLS, AUDIO_RATE>& lfoModSquareRef,
  int16_t* delayBuf,
  int& delayIdx,
  int16_t& hpfX1,
  int16_t& hpfY1,
  int& bcCount,
  int16_t& bcHold,
  int16_t& envFollow,
  int16_t& subLast,
  int8_t& subPol,
  int16_t& subLp,
  int& randCount,
  int16_t& randHold,
  uint32_t& randSeed,
  uint32_t& harmPhaseQ8
) {
  int16_t output = input;
  uint8_t fxP1 = fxParam1;
  uint8_t fxP2 = fxParam2;

  const int bpmSafe = max(1, (int)bpm);
  const int beatSamples = max(1, (AUDIO_RATE * 60) / bpmSafe);

  // Update oscillator frequencies only when targets change to reduce audio-rate overhead.
  static int lastBpmForFx = -1;
  static int lastFxAmtForFlanger = -1;
  static int lastLfoSinFreqParam = -1;
  static int lastLfoSqrFreqParam = -1;

  if (lastBpmForFx != bpmSafe || lastFxAmtForFlanger != fxAmt) {
    fxLfoRef.setFreq(0.4f + ((float)bpmSafe / 60.0f) * 0.5f);
    flangerLfoRef.setFreq(0.05f + ((float)bpmSafe / 180.0f) * ((float)fxAmt / 192.0f));
    tremoloLfoRef.setFreq(2.5f + ((float)bpmSafe / 60.0f) * 1.5f);
    lastBpmForFx = bpmSafe;
    lastFxAmtForFlanger = fxAmt;
  }
  if (lastLfoSinFreqParam != (int)lfoSineFreqParam) {
    lfoModSineRef.setFreq(0.5f + ((float)lfoSineFreqParam / 255.0f) * 4.5f);      // 0.5-5.0 Hz
    lastLfoSinFreqParam = lfoSineFreqParam;
  }
  if (lastLfoSqrFreqParam != (int)lfoSquareFreqParam) {
    lfoModSquareRef.setFreq(0.5f + ((float)lfoSquareFreqParam / 255.0f) * 4.5f);  // 0.5-5.0 Hz
    lastLfoSqrFreqParam = lfoSquareFreqParam;
  }

  // Pre-calculate LFO modulation values if they are active
  int16_t lfoSineVal = 0;
  int16_t lfoSquareVal = 0;
  if (fxMask[16]) {  // LFO_Sin is enabled
    lfoSineVal = lfoModSineRef.next();
  }
  if (fxMask[24]) {  // LFO_Sqr is enabled
    lfoSquareVal = lfoModSquareRef.next();
  }

  for (uint8_t fx = 0; fx < EFFECT_COUNT; fx++) {
    if (fx == 0 || !fxMask[fx]) continue;
    
    // Determine current P1 with LFO modulation if applicable
    uint8_t effectiveP1 = fxP1;
    if (fxMask[16] && lfoSineTargetEffect == fx) {  // LFO_Sin modulates this effect
      int lfoDepth = ((int)lfoSineDepthParam * 180) / 255;
      int delta = (lfoSineVal * lfoDepth) >> 8;
      effectiveP1 = (uint8_t)constrain((int)fxP1 + delta, 0, 255);
    } else if (fxMask[24] && lfoSquareTargetEffect == fx) {  // LFO_Sqr modulates this effect
      int lfoDepth = ((int)lfoSquareDepthParam * 200) / 255;
      int16_t squareVal = (lfoSquareVal >= 0) ? 100 : -100;
      int delta = (squareVal * lfoDepth) >> 8;
      effectiveP1 = (uint8_t)constrain((int)fxP1 + delta, 0, 255);
    }
    
    // Use effectiveP1 instead of fxP1 for this effect
    uint8_t workP1 = effectiveP1;
    
    switch (fx) {
      case 1: {
        uint8_t cutoff = (uint8_t)(18 + ((uint16_t)fxAmt * 180U) / 255U);
        uint8_t resonance = (uint8_t)(90 + ((uint16_t)workP1 * 130U) / 255U);
        int32_t driven = ((int32_t)output * (128 + fxP2)) >> 7;
        lpfRef.setResonance(resonance);
        lpfRef.setCutoffFreq(cutoff);
        output = lpfRef.next((int16_t)constrain(driven, -32767, 32767));
        break;
      }
      case 2: {
        // Integer HPF variant: less CPU than per-sample float divisions.
        int cutoffCtl = 16 + ((int)fxAmt * 180) / 255 + ((int)workP1 * 48) / 255;
        int aQ15 = 32700 - cutoffCtl * 110;
        aQ15 = constrain(aQ15, 4000, 32000);
        int32_t hpIn = (int32_t)hpfY1 + (int32_t)output - (int32_t)hpfX1;
        int16_t y = (int16_t)constrain((hpIn * aQ15) >> 15, -32767, 32767);
        hpfX1 = output;
        hpfY1 = y;
        output = compress16(((int32_t)y * (128 + fxP2)) >> 7, 22000, 2);
        break;
      }
      case 3: {
        int delaySamples = constrain(beatSamples / 2, 200, DELAY_BUFFER_SIZE - 1);
        int readIdx = (delayIdx - delaySamples + DELAY_BUFFER_SIZE) % DELAY_BUFFER_SIZE;
        int16_t delayed = delayBuf[readIdx];
        int fb = 48 + ((int)workP1 * 150) / 255;
        int wet = 30 + ((int)fxP2 * 180) / 255;
        delayBuf[delayIdx] = (int16_t)constrain((int)output + ((int)delayed * fb >> 8), -32767, 32767);
        output = (int16_t)((int)output + (((int)delayed * wet) >> 8));
        break;
      }
      case 4: {
        int delaySamples = constrain((beatSamples * 3) / 4, 320, DELAY_BUFFER_SIZE - 1);
        int readIdx = (delayIdx - delaySamples + DELAY_BUFFER_SIZE) % DELAY_BUFFER_SIZE;
        int16_t delayed = delayBuf[readIdx];
        int toneDamp = 8 + ((int)fxP2 * 120) / 255;
        int16_t filtered = delayed - ((delayed * toneDamp) >> 8);
        int fb = 110 + ((int)workP1 * 120) / 255;
        int wet = 120 + ((int)fxAmt * 120) / 255;
        int32_t wr = (int32_t)output + (((int32_t)filtered * fb) >> 8);
        delayBuf[delayIdx] = compress16(wr, 22000, 2);
        int32_t mix = (int32_t)output + (((int32_t)filtered * wet) >> 8);
        output = compress16(mix, 25000, 2);
        break;
      }
      case 5: {
        // Stronger phaser with deeper notch and richer wet blend.
        int16_t lfo = fxLfoRef.next();
        int base = 14;
        int depth = 24 + ((int)fxAmt * 2) / 5 + ((int)workP1 * 64) / 255;
        int d = base + (((lfo + 128) * depth) >> 8);
        int readIdx = (delayIdx - d + DELAY_BUFFER_SIZE) % DELAY_BUFFER_SIZE;
        int16_t tap = delayBuf[readIdx];

        int feedback = 56 + ((int)fxP2 * 160) / 255;
        int32_t wr = (int32_t)output + (((int32_t)tap * feedback) >> 8);
        delayBuf[delayIdx] = compress16(wr, 21000, 2);

        int32_t notch = (int32_t)output - (((int32_t)tap * 220) >> 8);
        int32_t wetMix = ((notch * 210) + ((int32_t)output * 58)) >> 8;
        output = compress16(wetMix, 21000, 2);
        break;
      }
      case 6: {
        // Ladder: ultra-resonant ladder with pronounced feedback character.
        int16_t dryIn = output;
        uint8_t cutoff = (uint8_t)(4 + ((uint16_t)fxAmt * 236U) / 255U);
        uint8_t resonance = (uint8_t)(170 + ((uint16_t)workP1 * 85U) / 255U);

        int driveMul = 2 + (fxP2 >> 4);  // 2..17x
        int32_t driven = (int32_t)output * driveMul;
        int16_t preSat = softClipInt16(driven, (int16_t)(9800 - ((driveMul - 2) * 420)));

        lpfRef.setResonance(resonance);
        lpfRef.setCutoffFreq(cutoff);
        int16_t filtered = lpfRef.next(preSat);

        int16_t resonant = softClipInt16((int32_t)filtered * (190 + (workP1 >> 1)) / 128, 8600);
        int16_t leveled = matchPerceivedLevelNoBoost(dryIn, resonant);
        int16_t wetOut = (int16_t)((((int32_t)leveled) * 194) >> 8);
        output = (int16_t)constrain((((int32_t)dryIn * (255 - fxAmt)) + ((int32_t)wetOut * fxAmt)) >> 8, -32767, 32767);
        break;
      }
      case 7: {
        int delaySamples = constrain(beatSamples / 4, 90, DELAY_BUFFER_SIZE - 1);
        int readIdx = (delayIdx - delaySamples + DELAY_BUFFER_SIZE) % DELAY_BUFFER_SIZE;
        int16_t delayed = delayBuf[readIdx];
        uint16_t centre = 140 + ((uint16_t)fxAmt * 2200U) / 255U;
        uint8_t resonance = (uint8_t)(110 + ((uint16_t)workP1 * 140U) / 255U);
        resoEchoFilterRef.setCentreFreq(centre);
        resoEchoFilterRef.setResonance(resonance);

        int16_t filtered = (int16_t)resoEchoFilterRef.next(delayed);
        int fb = 96 + ((int)fxP2 * 140) / 255;
        int wet = 100 + ((int)fxAmt * 130) / 255;

        int32_t fbSample = (int32_t)output + (((int32_t)filtered * fb) >> 8);
        delayBuf[delayIdx] = (int16_t)constrain(fbSample, -32767, 32767);

        int32_t wetMix = (int32_t)output + (((int32_t)filtered * wet) >> 8);
        output = (int16_t)constrain(wetMix, -32767, 32767);
        break;
      }
      case 8: {
        int16_t dryIn = output;
        // Poly-safe path for Acid when many notes are active.
        int16_t preIn = (polyLoad >= 4) ? compress16(dryIn, 17000, 3) : dryIn;
        uint8_t cutoffBase = (uint8_t)(18 + ((uint16_t)fxAmt * 132U) / 255U);
        uint8_t resonance = (uint8_t)(160 + ((uint16_t)workP1 * 95U) / 255U);
        if (polyLoad >= 4) resonance = (uint8_t)constrain((int)resonance - 26, 0, 255);
        int16_t lfo = fxLfoRef.next();
        int cutoffMod = (lfo * (16 + (fxAmt >> 3))) >> 7;

        // 303-ish accent: rhythmic push tied to the sequencer step.
        bool accent = ((drumStep & 0x3) == 0) || ((drumStep & 0x7) == 5);
        int accentBoost = accent ? (8 + (fxAmt >> 4)) : 0;
        uint8_t cutoff = (uint8_t)constrain((int)cutoffBase + cutoffMod + accentBoost, 6, 210);
        uint8_t resonanceAccent = (uint8_t)constrain((int)resonance + (accent ? 12 : 0), 0, 255);

        // Acid is sensitive to stepped coefficient updates; keep full-rate updates.
        acidFilterRef.setCutoffFreqAndResonance(cutoff, resonanceAccent);

        int driveMul = 2 + (fxP2 >> 5) + (accent ? 1 : 0);
        if (polyLoad >= 4) driveMul = 1 + (fxP2 >> 6);
        int32_t driveIn = (int32_t)preIn * driveMul;
        int16_t filtered = (int16_t)acidFilterRef.next((int16_t)constrain(driveIn, -26000, 26000));
        int16_t acidOut;
        if (polyLoad >= 4) {
          // Simpler output stage: lower CPU and less risk of crackles.
          acidOut = compress16(((int32_t)filtered * 176) >> 8, 20500, 2);
        } else {
          int16_t grit = softClipInt16((int32_t)filtered * (170 + (fxAmt >> 1)) / 128, (int16_t)(13000 - (fxAmt * 25)));
          acidOut = (int16_t)constrain((((int32_t)filtered * 180) + ((int32_t)grit * 76)) >> 8, -32767, 32767);
        }
        int16_t wetOut = compress16(((int32_t)acidOut * 182) >> 8, 22000, 2);
        output = (int16_t)constrain((((int32_t)dryIn * (255 - fxAmt)) + ((int32_t)wetOut * fxAmt)) >> 8, -32767, 32767);
        break;
      }
      case 9: {
        // Fuzz: abrasive punk character with asymmetric hard clipping.
        int16_t dryIn = output;
        int16_t preIn = (polyLoad >= 4) ? compress16(dryIn, 17500, 3) : dryIn;
        int drive = 4 + ((int)workP1 * 14) / 255;
        if (polyLoad >= 4) drive = 3 + ((int)workP1 * 7) / 255;
        int32_t raw = (int32_t)preIn * drive;
        int bias = ((int)fxP2 - 128) * 22;
        raw += (polyLoad >= 4) ? (bias >> 1) : bias;

        int32_t hiClip = 5200 + ((int32_t)(255 - fxP2) * 24);
        int32_t loClip = 8600 + ((int32_t)fxP2 * 18);
        if (polyLoad >= 4) {
          hiClip += 1800;
          loClip += 1800;
        }
        if (raw > hiClip) raw = hiClip;
        if (raw < -loClip) raw = -loClip;

        int16_t clipped = (int16_t)constrain(raw, -32767, 32767);
        int16_t rasp = (polyLoad >= 4)
          ? clipped
          : softClipInt16((int32_t)clipped * (210 + (workP1 >> 2)) / 128, 7600);
        int punkBlend = 120 + ((int)fxP2 * 120) / 255;
        int16_t fuzzOut = (int16_t)constrain((((int32_t)clipped * (255 - punkBlend)) + ((int32_t)rasp * punkBlend)) >> 8, -32767, 32767);
        int16_t wetOut;
        if (polyLoad >= 4) {
          // Skip expensive per-sample leveling when dense chords are active.
          wetOut = compress16(((int32_t)fuzzOut * 184) >> 8, 20800, 2);
        } else {
          int16_t leveled = matchPerceivedLevelNoBoost(dryIn, fuzzOut);
          wetOut = (int16_t)((((int32_t)leveled) * 205) >> 8);
        }
        output = (int16_t)constrain((((int32_t)dryIn * (255 - fxAmt)) + ((int32_t)wetOut * fxAmt)) >> 8, -32767, 32767);
        break;
      }
      case 10: {
        int base = 28;
        int depth = 16 + ((int)workP1 * 72) / 255;
        int lfo = (flangerLfoRef.next() + 128);
        int delaySamples = base + ((lfo * depth) >> 8);
        int readIdx = (delayIdx - delaySamples + DELAY_BUFFER_SIZE) % DELAY_BUFFER_SIZE;
        int16_t delayed = delayBuf[readIdx];
        int fb = 32 + ((int)fxP2 * 150) / 255;
        int32_t wr = (int32_t)output + (((int32_t)delayed * fb) >> 8);
        delayBuf[delayIdx] = compress16(wr, 22000, 2);
        int32_t mix = (int32_t)output + (((int32_t)delayed * 150) >> 8);
        output = compress16(mix, 23000, 2);
        break;
      }
      case 11: {
        // Overdrive: poly-safe warm saturation (lighter CPU + controlled headroom).
        int16_t dryIn = output;
        // Pre-limit avoids harsh spikes when several voices hit at once.
        int16_t preLimited = compress16(dryIn, (polyLoad >= 2) ? 15500 : 18000, (polyLoad >= 2) ? 3 : 2);
        int drive = 2 + ((int)workP1 * 6) / 255;  // drive factor 2..8
        if (polyLoad >= 2) drive = 2 + ((int)workP1 * 4) / 255;
        int32_t driveIn = (int32_t)preLimited * drive;
        int16_t driven = softClipInt16(driveIn, (int16_t)((polyLoad >= 2 ? 16500 : 15000) - ((int)workP1 * (polyLoad >= 2 ? 12 : 20))));

        // Tone control: fxP2 = 0 bright, 255 dark (high damping, no extra nonlinear stage).
        int toneDamp = 14 + ((int)fxP2 * 104) / 255;
        int16_t toned = driven - ((driven * toneDamp) >> 8);

        // Fixed gain trim keeps volume close to dry and removes expensive per-sample leveling.
        int16_t wetOut = compress16(((int32_t)toned * (polyLoad >= 2 ? 162 : 176)) >> 8, (polyLoad >= 2) ? 19500 : 21000, 2);
        output = (int16_t)constrain((((int32_t)dryIn * (255 - fxAmt)) + ((int32_t)wetOut * fxAmt)) >> 8, -32767, 32767);
        break;
      }
      case 12: {
        int16_t dryIn = output;
        int hold = 1 + (fxAmt >> 4) + (workP1 >> 4);
        if (++bcCount >= hold) { bcCount = 0; bcHold = output; }
        int crushShift = 1 + ((255 - (int)fxP2) >> 6);
        int16_t crushed = (int16_t)((bcHold >> crushShift) << crushShift);
        output = (int16_t)constrain((((int32_t)dryIn * (255 - fxAmt)) + ((int32_t)crushed * fxAmt)) >> 8, -32767, 32767);
        break;
      }
      case 13: {
        // Distortion removed.
        output = output;
        break;
      }
      case 14: {
        // AutoWah: envelope follower + subtle LFO movement.
        int32_t env = abs((int)output);
        uint8_t envPush = (uint8_t)constrain((int)((env * (80 + workP1)) >> 14), 0, 180);
        int16_t lfoVal = tremoloLfoRef.next();
        int lfoPush = ((int)lfoVal * (10 + (fxAmt >> 4))) >> 7;
        uint8_t baseCutoff = 16 + ((uint16_t)fxAmt * 120U) / 255U;
        uint8_t cutoff = (uint8_t)constrain((int)baseCutoff + (int)envPush + lfoPush, 8, 250);
        uint8_t wahReso = (uint8_t)(120 + ((uint16_t)fxP2 * 120U) / 255U);
        lpfRef.setResonance(wahReso);
        lpfRef.setCutoffFreq(cutoff);
        output = lpfRef.next(output);
        break;
      }
      case 15: {
        // Lightweight plate-like reverb from 3 decorrelated taps.
        int sizeQ8 = 154 + ((int)workP1 * 282) / 255;  // 0.60..1.70 in Q8
        int d1 = constrain((beatSamples * 17 * sizeQ8) / (100 * 256), 110, DELAY_BUFFER_SIZE - 1);
        int d2 = constrain((beatSamples * 29 * sizeQ8) / (100 * 256), 190, DELAY_BUFFER_SIZE - 1);
        int d3 = constrain((beatSamples * 41 * sizeQ8) / (100 * 256), 260, DELAY_BUFFER_SIZE - 1);
        int16_t t1 = delayBuf[(delayIdx - d1 + DELAY_BUFFER_SIZE) % DELAY_BUFFER_SIZE];
        int16_t t2 = delayBuf[(delayIdx - d2 + DELAY_BUFFER_SIZE) % DELAY_BUFFER_SIZE];
        int16_t t3 = delayBuf[(delayIdx - d3 + DELAY_BUFFER_SIZE) % DELAY_BUFFER_SIZE];

        int tone = 40 + ((int)fxP2 * 140) / 255;
        int32_t tail = ((int32_t)t1 * (160 - tone) + (int32_t)t2 * 86 + (int32_t)t3 * tone) >> 8;
        int fb = 120 + (fxAmt >> 2);
        delayBuf[delayIdx] = compress16((int32_t)output + ((tail * fb) >> 8), 22000, 2);

        int wet = 72 + (fxAmt >> 1);
        output = compress16((int32_t)output + ((tail * wet) >> 8), 22000, 2);
        break;
      }
      case 16: {
        // LFO_Sin: modulation marker (does nothing visible by itself)
        // Modulates the target effect's parameter via workP1
        output = output;  // passthrough
        break;
      }
      case 17: {
        // AutoPan in mono: animated left/right illusion via dynamic amplitude swing.
        int16_t lfo = fxLfoRef.next();
        int depth = 32 + ((int)fxP2 * 180) / 255;
        int gain = (60 + ((int)workP1 * 120) / 255) + (((255 - depth) * (lfo + 128)) >> 8);
        if (gain > 255) gain = 255;
        output = (int16_t)constrain(((int32_t)output * gain) >> 8, -32767, 32767);
        break;
      }
      case 18: {
        // Ring modulator (lo-fi): bipolar mod at pseudo-audio rate.
        int modRate = 4 + ((int)workP1 * 40) / 255;
        int16_t mod = (int16_t)((((delayIdx * modRate) & 0xFF) - 128));
        int16_t ring = (int16_t)(((int32_t)output * (int32_t)mod) >> 7);
        int mix = 40 + ((int)fxP2 * 200) / 255;
        output = (int16_t)constrain((((int32_t)output * (255 - mix)) + ((int32_t)ring * mix)) >> 8, -32767, 32767);
        break;
      }
      case 19: {
        // LPFDown: cutoff closes progressively while the note is held.
        int prog = (int)noteHoldProgressQ8;  // 0..255
        int openCut = 168 + ((int)fxAmt * 60) / 255;
        int closeCut = 8 + ((int)workP1 * 72) / 255;
        int sweep = ((openCut - closeCut) * prog) >> 8;
        int dyn = abs((int)output) >> 9;
        int cutoff = openCut - sweep + dyn;
        int reso = 90 + ((int)fxP2 * 120) / 255;
        lpfRef.setResonance((uint8_t)constrain(reso, 0, 255));
        lpfRef.setCutoffFreq((uint8_t)constrain(cutoff, 9, 238));
        output = lpfRef.next(output);
        break;
      }
      case 20: {
        // LPFUp: cutoff opens progressively while the note is held.
        int prog = (int)noteHoldProgressQ8;  // 0..255
        int base = 8 + ((int)fxAmt * 30) / 255;
        int top = 60 + ((int)workP1 * 180) / 255;
        int sweep = ((top - base) * prog) >> 8;
        int dyn = abs((int)output) >> 10;
        int cutoff = base + sweep + dyn;
        int reso = 90 + ((int)fxP2 * 130) / 255;
        lpfRef.setResonance((uint8_t)constrain(reso, 0, 255));
        lpfRef.setCutoffFreq((uint8_t)constrain(cutoff, 8, 240));
        output = lpfRef.next(output);
        break;
      }
      case 21: {
        // Harmonizer: pitch-shifted voice quantized to major pentatonic intervals.
        // Write current sample into delay buffer first so taps have fresh audio.
        delayBuf[delayIdx] = output;

        const int window = 80 + (fxAmt >> 1);  // 80..207 samples window
        // Q16 ratios for pentatonic semitones {0,2,4,7,9,12,14,16,19,21,24}
        static const uint32_t kPentatonicRatioQ16[] = {
          65536U, 73562U, 82570U, 98114U, 110218U, 131072U,
          147124U, 165140U, 196228U, 220436U, 262144U
        };
        int pentaIdx = ((int)workP1 * ((int)(sizeof(kPentatonicRatioQ16) / sizeof(kPentatonicRatioQ16[0])) - 1)) / 255;
        uint32_t stepQ16 = kPentatonicRatioQ16[pentaIdx];
        harmPhaseQ8 += stepQ16;
        // Wrap read pointer within [0, window)
        uint32_t spanQ16 = (uint32_t)window << 16;
        while (harmPhaseQ8 >= spanQ16) harmPhaseQ8 -= spanQ16;
        int pos = (int)(harmPhaseQ8 >> 16);
        int half = window >> 1;
        int pos2 = pos + half;
        if (pos2 >= window) pos2 -= window;

        int idxA = delayIdx - pos;
        if (idxA < 0) idxA += DELAY_BUFFER_SIZE;
        int idxB = delayIdx - pos2;
        if (idxB < 0) idxB += DELAY_BUFFER_SIZE;

        int16_t a = delayBuf[idxA];
        int16_t b = delayBuf[idxB];
        // Triangle crossfade between the two overlapping grains.
        int tri = (pos < half) ? ((pos * 255) / (half > 0 ? half : 1))
                               : (((window - pos) * 255) / (half > 0 ? half : 1));
        tri = constrain(tri, 0, 255);
        int16_t shifted = (int16_t)((((int32_t)a * tri) + ((int32_t)b * (255 - tri))) >> 8);

        // Blend shifted voice with dry at ~50% wet.
        int wet = 48 + ((int)fxP2 * 180) / 255;
        output = compress16(((int32_t)output * 200 + (int32_t)shifted * wet) >> 8, 22000, 2);
        break;
      }
      case 22: {
        // Sub harmonic generator: divide-like low octave reinforcement.
        if (output >= 0 && subLast < 0) subPol = (int8_t)-subPol;
        subLast = output;
        int16_t squareSub = (subPol > 0) ? 127 : -127;
        int target = ((int)squareSub << 8);
        int toneShift = 2 + ((255 - (int)workP1) >> 7);
        subLp += (int16_t)((target - subLp) >> toneShift);
        int subAmt = 96 + ((int)fxP2 * 220) / 255;
        int32_t subMix = (((int32_t)subLp * subAmt) >> 16);
        output = compress16((int32_t)output + subMix, 23000, 2);
        break;
      }
      case 23: {
        // Formant-like filter: vocal peak sweeping with subtle movement.
        int vowel = ((int)workP1 * 3) >> 8;
        int baseCut = 34;
        if (vowel == 1) baseCut = 56;
        else if (vowel == 2) baseCut = 86;
        else if (vowel >= 3) baseCut = 118;
        int16_t lfo = tremoloLfoRef.next();
        int cutoff = baseCut + ((lfo * (4 + (fxP2 >> 3))) >> 7);
        acidFilterRef.setCutoffFreqAndResonance((uint8_t)constrain(cutoff, 12, 200), (uint8_t)(190 + (fxAmt >> 3)));
        int16_t vowelBand = (int16_t)acidFilterRef.next(output);
        output = compress16((((int32_t)vowelBand * 196) + ((int32_t)output * 70)) >> 8, 22000, 2);
        break;
      }
      case 24: {
        // LFO_Sqr: modulation marker (does nothing visible by itself)
        // Modulates the target effect's parameter via workP1
        output = output;  // passthrough
        break;
      }
      case 25: {
        // Random modulation (sample&hold): random cutoff snapshots over time.
        int holdSamples = 12 + ((255 - (int)workP1) << 1);
        if (++randCount >= holdSamples) {
          randCount = 0;
          randSeed = randSeed * 1664525UL + 1013904223UL;
          randHold = (int16_t)((randSeed >> 24) - 128);
        }
        int base = 20 + ((int)fxAmt * 120) / 255;
        int cutoff = base + ((randHold * (18 + (fxAmt >> 3))) >> 7);
        lpfRef.setResonance((uint8_t)(90 + (fxP2 >> 1)));
        lpfRef.setCutoffFreq((uint8_t)constrain(cutoff, 8, 235));
        output = lpfRef.next(output);
        break;
      }
      case 26: {
        // Ladder2 removed.
        output = output;
        break;
      }
      case 27: {
        // Ladder3 removed (merged into Ladder).
        output = output;
        break;
      }
    }
  }

  delayIdx++;
  if (delayIdx >= DELAY_BUFFER_SIZE) delayIdx = 0;
  return output;
}

#if (MOZZI_COMPATIBILITY_LEVEL <= MOZZI_COMPATIBILITY_1_1) && MOZZI_IS(MOZZI_AUDIO_CHANNELS, MOZZI_MONO)
AudioOutput_t updateAudio() {
#else
AudioOutput updateAudio() {
#endif
  int32_t mixedSynthLive = 0;
  int32_t mixedSynthLoop = 0;
  int32_t mixedDrum = 0;
  uint8_t synthActiveLive = 0;
  uint8_t synthActiveLoop = 0;
  uint8_t drumCount = 0;
  static uint8_t pitchEnvUpdateDiv[VOICE_COUNT] = {0};

  // ── Voix synth ──
  for (uint8_t i = 0; i < VOICE_COUNT; i++) {
    if (!voices[i].active) continue;

    uint8_t env = envelope[i].next();

    // Déactiver quand enveloppe éteinte + gate relâché
    if (env == 0 && !voices[i].gate) {
      voices[i].active = false;
      continue;
    }

    if (voices[i].envMode == ENV_MODE_FADE && voices[i].gate) {
      voiceHoldGain[i] *= 0.9996f;
      if (voiceHoldGain[i] < 0.12f) voiceHoldGain[i] = 0.12f;
    } else if (!voices[i].gate) {
      voiceHoldGain[i] *= 0.9985f;
    }

    if (voices[i].envMode == ENV_MODE_PITCH && voicePitchEnvSemi[i] > 0.02f) {
      voicePitchEnvSemi[i] *= 0.9965f;
      // Update pitch less often and avoid powf() in audio-rate loop.
      if ((++pitchEnvUpdateDiv[i] & 0x01U) == 0U) {
        float f = voiceTargetFreq[i] * fastSemitoneRatio(voicePitchEnvSemi[i]);
        setVoiceFreq(i, f);
      }
      if (voicePitchEnvSemi[i] <= 0.03f) {
        voicePitchEnvSemi[i] = 0.0f;
        setVoiceFreq(i, voiceTargetFreq[i]);
      }
    } else {
      pitchEnvUpdateDiv[i] = 0;
    }

    int16_t sample = 0;
    // Per-voice shape allows frozen loop timbre while changing live instrument.
    uint8_t safeShape = (uint8_t)constrain((int)voices[i].shape, 0, SHAPE_COUNT - 1);
    uint8_t activeVoicesLoad = (uint8_t)(synthActiveLive + synthActiveLoop);
    bool heavyPolyLoad = activeVoicesLoad >= 4;
    switch (safeShape) {
      case 0: { // Square
        sample = oscSqr[i].next();
        break;
      }
      case 1: { // Triangle
        sample = oscTri[i].next();
        break;
      }
      case 2: { // Guitar
        int32_t m = (int32_t)oscTri[i].next() * 120
                  + (int32_t)oscSaw[i].next() * 65
                  + (int32_t)oscCheby[i].next() * 28;
        sample = (int16_t)(m >> 8);
        break;
      }
      case 3: { // Bass
        int32_t m = (int32_t)oscSin[i].next() * 170
                  + (int32_t)oscSqr[i].next() * 90
                  + (int32_t)oscCheby[i].next() * 20;
        sample = (int16_t)(m >> 8);
        break;
      }
      case 4: { // Violin
        int32_t m = (int32_t)oscSaw[i].next() * 100
                  + (int32_t)oscSaw2[i].next() * 92
                  + (int32_t)oscTri[i].next() * 58;
        sample = (int16_t)(m >> 8);
        break;
      }
      case 5: { // TechLead
        int32_t m = (int32_t)oscSaw[i].next() * 118
                  + (int32_t)oscSaw2[i].next() * 102
                  + (int32_t)oscSqr[i].next() * 72;
        sample = (int16_t)(m >> 8);
        break;
      }
      case 6: { // AcidBass
        int32_t m = (int32_t)oscSaw[i].next() * 132
                  + (int32_t)oscSqr[i].next() * 70
                  + (int32_t)oscSin[i].next() * 38;
        sample = (int16_t)(m >> 8);
        break;
      }
      case 7: { // Hybrid
        int32_t m = (int32_t)oscSin[i].next() * 62
                  + (int32_t)oscSaw[i].next() * 78
                  + (int32_t)oscSqr[i].next() * 54
                  + (int32_t)oscTri[i].next() * 46
                  + (int32_t)oscCheby[i].next() * 24;
        sample = (int16_t)(m >> 8);
        break;
      }
      case 8: { // SuperSaw (2 saw déphasées + appui sub)
        int32_t m = (int32_t)oscSaw[i].next() * 118
                  + (int32_t)oscSaw2[i].next() * 118
                  + (int32_t)oscSin[i].next() * 34;
        sample = (int16_t)(m >> 8);
        break;
      }
      case 9: { // Bell / Harmonic additive
        int32_t m = (int32_t)oscSin[i].next() * 90
                  + (int32_t)oscTri[i].next() * 52
                  + (int32_t)oscCheby[i].next() * 86
                  + (int32_t)oscSaw2[i].next() * 18;
        sample = (int16_t)(m >> 8);
        break;
      }
      case 10: { // Sample slot (fallbacks to synth when sample engine is off)
    #if ENABLE_SAMPLE_INSTRUMENT_ENGINE
        sample = oscSample[i].next();
    #else
        int32_t m = (int32_t)oscTri[i].next() * 112
          + (int32_t)oscSin[i].next() * 92
          + (int32_t)oscCheby[i].next() * 40;
        sample = (int16_t)(m >> 8);
    #endif
        break;
      }
      case 11: { // VoxPad (cos + tri + saw detune)
        int32_t m = (int32_t)oscCos[i].next() * 108
                  + (int32_t)oscTri[i].next() * 82
                  + (int32_t)oscSaw2[i].next() * 48;
        sample = (int16_t)(((m * 14) / 10) >> 8);
        break;
      }
      case 12: { // Brass (square + saw + cheby bite)
        int32_t m = (int32_t)oscSqr[i].next() * 102
                  + (int32_t)oscSaw[i].next() * 104
                  + (int32_t)oscCheby[i].next() * 62;
        sample = (int16_t)(m >> 8);
        break;
      }
      case 13: { // DigiFM style (table-based pseudo-ring blend)
        int16_t c = oscCos[i].next();
        int16_t s = oscSin[i].next();
        int16_t fm = (int16_t)(((int32_t)c * (int32_t)s) >> 7);
        int32_t m = (int32_t)fm * 110
                  + (int32_t)oscTri[i].next() * 58
                  + (int32_t)oscCheby[i].next() * 44;
        sample = (int16_t)(m >> 8);
        break;
      }
      case 14: { // Saw (single saw brute pour comparer avec SupSaw)
        sample = oscSaw[i].next();
        break;
      }
      case 15: { // NeoVox (variante plus douce de VoxPad)
        int32_t m = (int32_t)oscCos[i].next() * 116
                  + (int32_t)oscSin[i].next() * 76
                  + (int32_t)oscTri[i].next() * 44;
        sample = (int16_t)(((m * 15) / 10) >> 8);
        break;
      }
      case 16: { // DigiPlk (FM percussif court)
        if (heavyPolyLoad) {
          // Lighter branch to avoid crackles when many voices are active.
          int32_t m = (int32_t)oscSqr[i].next() * 118
                    + (int32_t)oscTri[i].next() * 62;
          sample = (int16_t)(m >> 8);
        } else {
          int16_t c = oscCos[i].next();
          int16_t s = oscSqr[i].next();
          int16_t fm = (int16_t)(((int32_t)c * (int32_t)s) >> 7);
          int32_t m = (int32_t)fm * 112
                    + (int32_t)oscTri[i].next() * 54
                    + (int32_t)oscSin[i].next() * 36;
          sample = (int16_t)(m >> 8);
        }
        break;
      }
      case 17: { // FMGlass (harmoniques brillants)
        if (heavyPolyLoad) {
          int32_t m = (int32_t)oscTri[i].next() * 104
                    + (int32_t)oscSaw2[i].next() * 72;
          sample = (int16_t)((m >> 8));
        } else {
          int16_t c = oscCos[i].next();
          int16_t t = oscTri[i].next();
          int16_t fm = (int16_t)(((int32_t)c * (int32_t)t) >> 7);
          int32_t m = (int32_t)fm * 102
                    + (int32_t)oscCheby[i].next() * 64
                    + (int32_t)oscSaw2[i].next() * 34;
          sample = (int16_t)(((m * 14) / 10) >> 8);
        }
        break;
      }
      case 18: { // ChoirPad (choeur doux, large et aérien)
        if (heavyPolyLoad) {
          int32_t m = (int32_t)oscTri[i].next() * 104
                    + (int32_t)oscSin[i].next() * 84
                    + (int32_t)oscSaw2[i].next() * 44;
          sample = (int16_t)(m >> 8);
          break;
        }
        int16_t s = oscSin[i].next();
        int16_t t = oscTri[i].next();
        int16_t c = oscCos[i].next();
        int16_t airy = (int16_t)(((int32_t)oscSaw2[i].next() + (int32_t)oscCheby[i].next()) >> 1);
        int16_t shimmerLfo = oscCos[i].next();
        int shimmerQ8 = 200 + (((int)shimmerLfo + 128) >> 2); // ~0.78..1.03

        int32_t choir = (int32_t)s * 92
                      + (int32_t)t * 64
                      + (int32_t)c * 56
                      + (int32_t)airy * 34;

        int32_t shaped = ((choir >> 8) * shimmerQ8) >> 8;
        sample = softClipInt16(shaped, 15000);
        break;
      }
      case 19: { // TalkBox (nasal/formant, attaque marquée)
        if (heavyPolyLoad) {
          int32_t m = (int32_t)oscSaw[i].next() * 110
                    + (int32_t)oscSqr[i].next() * 82
                    + (int32_t)oscSin[i].next() * 36;
          sample = softClipInt16(m >> 8, 11200);
          break;
        }
        int16_t s = oscSin[i].next();
        int16_t c = oscCos[i].next();
        int16_t q = oscSqr[i].next();
        int16_t buzz = (int16_t)(((int32_t)oscSaw[i].next() * 3 + (int32_t)q * 2) >> 2);
        int16_t formA = (int16_t)(((int32_t)s * (int32_t)q) >> 7);
        int16_t formB = (int16_t)(((int32_t)c * (int32_t)buzz) >> 7);
        int32_t m = (int32_t)buzz * 98
                  + (int32_t)formA * 74
                  + (int32_t)formB * 52
                  + (int32_t)oscTri[i].next() * 24;
        sample = softClipInt16(m >> 8, 10800);
        break;
      }
      case 20: { // VoxSwp (balayage de formants type voyelles évolutives)
        if (heavyPolyLoad) {
          int32_t m = (int32_t)oscSin[i].next() * 102
                    + (int32_t)oscTri[i].next() * 72
                    + (int32_t)oscSaw[i].next() * 46;
          sample = softClipInt16(m >> 8, 11800);
          break;
        }
        int16_t s = oscSin[i].next();
        int16_t c = oscCos[i].next();
        int16_t t = oscTri[i].next();
        int16_t saw = oscSaw[i].next();

        int16_t sweepLfo = oscCos[i].next();
        int sweepQ8 = ((int)sweepLfo + 128) << 1; // 0..512
        if (sweepQ8 < 0) sweepQ8 = 0;
        if (sweepQ8 > 255) sweepQ8 = 255;

        int16_t vowelA = (int16_t)(((int32_t)s * 170 + (int32_t)t * 70 + (int32_t)c * 50) >> 8);
        int16_t vowelO = (int16_t)(((int32_t)s * 130 + (int32_t)c * 90 + (int32_t)oscCheby[i].next() * 60) >> 8);
        int16_t core = (int16_t)((((int32_t)vowelA * (255 - sweepQ8)) + ((int32_t)vowelO * sweepQ8)) >> 8);

        int formQ8 = 120 + ((80 * sweepQ8) >> 8);
        int16_t formDyn = (int16_t)(((int32_t)core * formQ8) >> 8);
        int32_t m = (int32_t)formDyn * 120 + (int32_t)saw * 42 + (int32_t)c * 28;
        sample = softClipInt16(m >> 8, 11500);
        break;
      }
    #if ENABLE_SOUND_SYNTH_BANKS
      case SOUND_SYNTH_SHAPE_FIRST:
      case SOUND_SYNTH_SHAPE_FIRST + 1:
      case SOUND_SYNTH_SHAPE_FIRST + 2: {
        sample = nextSoundInstrumentSample((uint8_t)i, (uint8_t)safeShape);
        break;
      }
    #endif
      default: sample = oscSin[i].next(); break;
    }

    float envGain = (env / 255.0f) * voiceHoldGain[i] * voiceModAmp[i];
    int32_t voiceOut = (int32_t)((float)sample * envGain);
    if (voices[i].loopVoice) {
      mixedSynthLoop += voiceOut;
      synthActiveLoop++;
    } else {
      mixedSynthLive += voiceOut;
      synthActiveLive++;
    }
  }

  // ── Drums avec samples ──
  const uint32_t nowMs = millis();
  for (uint8_t r = 0; r < DRUM_ROWS; r++) {
    uint8_t denv = drumEnv[r].next();
    if (denv == 0) { 
      drumActive[r] = false;
      continue; 
    }
    if (!drumActive[r]) continue;

    int16_t ds = 0;
    
    // Drums synthétiques (kick, snare, hat, clap)
    const DrumBank &bank = drumBanks[currentDrumBank];
    uint32_t elapsed = nowMs - drumTrigMs[r];
    
    if (isSoundDrumBank((uint8_t)currentDrumBank)) {
      ds = nextSoundDrumSample((uint8_t)r);
    } else {
      switch (r) {
        case 0: { // Kick - utilise sample burroughs1 en banques Rock/Metal, sinon synthétique
#if ENABLE_BURROUGHS_KICK_SAMPLE
          if (currentDrumBank == 3 || currentDrumBank == 4) {
            // Rock/Metal: kick en sample burroughs1 (démarré dans triggerDrum)
            if (drumSampleActive && drumSampleRole == 0 && drumSample.isPlaying()) {
              ds = drumSample.next();
            }
          } else {
#endif
            // Autres banques: kick synthétique
            float sweepT = constrain((float)elapsed / bank.kickTauMs, 0.0f, 1.0f);
            float kf = (bank.kickStartHz - sweepT * (bank.kickStartHz - bank.kickEndHz)) * drumPitch;
            drumSinKick.setFreq(kf);
            ds = drumSinKick.next();
#if ENABLE_BURROUGHS_KICK_SAMPLE
          }
#endif
          break;
        }
        case 1: { // Snare
          float sweepT = constrain((float)elapsed / bank.snareTauMs, 0.0f, 1.0f);
          float sf = (bank.snareStartHz - sweepT * (bank.snareStartHz - bank.snareEndHz)) * drumPitch;
          drumSinSnare.setFreq(sf);
          int16_t tone = drumSinSnare.next();
          int8_t noise = nextNoise();
          ds = (int16_t)(((int32_t)tone * (255 - bank.snareNoise) + (int32_t)noise * bank.snareNoise) >> 8);
          break;
        }
        case 2: { // HiHat
          int8_t n = nextNoise();
          ds = (int16_t)(((int32_t)n * bank.hhNoise) >> 7);
          break;
        }
        case 3: { // Clap
          float sweepT = constrain((float)elapsed / bank.clapTauMs, 0.0f, 1.0f);
          float cf = (bank.clapStartHz - sweepT * (bank.clapStartHz - bank.clapEndHz)) * drumPitch;
          drumSinClap.setFreq(cf);
          int16_t tone = drumSinClap.next();
          int8_t noise = nextNoise();
          ds = (int16_t)(((int32_t)tone * (255 - bank.clapNoise) + (int32_t)noise * bank.clapNoise) >> 8);
          break;
        }
      }
    }
    
    mixedDrum += ((int32_t)ds * denv) >> 7;  // Divisé par 128
    drumCount++;
  }

  int32_t synthOutLive = mixedSynthLive;
  int32_t synthOutLoop = mixedSynthLoop;
  // Keep full transient energy for 1-2 notes (A+B behavior), then gradually tame dense chords.
  if (synthActiveLive > 2) {
    synthOutLive = (synthOutLive * 2) / synthActiveLive;
  }
  if (synthActiveLoop > 2) {
    synthOutLoop = (synthOutLoop * 2) / synthActiveLoop;
  }
  int32_t drumOut = (drumCount > 0) ? (mixedDrum / drumCount) : 0;
  float vscaleForDrumComp = masterVolume / 16000.0f;
  if (vscaleForDrumComp < 1.0f) vscaleForDrumComp = 1.0f;
  if (vscaleForDrumComp > 10.0f) vscaleForDrumComp = 10.0f;

  int32_t drumPost = ((drumOut * drumMixAmount) >> 8);
  // As master volume rises, compensate drum gain so drums do not explode in level.
  float drumComp = (drumGlobalGain / (0.80f + 0.20f * vscaleForDrumComp)) * drumAmplitude;
  drumPost = (int32_t)((float)drumPost * drumComp);
  if (drumPost > 28000) drumPost = 28000;
  if (drumPost < -28000) drumPost = -28000;

  int16_t instDry = compress16(((int32_t)synthOutLive * (int32_t)masterTrackGainQ8[0]) >> 7, 20000, 2);
  int16_t drumDry = compress16(((int32_t)drumPost * (int32_t)masterTrackGainQ8[1]) >> 7, 20000, 2);
  int16_t loopDry = compress16(((int32_t)synthOutLoop * (int32_t)masterTrackGainQ8[2]) >> 7, 20000, 2);

  int32_t liveFxSource = 0;
  if (masterTrackFxEnabled[0]) liveFxSource += instDry;
  if (masterTrackFxEnabled[1]) liveFxSource += drumDry;
  int16_t liveInput = compress16(liveFxSource, 18000, 2);

  int16_t liveDryBypass = 0;
  if (!masterTrackFxEnabled[0]) liveDryBypass = compress16((int32_t)liveDryBypass + instDry, 22000, 2);
  if (!masterTrackFxEnabled[1]) liveDryBypass = compress16((int32_t)liveDryBypass + drumDry, 22000, 2);

  int16_t liveOut = liveInput;
  uint8_t livePolyLoad = synthActiveLive;
  if (masterTrackFxEnabled[0] || masterTrackFxEnabled[1]) {
    liveOut = applyEffectsChain(
      liveInput,
      effectEnabled,
      fxAmount,
      livePolyLoad,
      lpf,
      resoEchoFilter,
      acidFilter,
      fxLfo,
      flangerLfo,
      tremoloLfo,
      lfoModSine,
      lfoModSquare,
      delayBuffer,
      delayWriteIndex,
      liveHpfX1,
      liveHpfY1,
      liveBcCount,
      liveBcHold,
      liveEnvFollow,
      liveSubLast,
      liveSubPol,
      liveSubLp,
      liveRandCount,
      liveRandHold,
      liveRandSeed,
      liveHarmPhaseQ8
    );
  }
  liveOut = compress16((int32_t)liveOut + liveDryBypass, 22000, 2);

  const bool* loopFxMask = loopTrackLocked ? lockedEffectMask : effectEnabled;
  uint8_t loopFxAmt = loopTrackLocked ? lockedFxAmount : fxAmount;
  uint8_t loopPolyLoad = synthActiveLoop;

  int16_t loopOut = loopDry;
  if (masterTrackFxEnabled[2]) {
    loopOut = applyEffectsChain(
      loopDry,
      loopFxMask,
      loopFxAmt,
      loopPolyLoad,
      lockLpf,
      lockResoEchoFilter,
      lockAcidFilter,
      lockFxLfo,
      lockFlangerLfo,
      lockTremoloLfo,
      lfoModSine,
      lfoModSquare,
      lockDelayBuffer,
      lockDelayWriteIndex,
      lockHpfX1,
      lockHpfY1,
      lockBcCount,
      lockBcHold,
      lockEnvFollow,
      lockSubLast,
      lockSubPol,
      lockSubLp,
      lockRandCount,
      lockRandHold,
      lockRandSeed,
      lockHarmPhaseQ8
    );
  }

  int16_t output = compress16((int32_t)liveOut + (int32_t)loopOut, 22000, 2);

  // ── Volume + saturation sécurisée ──
  float vscale = masterVolume / 16000.0f;
  if (vscale > 10.0f) vscale = 10.0f;
  int32_t driven = (int32_t)((float)output * vscale * 1.5f);
  int32_t absDriven = driven < 0 ? -driven : driven;
  int32_t final32 = (driven * (32767 + (absDriven >> 2))) / (32767 + (absDriven >> 1));
  if (final32 >  32767) final32 =  32767;
  if (final32 < -32767) final32 = -32767;
  int16_t finalSample = (int16_t)final32;

  return MonoOutput::from16Bit(finalSample);
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  lastUserActivityMs = millis();
  
  // Désactiver WiFi/Bluetooth pour éviter les interruptions périodiques
  WiFi.mode(WIFI_OFF);
  esp_bt_controller_disable();
  
  Serial.println("\n\n[BOOT] === CLAVIER V2 INIT ===");
#if SAFE_MODE
  Serial.println("[BOOT] *** SAFE_MODE ACTIVE: I2C/SPI/LEDs DISABLED ***");
  Serial.println("[BOOT] *** Audio I2S + ADC pots ONLY ***");
#endif
  Serial.println("[BOOT] WiFi/BT disabled for audio stability");
  Serial.println("[BOOT] 1/10 Serial OK");
  
  analogReadResolution(12);
  Serial.println("[BOOT] 2/10 Analog resolution OK");

  // I2S is initialized by Mozzi native ESP32 backend in MOZZI_OUTPUT_I2S_DAC mode.
  Serial.println("[BOOT] 3/10 I2S backend: Mozzi native");

#if !SAFE_MODE
  // NeoPixel
  Serial.println("[BOOT] 4/10 Initializing NeoPixels...");
  strip.begin();
  strip.setBrightness(BRIGHTNESS);
  strip.show();
  columnColors[0] = strip.Color(255,   0,   0);
  columnColors[1] = strip.Color(255, 127,   0);
  columnColors[2] = strip.Color(255, 255,   0);
  columnColors[3] = strip.Color(  0, 255,   0);
  columnColors[4] = strip.Color(  0, 255, 255);
  columnColors[5] = strip.Color(  0,   0, 255);
  columnColors[6] = strip.Color(127,   0, 255);
  columnColors[7] = strip.Color(255,   0, 127);
  Serial.println("[BOOT] 4/10 NeoPixel OK");
#else
  Serial.println("[BOOT] 4/10 NeoPixel SKIPPED (SAFE_MODE)");
#endif

#if !SAFE_MODE
  // OLED
  Serial.println("[BOOT] 5/10 Initializing OLED...");
  Wire.begin(OLED_SDA, OLED_SCL, 400000);
  Wire.setClock(400000);
  Wire.setTimeOut(20);
  delay(20);
  u8g2.begin();
  u8g2.setFont(u8g2_font_6x12_tf);
  u8g2.clearBuffer();
  u8g2.drawFrame(0, 0, 128, 128);
  u8g2.drawStr(20, 40, "CLAVIER V2");
  u8g2.drawStr(10, 58, "Mozzi synth");
  u8g2.drawStr(10, 74, "drum engine");
  u8g2.sendBuffer();
  Serial.println("[BOOT] 5/10 OLED OK");
  delay(600);
#else
  Serial.println("[BOOT] 5/10 OLED SKIPPED (SAFE_MODE)");
#endif

#if !SAFE_MODE
  // MCP23X17
  Serial.println("[BOOT] 6/10 Initializing MCP23X17...");
  buttonsWire.begin(I2C_SDA, I2C_SCL, 400000);
  buttonsWire.setTimeOut(20);
  delay(50);
  
  Serial.println("[BOOT]    Initializing MCP1 (0x21)...");
  if (!mcp1.begin_I2C(0x21, &buttonsWire))    { Serial.println("[BOOT] ERROR: MCP1 fail");     while (1); }
  Serial.println("[BOOT]    MCP1 (0x21) OK");
  delay(10);
  
  Serial.println("[BOOT]    Initializing MCP2 (0x23)...");
  if (!mcp2.begin_I2C(0x23, &buttonsWire))    { Serial.println("[BOOT] ERROR: MCP2 fail");     while (1); }
  Serial.println("[BOOT]    MCP2 (0x23) OK");
  delay(10);
  
  Serial.println("[BOOT]    Initializing MCPExtra (0x26)...");
  Serial.flush();  // Force serial output before potential crash
  if (!mcpExtra.begin_I2C(0x26, &buttonsWire)){ Serial.println("[BOOT] ERROR: MCPExtra fail"); while (1); }
  Serial.println("[BOOT]    MCPExtra (0x26) OK");
  
  Serial.println("[BOOT]    Configuring MCP pin modes...");
  for (int i = 0; i < 16; i++) {
    mcp1.pinMode(i, INPUT_PULLUP);
    mcp2.pinMode(i, INPUT_PULLUP);
  }
  for (int i = 0; i < EXTRA_BUTTONS; i++) {
    mcpExtra.pinMode(extraMcpPins[i], INPUT_PULLUP);
  }
  Serial.println("[BOOT]    Scanning initial button state...");
  scanButtons();
  consumeButtonEvents();
  Serial.println("[BOOT] 6/10 MCP23X17 OK");
#else
  Serial.println("[BOOT] 6/10 MCP23X17 SKIPPED (SAFE_MODE)");
#endif

  // Init synth voices
  Serial.println("[BOOT] 7/10 Initializing synth module...");
  initSynth();
  Serial.println("[BOOT] 7/10 Synth OK");
  
  // Init drums
  Serial.println("[BOOT] 8/10 Initializing drum module...");
  initDrums();
  Serial.println("[BOOT] 8/10 Drums OK");
  
  // Init effects
  Serial.println("[BOOT] 9/10 Initializing effects module...");
  initEffects();
  lockFxLfo.setFreq(0.3f);
  lockFlangerLfo.setFreq(0.4f);
  lockLpf.setResonance(180);
  lockAcidFilter.setResonance(200);
  Serial.println("[BOOT] 9/10 Effects OK");

  // Read pots
  Serial.println("[BOOT] Reading potentiometers...");
  for (int i = 0; i < POT_COUNT; i++) {
    potRaw[i] = analogRead(POT_PINS[i]);
    potFilt[i] = invertPotValue(potRaw[i]);
  }
  octaveShift = quantizeOctaveFromPot(potFilt[4]);
  lastOctaveShift = octaveShift;
  masterVolume = 600000.0f;
  Serial.println("[BOOT] Potentiometers OK");

  // Create FreeRTOS tasks FIRST (as in original working version)
  Serial.println("[BOOT] Creating FreeRTOS tasks...");
  Serial.println("[BOOT] All I2C/SPI tasks on core 1, core 0 reserved for audio");
#if !SAFE_MODE
  xTaskCreatePinnedToCore(buttonScanTask, "buttonScan", 4096, nullptr, 1, &buttonScanTaskHandle, 1);
  Serial.println("[BOOT]    buttonScanTask created on core 1");
#else
  Serial.println("[BOOT]    buttonScanTask SKIPPED (SAFE_MODE)");
#endif
  xTaskCreatePinnedToCore(uiTask, "uiTask", 6144, nullptr, 1, &uiTaskHandle, 1);
  Serial.println("[BOOT]    uiTask created on core 1");

  // Start Mozzi AFTER tasks (as in original working version)
  Serial.println("[BOOT] 10/10 Starting Mozzi...");
  Serial.flush();
  startMozzi(CONTROL_RATE);
  Serial.println("[BOOT] 10/10 Mozzi OK");

  // Afficher la banque de drums par défaut
  Serial.print("[DRUM] Initial Drum Bank: ");
  Serial.println(drumBanks[currentDrumBank].name);
  
  Serial.println("[BOOT] === CLAVIER V2 READY ===\n");
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
  consumeButtonEvents();
  processInputActions();
#if !SAFE_MODE
  if (ledImmediateRequested) {
    unsigned long now = millis();
    if (lastLedMs == 0 || (now - lastLedMs >= 1)) {
      lastLedMs = now;
      ledImmediateRequested = false;
      ledRefreshRequested = false;
      renderLeds();
    }
  }
#endif
  audioHook();
}
