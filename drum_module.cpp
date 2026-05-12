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

// Full Crunch_E pool for SMPL instrument (all 45 samples)
// Remap conflicting symbol names so Crunch_E and MothOS can coexist at link time.
#define bass1 ce_bass1
#define bass1Length ce_bass1Length
#include "SOUNDS/Crunch_E/bass1.h"
#undef bass1
#undef bass1Length
#include "SOUNDS/Crunch_E/bass2.h"
#define clap1 ce_clap1
#define clap1Length ce_clap1Length
#include "SOUNDS/Crunch_E/clap1.h"
#undef clap1
#undef clap1Length
#define crash1 ce_crash1
#define crash1Length ce_crash1Length
#include "SOUNDS/Crunch_E/crash1.h"
#undef crash1
#undef crash1Length
#define guitar1 ce_guitar1
#define guitar1Length ce_guitar1Length
#include "SOUNDS/Crunch_E/guitar1.h"
#undef guitar1
#undef guitar1Length
#define hihat2 ce_hihat2
#define hihat2Length ce_hihat2Length
#include "SOUNDS/Crunch_E/hihat2.h"
#undef hihat2
#undef hihat2Length
#define jbass1 ce_jbass1
#define jbass1Length ce_jbass1Length
#include "SOUNDS/Crunch_E/jbass1.h"
#undef jbass1
#undef jbass1Length
#define jbass2 ce_jbass2
#define jbass2Length ce_jbass2Length
#include "SOUNDS/Crunch_E/jbass2.h"
#undef jbass2
#undef jbass2Length
#define jlead1 ce_jlead1
#define jlead1Length ce_jlead1Length
#include "SOUNDS/Crunch_E/jlead1.h"
#undef jlead1
#undef jlead1Length
#define jlead2 ce_jlead2
#define jlead2Length ce_jlead2Length
#include "SOUNDS/Crunch_E/jlead2.h"
#undef jlead2
#undef jlead2Length
#include "SOUNDS/Crunch_E/jlead3.h"
#include "SOUNDS/Crunch_E/jlead4.h"
#define jpad1 ce_jpad1
#define jpad1Length ce_jpad1Length
#include "SOUNDS/Crunch_E/jpad1.h"
#undef jpad1
#undef jpad1Length
#define kick3 ce_kick3
#define kick3Length ce_kick3Length
#include "SOUNDS/Crunch_E/kick3.h"
#undef kick3
#undef kick3Length
#define pad1 ce_pad1
#define pad1Length ce_pad1Length
#include "SOUNDS/Crunch_E/pad1.h"
#undef pad1
#undef pad1Length
#include "SOUNDS/Crunch_E/pad2.h"
#define pad3 ce_pad3
#define pad3Length ce_pad3Length
#include "SOUNDS/Crunch_E/pad3.h"
#undef pad3
#undef pad3Length
#include "SOUNDS/Crunch_E/pureSin.h"
#include "SOUNDS/Crunch_E/pureTriSoft.h"
#define ride1 ce_ride1
#define ride1Length ce_ride1Length
#include "SOUNDS/Crunch_E/ride1.h"
#undef ride1
#undef ride1Length
#define sfx1 ce_sfx1
#define sfx1Length ce_sfx1Length
#include "SOUNDS/Crunch_E/sfx1.h"
#undef sfx1
#undef sfx1Length
#define sfx2 ce_sfx2
#define sfx2Length ce_sfx2Length
#include "SOUNDS/Crunch_E/sfx2.h"
#undef sfx2
#undef sfx2Length
#define sfx3 ce_sfx3
#define sfx3Length ce_sfx3Length
#include "SOUNDS/Crunch_E/sfx3.h"
#undef sfx3
#undef sfx3Length
#include "SOUNDS/Crunch_E/sfx4.h"
#define sfx5 ce_sfx5
#define sfx5Length ce_sfx5Length
#include "SOUNDS/Crunch_E/sfx5.h"
#undef sfx5
#undef sfx5Length
#define sfx6 ce_sfx6
#define sfx6Length ce_sfx6Length
#include "SOUNDS/Crunch_E/sfx6.h"
#undef sfx6
#undef sfx6Length
#include "SOUNDS/Crunch_E/sfx7.h"
#include "SOUNDS/Crunch_E/sfx8.h"
#include "SOUNDS/Crunch_E/sfx9.h"
#include "SOUNDS/Crunch_E/sfx10.h"
#include "SOUNDS/Crunch_E/sfx11.h"
#include "SOUNDS/Crunch_E/sfx12.h"
#define snareB1 ce_snareB1
#define snareB1Length ce_snareB1Length
#include "SOUNDS/Crunch_E/snareB1.h"
#undef snareB1
#undef snareB1Length
#define snareB2 ce_snareB2
#define snareB2Length ce_snareB2Length
#include "SOUNDS/Crunch_E/snareB2.h"
#undef snareB2
#undef snareB2Length
#include "SOUNDS/Crunch_E/synth1.h"
#define synth2 ce_synth2
#define synth2Length ce_synth2Length
#include "SOUNDS/Crunch_E/synth2.h"
#undef synth2
#undef synth2Length
#include "SOUNDS/Crunch_E/synth3.h"

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

// ==================== SMPL ENGINE (Crunch_E one-shot samples per key) ====================
// Utilise les 45 samples Crunch_E: key % 45 sélectionne le sample
#define SMPL_POOL_SIZE 45

struct SmplSampleDesc {
  const int* data;
  uint32_t len;
};

// Array des 45 samples Crunch_E (ordre alphabétique: bass1, bass2, bongo1, ...)
static const SmplSampleDesc smplPool[SMPL_POOL_SIZE] = {
  {ce_bass1,     ce_bass1Length},    // 0
  {bass2,        bass2Length},       // 1
  {bongo1,       bongo1Length},      // 2
  {ce_clap1,     ce_clap1Length},    // 3
  {ce_crash1,    ce_crash1Length},   // 4
  {ce_guitar1,   ce_guitar1Length},  // 5
  {hihat1,       hihat1Length},      // 6
  {ce_hihat2,    ce_hihat2Length},   // 7
  {ce_jbass1,    ce_jbass1Length},   // 8
  {ce_jbass2,    ce_jbass2Length},   // 9
  {ce_jlead1,    ce_jlead1Length},   // 10
  {ce_jlead2,    ce_jlead2Length},   // 11
  {jlead3,       jlead3Length},      // 12
  {jlead4,       jlead4Length},      // 13
  {ce_jpad1,     ce_jpad1Length},    // 14
  {kick1,        kick1Length},       // 15
  {kick2,        kick2Length},       // 16
  {ce_kick3,     ce_kick3Length},    // 17
  {ce_pad1,      ce_pad1Length},     // 18
  {pad2,         pad2Length},        // 19
  {ce_pad3,      ce_pad3Length},     // 20
  {pureSin,      pureSinLength},     // 21
  {pureTriSoft,  pureTriSoftLength}, // 22
  {ce_ride1,     ce_ride1Length},    // 23
  {ce_sfx1,      ce_sfx1Length},     // 24
  {sfx10,        sfx10Length},       // 25
  {sfx11,        sfx11Length},       // 26
  {sfx12,        sfx12Length},       // 27
  {ce_sfx2,      ce_sfx2Length},     // 28
  {ce_sfx3,      ce_sfx3Length},     // 29
  {sfx4,         sfx4Length},        // 30
  {ce_sfx5,      ce_sfx5Length},     // 31
  {ce_sfx6,      ce_sfx6Length},     // 32
  {sfx7,         sfx7Length},        // 33
  {sfx8,         sfx8Length},        // 34
  {sfx9,         sfx9Length},        // 35
  {snare1,       snare1Length},      // 36
  {snare2,       snare2Length},      // 37
  {snare3,       snare3Length},      // 38
  {ce_snareB1,   ce_snareB1Length},  // 39
  {ce_snareB2,   ce_snareB2Length},  // 40
  {snareB3,      snareB3Length},     // 41
  {synth1,       synth1Length},      // 42
  {ce_synth2,    ce_synth2Length},   // 43
  {synth3,       synth3Length},      // 44
};

static SmplSampleDesc getSmplSampleDesc(uint8_t idx) {
  if (idx >= SMPL_POOL_SIZE) idx = 0;
  return smplPool[idx];
}

static float smplVoicePos[VOICE_COUNT] = {0.0f};
static uint8_t smplVoiceSmpIdx[VOICE_COUNT] = {0};  // index dans le pool (0-44, mappé via key % 45)
static bool smplVoiceActive[VOICE_COUNT] = {false};

void triggerSmplVoice(uint8_t voiceIdx, uint8_t key) {
  if (voiceIdx >= VOICE_COUNT) return;
  uint8_t si = key % SMPL_POOL_SIZE;
  smplVoiceSmpIdx[voiceIdx] = si;
  smplVoicePos[voiceIdx] = 0.0f;
  smplVoiceActive[voiceIdx] = true;
}

int16_t nextSmplVoiceSample(uint8_t voiceIdx) {
  if (voiceIdx >= VOICE_COUNT || !smplVoiceActive[voiceIdx]) return 0;
  
  SmplSampleDesc desc = getSmplSampleDesc(smplVoiceSmpIdx[voiceIdx]);
  if (desc.data == nullptr || desc.len < 2) {
    smplVoiceActive[voiceIdx] = false;
    return 0;
  }
  
  float pos = smplVoicePos[voiceIdx];
  if (pos >= (float)(desc.len - 1)) {
    smplVoiceActive[voiceIdx] = false;
    return 0;
  }
  
  uint32_t idx0 = (uint32_t)pos;
  uint32_t idx1 = idx0 + 1;
  float frac = pos - (float)idx0;
  int32_t a = (int32_t)desc.data[idx0];
  int32_t b = (int32_t)desc.data[idx1];
  int32_t interp = (int32_t)((1.0f - frac) * (float)a + frac * (float)b);
  smplVoicePos[voiceIdx] = pos + 1.0f;
  
  // Stronger attenuation for better balance with synth instruments (~-36 dB)
  int32_t s = (interp >> 6);
  return (int16_t)constrain(s, -32767, 32767);
}
