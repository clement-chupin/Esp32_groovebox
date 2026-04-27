#include "drum_module.h"
#include "synth_module.h"
#include <Oscil.h>
#include <ADSR.h>
#include <Sample.h>
#include <tables/sin2048_int8.h>
#if ENABLE_BURROUGHS_KICK_SAMPLE
#include <samples/burroughs1_18649_int8.h>
#endif
#include "SOUNDS/zSAMPLE03.h"
#include "SOUNDS/zSAMPLE04.h"
#include "SOUNDS/zSAMPLE06.h"
#include "SOUNDS/zSAMPLE07.h"
#include "SOUNDS/zSAMPLE10.h"
#include "SOUNDS/zSAMPLE11.h"
#include "SOUNDS/zSAMPLE14.h"
#include "SOUNDS/zSAMPLE15.h"
#include "SOUNDS/zSAMPLE24.h"
#include "SOUNDS/zSAMPLE36.h"
#include "SOUNDS/zSAMPLE38.h"
#include "SOUNDS/zSAMPLE40.h"
#include "SOUNDS/zSAMPLE41.h"
#include "SOUNDS/zSAMPLE42.h"
#include "SOUNDS/zSAMPLE43.h"
#include "SOUNDS/zSAMPLE44.h"
#include "SOUNDS/zSAMPLE45.h"
#include "SOUNDS/zSAMPLE46.h"

#ifdef NUM_ELEMENTS
#undef NUM_ELEMENTS
#endif

struct SoundDrumSampleDesc {
  const int16_t* data;
  uint32_t len;
};

static const SoundDrumSampleDesc kSoundDrumPool[18] = {
  {SAMPLE03, (uint32_t)(sizeof(SAMPLE03) / sizeof(SAMPLE03[0]))},
  {SAMPLE04, (uint32_t)(sizeof(SAMPLE04) / sizeof(SAMPLE04[0]))},
  {SAMPLE06, (uint32_t)(sizeof(SAMPLE06) / sizeof(SAMPLE06[0]))},
  {SAMPLE07, (uint32_t)(sizeof(SAMPLE07) / sizeof(SAMPLE07[0]))},
  {SAMPLE10, (uint32_t)(sizeof(SAMPLE10) / sizeof(SAMPLE10[0]))},
  {SAMPLE11, (uint32_t)(sizeof(SAMPLE11) / sizeof(SAMPLE11[0]))},
  {SAMPLE14, (uint32_t)(sizeof(SAMPLE14) / sizeof(SAMPLE14[0]))},
  {SAMPLE15, (uint32_t)(sizeof(SAMPLE15) / sizeof(SAMPLE15[0]))},
  {SAMPLE24, (uint32_t)(sizeof(SAMPLE24) / sizeof(SAMPLE24[0]))},
  {SAMPLE36, (uint32_t)(sizeof(SAMPLE36) / sizeof(SAMPLE36[0]))},
  {SAMPLE38, (uint32_t)(sizeof(SAMPLE38) / sizeof(SAMPLE38[0]))},
  {SAMPLE40, (uint32_t)(sizeof(SAMPLE40) / sizeof(SAMPLE40[0]))},
  {SAMPLE41, (uint32_t)(sizeof(SAMPLE41) / sizeof(SAMPLE41[0]))},
  {SAMPLE42, (uint32_t)(sizeof(SAMPLE42) / sizeof(SAMPLE42[0]))},
  {SAMPLE43, (uint32_t)(sizeof(SAMPLE43) / sizeof(SAMPLE43[0]))},
  {SAMPLE44, (uint32_t)(sizeof(SAMPLE44) / sizeof(SAMPLE44[0]))},
  {SAMPLE45, (uint32_t)(sizeof(SAMPLE45) / sizeof(SAMPLE45[0]))},
  {SAMPLE46, (uint32_t)(sizeof(SAMPLE46) / sizeof(SAMPLE46[0]))}
};

static const uint8_t kSoundDrumBankMap[SOUND_DRUM_BANK_COUNT][DRUM_ROWS] = {
  {11, 12, 13, 14},
  {2, 4, 9, 10},
  {6, 7, 15, 17},
  {0, 5, 8, 16},
  {1, 12, 14, 15},
  {3, 4, 10, 13},
  {2, 5, 9, 17},
  {0, 6, 11, 16}
};

static bool soundDrumVoiceActive[DRUM_ROWS] = {false};
static uint32_t soundDrumVoicePosQ16[DRUM_ROWS] = {0};
// 65536 = 1.0 in Q16 fixed-point format (1 << 16)
static const uint32_t kQ16One = 65536U;
static uint32_t soundDrumVoiceIncQ16[DRUM_ROWS] = {65536U};

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
  soundDrumVoicePosQ16[row] = 0;

  float rowMul = 0.88f;
  if (row == 1) rowMul = 0.92f;
  else if (row == 2) rowMul = 1.05f;
  else if (row == 3) rowMul = 0.95f;

  float incF = constrain(drumPitch * rowMul, 0.35f, 1.25f);
  soundDrumVoiceIncQ16[row] = (uint32_t)(incF * (float)kQ16One);
}

int16_t nextSoundDrumSample(uint8_t row) {
  if (row >= DRUM_ROWS || !soundDrumVoiceActive[row]) return 0;

  const SoundDrumSampleDesc* desc = currentSoundDrumSample(row);
  if (desc == nullptr || desc->len < 2 || desc->data == nullptr) {
    soundDrumVoiceActive[row] = false;
    return 0;
  }

  // Q16 fixed-point position: upper 16 bits = sample index, lower 16 = fraction.
  uint32_t posQ16 = soundDrumVoicePosQ16[row];
  uint32_t endQ16 = (uint32_t)(desc->len - 1U) << 16;
  if (posQ16 >= endQ16) {
    soundDrumVoiceActive[row] = false;
    return 0;
  }

  uint32_t idx     = posQ16 >> 16;
  uint32_t idxNext = idx + 1U;
  // Q8 fraction (0..255): drum data can span up to ~60 000, so Q16 would overflow int32_t.
  // Using Q8 keeps the intermediate product well within int32_t.
  int32_t frac8 = (int32_t)((posQ16 >> 8) & 0xFF);

  int32_t a = (int32_t)desc->data[idx];
  int32_t b = (int32_t)desc->data[idxNext];
  int32_t interp = a + (((b - a) * frac8) >> 8);

  posQ16 += soundDrumVoiceIncQ16[row];
  if (posQ16 >= endQ16) {
    soundDrumVoiceActive[row] = false;
  }
  soundDrumVoicePosQ16[row] = posQ16;

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
    soundDrumVoicePosQ16[r] = 0;
    soundDrumVoiceIncQ16[r] = kQ16One;
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
  
  // Mise à jour enveloppes drums + sweep fréquence (at control rate, not audio rate)
  for (int r = 0; r < DRUM_ROWS; r++) {
    drumEnv[r].update();
    if (drumActive[r]) {
      unsigned long elapsed = millis() - drumTrigMs[r];
      const DrumBank &bank = drumBanks[currentDrumBank];
      
      if (!isSoundDrumBank((uint8_t)currentDrumBank)) {
        // Kick (r==0): linear frequency sweep computed here instead of at audio rate
        if (r == 0) {
          #if ENABLE_BURROUGHS_KICK_SAMPLE
          // Oscillator only needed for non-sample banks
          if (currentDrumBank != 3 && currentDrumBank != 4) {
          #endif
            float sweepT = constrain((float)elapsed / bank.kickTauMs, 0.0f, 1.0f);
            kickFreqCurrent = (bank.kickStartHz - sweepT * (bank.kickStartHz - bank.kickEndHz)) * drumPitch;
            drumSinKick.setFreq(kickFreqCurrent);
          #if ENABLE_BURROUGHS_KICK_SAMPLE
          }
          #endif
        }
        // Snare (r==1): linear frequency sweep computed here instead of at audio rate
        else if (r == 1) {
          float sweepT = constrain((float)elapsed / bank.snareTauMs, 0.0f, 1.0f);
          snareFreqCurrent = (bank.snareStartHz - sweepT * (bank.snareStartHz - bank.snareEndHz)) * drumPitch;
          drumSinSnare.setFreq(snareFreqCurrent);
        }
        // Clap (r==3): linear frequency sweep computed here instead of at audio rate
        else if (r == 3) {
          float sweepT = constrain((float)elapsed / bank.clapTauMs, 0.0f, 1.0f);
          clapFreqCurrent = (bank.clapStartHz - sweepT * (bank.clapStartHz - bank.clapEndHz)) * drumPitch;
          drumSinClap.setFreq(clapFreqCurrent);
        }
      }
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
