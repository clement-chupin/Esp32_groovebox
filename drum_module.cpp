#include "drum_module.h"
#include "synth_module.h"
#include <Oscil.h>
#include <ADSR.h>
#include <Sample.h>
#include <tables/sin2048_int8.h>
#if ENABLE_BURROUGHS_KICK_SAMPLE
#include <samples/burroughs1_18649_int8.h>
#endif
// Crunch_E drum bank — compact selection to fit flash budget
// kick1=24KB kick2=18KB snare1=19KB snare2=17KB snareB3=12KB
// hihat1=10KB snare3=18KB bongo1=21KB  →  total ~139KB
#include "SOUNDS/Crunch_E/kick1.h"
#include "SOUNDS/Crunch_E/kick2.h"
#include "SOUNDS/Crunch_E/snare1.h"
#include "SOUNDS/Crunch_E/snare2.h"
#include "SOUNDS/Crunch_E/snareB3.h"
#include "SOUNDS/Crunch_E/hihat1.h"
#include "SOUNDS/Crunch_E/snare3.h"
#include "SOUNDS/Crunch_E/bongo1.h"

#ifdef NUM_ELEMENTS
#undef NUM_ELEMENTS
#endif

struct SoundDrumSampleDesc {
  const int* data;
  uint32_t len;
};

// Pool: 0=kick1, 1=kick2, 2=snare1, 3=snare2, 4=snareB3, 5=hihat1, 6=snare3, 7=bongo1
static const SoundDrumSampleDesc kSoundDrumPool[8] = {
  {kick1,   (uint32_t)(sizeof(kick1)   / sizeof(kick1[0]))},
  {kick2,   (uint32_t)(sizeof(kick2)   / sizeof(kick2[0]))},
  {snare1,  (uint32_t)(sizeof(snare1)  / sizeof(snare1[0]))},
  {snare2,  (uint32_t)(sizeof(snare2)  / sizeof(snare2[0]))},
  {snareB3, (uint32_t)(sizeof(snareB3) / sizeof(snareB3[0]))},
  {hihat1,  (uint32_t)(sizeof(hihat1)  / sizeof(hihat1[0]))},
  {snare3,  (uint32_t)(sizeof(snare3)  / sizeof(snare3[0]))},
  {bongo1,  (uint32_t)(sizeof(bongo1)  / sizeof(bongo1[0]))}
};

// Banks: rows = [kick, snare, hihat, perc]
// Bank 0: kick1, snare1,  hihat1, bongo1
// Bank 1: kick2, snare2,  hihat1, snareB3
// Bank 2: kick1, snare3,  hihat1, snare2
// Bank 3: kick2, snareB3, hihat1, bongo1
// Bank 4: kick1, snare2,  hihat1, snare3
// Bank 5: kick2, snare1,  hihat1, snareB3
// Bank 6: kick1, snareB3, hihat1, snare1
// Bank 7: kick2, snare3,  hihat1, bongo1
static const uint8_t kSoundDrumBankMap[SOUND_DRUM_BANK_COUNT][DRUM_ROWS] = {
  {0, 2, 5, 7},
  {1, 3, 5, 4},
  {0, 6, 5, 3},
  {1, 4, 5, 7},
  {0, 3, 5, 6},
  {1, 2, 5, 4},
  {0, 4, 5, 2},
  {1, 6, 5, 7}
};

static bool soundDrumVoiceActive[DRUM_ROWS] = {false};
static float soundDrumVoicePos[DRUM_ROWS] = {0.0f};
static float soundDrumVoiceInc[DRUM_ROWS] = {1.0f};

// SDr banks were perceptually hotter than synth banks; attenuate and rebalance per row.
static const uint8_t kSoundDrumRowGainQ8[DRUM_ROWS] = {
  112, // kick
  118, // snare
  98,  // hihat
  108  // clap
};

bool isSoundDrumBank(uint8_t bankIndex) {
  return (bankIndex >= SOUND_DRUM_BANK_FIRST) && (bankIndex < DRUM_BANK_COUNT);
}

static inline const SoundDrumSampleDesc* currentSoundDrumSample(uint8_t row) {
  if (row >= DRUM_ROWS || !isSoundDrumBank((uint8_t)currentDrumBank)) return nullptr;
  uint8_t bank = (uint8_t)currentDrumBank - SOUND_DRUM_BANK_FIRST;
  if (bank >= SOUND_DRUM_BANK_COUNT) return nullptr;
  uint8_t poolIndex = kSoundDrumBankMap[bank][row];
  return &kSoundDrumPool[poolIndex];
}

static void triggerSoundDrum(uint8_t row) {
  const SoundDrumSampleDesc* desc = currentSoundDrumSample(row);
  if (desc == nullptr || desc->len < 2 || desc->data == nullptr) return;

  soundDrumVoiceActive[row] = true;
  soundDrumVoicePos[row] = 0.0f;

  float rowMul = 0.88f;
  if (row == 1) rowMul = 0.92f;
  else if (row == 2) rowMul = 1.05f;
  else if (row == 3) rowMul = 0.95f;

  soundDrumVoiceInc[row] = constrain(drumPitch * rowMul, 0.35f, 1.25f);
}

int16_t nextSoundDrumSample(uint8_t row) {
  if (row >= DRUM_ROWS || !soundDrumVoiceActive[row]) return 0;

  const SoundDrumSampleDesc* desc = currentSoundDrumSample(row);
  if (desc == nullptr || desc->len < 2 || desc->data == nullptr) {
    soundDrumVoiceActive[row] = false;
    return 0;
  }

  float pos = soundDrumVoicePos[row];
  if (pos < 0.0f) pos = 0.0f;
  if (pos >= (float)(desc->len - 1U)) {
    soundDrumVoiceActive[row] = false;
    return 0;
  }

  uint32_t idx = (uint32_t)pos;
  uint32_t idxNext = idx + 1U;
  float frac = pos - (float)idx;

  int32_t a = (int32_t)desc->data[idx];
  int32_t b = (int32_t)desc->data[idxNext];
  int32_t interp = (int32_t)((1.0f - frac) * (float)a + frac * (float)b);

  pos += soundDrumVoiceInc[row];
  if (pos >= (float)(desc->len - 1U)) {
    soundDrumVoiceActive[row] = false;
  }
  soundDrumVoicePos[row] = pos;

  // Extra attenuation for SDr banks: these source samples are mastered much hotter.
  int32_t sample16 = (interp >> 3);
  sample16 = (sample16 * (int32_t)kSoundDrumRowGainQ8[row]) >> 8;
  sample16 >>= 1;  // User-requested extra -6 dB for all SDr banks.
  sample16 = (sample16 * 170) >> 8;  // Additional ~0.66x trim (about -3.6 dB).
  return (int16_t)constrain(sample16, -32767, 32767);
}

// ==================== INITIALISATION ====================
void initDrums() {
  drumSinKick.setTable(SIN2048_DATA);
  drumSinSnare.setTable(SIN2048_DATA);
  drumSinHat.setTable(SIN2048_DATA);
  drumSinClap.setTable(SIN2048_DATA);
  drumSinKick.setFreq(80.0f);
  drumSinSnare.setFreq(200.0f);
  drumSinHat.setFreq(2400.0f);
  drumSinClap.setFreq(1600.0f);
#if ENABLE_BURROUGHS_KICK_SAMPLE
  drumSample.setFreq((float)BURROUGHS1_18649_SAMPLERATE / (float)BURROUGHS1_18649_NUM_CELLS);
#endif

  for (uint8_t r = 0; r < DRUM_ROWS; r++) {
    soundDrumVoiceActive[r] = false;
    soundDrumVoicePos[r] = 0.0f;
    soundDrumVoiceInc[r] = 1.0f;
  }
  
  for (int r = 0; r < DRUM_ROWS; r++) {
    drumEnv[r].setADLevels(255, 0);
    drumEnv[r].setTimes(2, 60, 0, 80);
  }
}

// ==================== VARIABLES (non-Mozzi only - Mozzi objects are in .ino) ====================
// All Mozzi objects (Oscil, ADSR, Sample) are defined in clavier_v2.ino
// and declared as extern in drum_module.h

// LFSR 32-bit pour le bruit
static uint32_t lfsrState = 0xACE1FACE;

// ==================== NOISE GENERATION ====================
int8_t nextNoise() {
  lfsrState ^= lfsrState << 13;
  lfsrState ^= lfsrState >> 17;
  lfsrState ^= lfsrState << 5;
  return (int8_t)(lfsrState & 0xFF);
}

// ==================== DRUM TRIGGERING ====================
void triggerDrum(uint8_t row) {
  if (row >= DRUM_ROWS) return;
  static unsigned long lastTrigMs[DRUM_ROWS] = {0};
  unsigned long now = millis();
  if (now - lastTrigMs[row] < 12) return;
  lastTrigMs[row] = now;
  
  drumActive[row] = true;
  drumTrigMs[row] = now;

  if (isSoundDrumBank((uint8_t)currentDrumBank)) {
    triggerSoundDrum(row);
  }
  
#if ENABLE_BURROUGHS_KICK_SAMPLE
  // Si c'est le kick (row 0) en banque Rock (3) ou Metal (4), utiliser sample burroughs1
  if (!isSoundDrumBank((uint8_t)currentDrumBank) && row == 0 && (currentDrumBank == 3 || currentDrumBank == 4)) {
    // Offset aléatoire pour varier la partie du sample jouée
    static uint16_t sampleOffset = 0;
    sampleOffset = (sampleOffset + 2347) % (BURROUGHS1_18649_NUM_CELLS - 1000);
    drumSample.start(sampleOffset);
    drumSampleActive = true;
    drumSampleRole = 0; // kick
  }
#endif
  
  // Enveloppes par défaut (peuvent être personnalisées par row)
  drumEnv[row].setADLevels(255, 0);
  switch (row) {
    case 0: drumEnv[row].setTimes(1, 80, 0, 100); break;  // Kick: court
    case 1: drumEnv[row].setTimes(2, 60, 0, 80); break;   // Snare: moyen
    case 2: drumEnv[row].setTimes(1, 40, 0, 50); break;   // HiHat: très court
    case 3: drumEnv[row].setTimes(2, 100, 0, 120); break; // Clap: moyen-long
  }
  drumEnv[row].noteOn();
}

// ==================== SEQUENCER ====================
static const uint8_t kDrumStepOptions[3] = {4, 8, 16};

uint8_t currentDrumSteps() {
  return kDrumStepOptions[drumDivisionIndex % 3];
}

void resetDrumTransport(bool keepRunning) {
  drumStep = 0;
  lastStepMs = millis();
  if (!keepRunning) drumRun = false;
}

void runDrumSequencer() {
  extern uint16_t bpm;
  uint16_t stepMs = (uint16_t)constrain((60000UL / bpm) / 4, 20, 2000);
  uint8_t stepCount = currentDrumSteps();
  unsigned long now = millis();

  // En 1/16, la page d'édition suit automatiquement la position du step.
  if (stepCount == 16) {
    drumEditPage = (uint8_t)((drumStep >= 8) ? 1 : 0);
  } else {
    drumEditPage = 0;
  }

  if (drumRun && (now - lastStepMs >= stepMs)) {
    lastStepMs = now;
    drumStep = (drumStep + 1) % stepCount;

    if (stepCount == 16) {
      drumEditPage = (uint8_t)((drumStep >= 8) ? 1 : 0);
    }

    for (int r = 0; r < DRUM_ROWS; r++) {
      if (drumPattern[r][drumStep]) triggerDrum((uint8_t)r);
    }
    extern bool ledRefreshRequested;
    ledRefreshRequested = true;
  }
}

// ==================== UPDATE CONTROL ====================
void updateDrumsControl() {
  extern uint16_t bpm;
  
  // Mise à jour enveloppes drums + sweep fréquence
  for (int r = 0; r < DRUM_ROWS; r++) {
    drumEnv[r].update();
    if (drumActive[r]) {
      unsigned long elapsed = millis() - drumTrigMs[r];
      const DrumBank &bank = drumBanks[currentDrumBank];
      
      // Seule la snare utilise encore la synthèse avec sweep
      if (r == 1) {
        // Snare : sweep selon la bank
        float decay = expf(-(float)elapsed / bank.snareTauMs);
        snareFreqCurrent = bank.snareEndHz + (bank.snareStartHz - bank.snareEndHz) * decay;
        drumSinSnare.setFreq(applyOctave(snareFreqCurrent, octaveShift));
      }
      // Les autres drums (kick, hat, clap) utilisent des samples bamboo
    }
  }

  // Sample playback check (ne pas lire ici, c'est fait dans updateAudio)
  #if ENABLE_BURROUGHS_KICK_SAMPLE
  if (!isSoundDrumBank((uint8_t)currentDrumBank) && drumSampleActive && !drumSample.isPlaying()) {
    drumSampleActive = false;
    drumSampleRole = 255;
  }
  #endif
}
