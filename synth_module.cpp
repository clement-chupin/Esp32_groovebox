#include "synth_module.h"
#include "controls_module.h"
#include <Oscil.h>
#include <ADSR.h>
#include <tables/sin2048_int8.h>
#include <tables/saw2048_int8.h>
#include <tables/square_no_alias_2048_int8.h>
#include <tables/triangle2048_int8.h>
#include <tables/waveshape_chebyshev_3rd_256_int8.h>
#include <tables/cos2048_int8.h>
#if ENABLE_SOUND_SYNTH_BANKS
#include "SOUNDS/SYNTH1.h"
#include "SOUNDS/SYNTH2.h"
#include "SOUNDS/SYNTH3.h"
#endif

extern bool loopTrackLocked;
unsigned long voiceNoteOnMs[VOICE_COUNT] = {0};

// Pre-computed semitone frequency ratios: pow(2, n/12) for n = 0..48
// Avoids expensive powf() calls during note-on and modulation.
static const float kSemiRatio[49] = {
     1.000000f,  1.059463f,  1.122462f,  1.189207f,  1.259921f,
     1.334840f,  1.414214f,  1.498307f,  1.587401f,  1.681793f,
     1.781797f,  1.887749f,  2.000000f,  2.118926f,  2.244924f,
     2.378414f,  2.519842f,  2.669680f,  2.828427f,  2.996614f,
     3.174802f,  3.363586f,  3.563595f,  3.775501f,  4.000000f,
     4.237852f,  4.489848f,  4.756828f,  5.039684f,  5.339360f,
     5.656854f,  5.993229f,  6.349604f,  6.727171f,  7.127190f,
     7.551003f,  8.000000f,  8.475705f,  8.979696f,  9.513657f,
    10.079369f, 10.678720f, 11.313708f, 11.986459f, 12.699208f,
    13.454342f, 14.254381f, 15.102005f, 16.000000f
};

// Linear interpolation of semitone ratio for fractional semitone values in [0, 8).
// Used by the pitch envelope which starts at 7 semitones and decays to 0.
static inline float semiToRatioFine(float semi) {
    int idx = (int)semi;
    if (idx < 0) return 1.0f;
    if (idx >= 8) return kSemiRatio[8];
    float frac = semi - (float)idx;
    return kSemiRatio[idx] + frac * (kSemiRatio[idx + 1] - kSemiRatio[idx]);
}
static float voiceSlideStartFreq[VOICE_COUNT] = {0.0f};
static float voiceSlideProgress[VOICE_COUNT] = {1.0f};

struct SlideHeldNote {
  bool active;
  uint8_t key;
  float baseFreq;
  float playFreq;
  uint8_t shape;
  uint8_t envMode;
  unsigned long pressedMs;
};

static SlideHeldNote slideHeldNotes[2][VOICE_COUNT] = {};

static int slideLayerIndex(bool loopVoice) {
  return loopVoice ? 1 : 0;
}

static void rememberSlideHeldNote(bool loopVoice, uint8_t key, float baseFreq, float playFreq, uint8_t shape, uint8_t envMode) {
  int layer = slideLayerIndex(loopVoice);
  int freeSlot = -1;
  int existingSlot = -1;
  int oldestSlot = 0;
  unsigned long oldestMs = 0xFFFFFFFFUL;

  for (int i = 0; i < VOICE_COUNT; i++) {
    if (slideHeldNotes[layer][i].active) {
      if (slideHeldNotes[layer][i].key == key) existingSlot = i;
      if (slideHeldNotes[layer][i].pressedMs < oldestMs) {
        oldestMs = slideHeldNotes[layer][i].pressedMs;
        oldestSlot = i;
      }
    } else if (freeSlot < 0) {
      freeSlot = i;
    }
  }

  int slot = (existingSlot >= 0) ? existingSlot : ((freeSlot >= 0) ? freeSlot : oldestSlot);
  slideHeldNotes[layer][slot] = {true, key, baseFreq, playFreq, shape, envMode, millis()};
}

static void forgetSlideHeldNote(bool loopVoice, uint8_t key) {
  int layer = slideLayerIndex(loopVoice);
  for (int i = 0; i < VOICE_COUNT; i++) {
    if (slideHeldNotes[layer][i].active && slideHeldNotes[layer][i].key == key) {
      slideHeldNotes[layer][i].active = false;
    }
  }
}

static int newestSlideHeldNoteSlot(bool loopVoice) {
  int layer = slideLayerIndex(loopVoice);
  int best = -1;
  unsigned long newestMs = 0;
  for (int i = 0; i < VOICE_COUNT; i++) {
    if (!slideHeldNotes[layer][i].active) continue;
    if (best < 0 || slideHeldNotes[layer][i].pressedMs >= newestMs) {
      newestMs = slideHeldNotes[layer][i].pressedMs;
      best = i;
    }
  }
  return best;
}

static void clearSlideHeldNotes() {
  for (int layer = 0; layer < 2; layer++) {
    for (int i = 0; i < VOICE_COUNT; i++) {
      slideHeldNotes[layer][i].active = false;
    }
  }
}

static void retargetSlideVoiceFromHeldStack(bool loopVoice) {
  int heldSlot = newestSlideHeldNoteSlot(loopVoice);
  if (heldSlot < 0) return;

  int voiceIdx = -1;
  unsigned long newestVoiceMs = 0;
  for (int i = 0; i < VOICE_COUNT; i++) {
    if (!voices[i].active || voices[i].loopVoice != loopVoice) continue;
    if (voiceIdx < 0 || voiceNoteOnMs[i] >= newestVoiceMs) {
      newestVoiceMs = voiceNoteOnMs[i];
      voiceIdx = i;
    }
  }
  if (voiceIdx < 0) return;

  const SlideHeldNote& held = slideHeldNotes[slideLayerIndex(loopVoice)][heldSlot];
  voices[voiceIdx].active = true;
  voices[voiceIdx].gate = true;
  voices[voiceIdx].loopVoice = loopVoice;
  voices[voiceIdx].key = held.key;
  voices[voiceIdx].shape = held.shape;
  voices[voiceIdx].envMode = held.envMode;
  voices[voiceIdx].baseFreq = held.baseFreq;
  voiceSlideStartFreq[voiceIdx] = voiceCurFreq[voiceIdx];
  voiceSlideProgress[voiceIdx] = 0.0f;
  voiceTargetFreq[voiceIdx] = held.playFreq;
#if ENABLE_SOUND_SYNTH_BANKS
  if (isSoundSynthShape(held.shape)) {
    voiceSoundSamplePosQ16[voiceIdx] = 0;
  }
#endif
  voiceNoteOnMs[voiceIdx] = millis();
}

#if ENABLE_SOUND_SYNTH_BANKS
struct SoundSynthSampleDesc {
  const int16_t* data;
  uint32_t len;
};

static const SoundSynthSampleDesc kSoundSynthBanks[SOUND_SYNTH_BANK_COUNT] = {
  {SYNTH1, (uint32_t)(sizeof(SYNTH1) / sizeof(SYNTH1[0]))},
  {SYNTH2, (uint32_t)(sizeof(SYNTH2) / sizeof(SYNTH2[0]))},
  {SYNTH3, (uint32_t)(sizeof(SYNTH3) / sizeof(SYNTH3[0]))}
};

static uint32_t voiceSoundSamplePosQ16[VOICE_COUNT] = {0};

static inline bool isSoundSynthShape(uint8_t shape) {
  return shape >= SOUND_SYNTH_SHAPE_FIRST && shape < SHAPE_COUNT;
}
#else
static inline bool isSoundSynthShape(uint8_t shape) {
  (void)shape;
  return false;
}
#endif

int16_t nextSoundInstrumentSample(uint8_t voiceIndex, uint8_t shapeIndex) {
#if ENABLE_SOUND_SYNTH_BANKS
  if (voiceIndex >= VOICE_COUNT || !isSoundSynthShape(shapeIndex)) return 0;

  uint8_t bank = (uint8_t)(shapeIndex - SOUND_SYNTH_SHAPE_FIRST);
  if (bank >= SOUND_SYNTH_BANK_COUNT) return 0;

  const SoundSynthSampleDesc& desc = kSoundSynthBanks[bank];
  if (desc.len < 2 || desc.data == nullptr) return 0;

  // Q16 fixed-point position: upper 16 bits = integer index, lower 16 bits = fraction.
  // Using integer interpolation avoids float division and float multiply per sample.
  uint32_t posQ16 = voiceSoundSamplePosQ16[voiceIndex];
  uint32_t lenQ16 = (uint32_t)desc.len << 16;
  while (posQ16 >= lenQ16) posQ16 -= lenQ16;

  uint32_t idx     = posQ16 >> 16;
  uint32_t idxNext = (idx + 1U < desc.len) ? (idx + 1U) : 0U;
  // Q8 fraction (0..255) is safe: max diff for int16_t data is ~65535, 65535*255 < INT32_MAX.
  int32_t frac8    = (int32_t)((posQ16 >> 8) & 0xFF);

  int32_t a = (int32_t)desc.data[idx];
  int32_t b = (int32_t)desc.data[idxNext];
  int32_t interp = a + (((b - a) * frac8) >> 8);

  // Compute increment from desired frequency; one float div happens only here.
  float incF = (voiceCurFreq[voiceIndex] * (float)desc.len) * (1.0f / (float)AUDIO_RATE);
  if (incF < 0.05f) incF = 0.05f;
  posQ16 += (uint32_t)(incF * 65536.0f);
  while (posQ16 >= lenQ16) posQ16 -= lenQ16;
  voiceSoundSamplePosQ16[voiceIndex] = posQ16;

  return (int16_t)(interp >> 8);
#else
  (void)voiceIndex;
  (void)shapeIndex;
  return 0;
#endif
}

// ==================== INITIALISATION ====================
void initSynth() {
  for (int i = 0; i < VOICE_COUNT; i++) {
    oscSin[i].setTable(SIN2048_DATA);
    oscSaw[i].setTable(SAW2048_DATA);
    oscSaw2[i].setTable(SAW2048_DATA);
    oscSqr[i].setTable(SQUARE_NO_ALIAS_2048_DATA);
    oscTri[i].setTable(TRIANGLE2048_DATA);
    oscCheby[i].setTable(CHEBYSHEV_3RD_256_DATA);
    oscCos[i].setTable(COS2048_DATA);
  #if ENABLE_SAMPLE_INSTRUMENT_ENGINE
    oscSample[i].setTable(BURROUGHS1_18649_DATA);
    oscSample[i].setFreq((float)BURROUGHS1_18649_SAMPLERATE / (float)BURROUGHS1_18649_NUM_CELLS);
    oscSample[i].setLoopingOn();
  #endif
    voices[i] = {false, false, false, 0, 0, ENV_MODE_NORMAL, 440.0f};
    setVoiceFreq(i, 440.0f);
  #if ENABLE_SOUND_SYNTH_BANKS
    voiceSoundSamplePosQ16[i] = 0;
  #endif
    voiceModPhase[i] = 0.0f;
    voiceModAmp[i] = 1.0f;
    voiceSlideStartFreq[i] = 440.0f;
    voiceSlideProgress[i] = 1.0f;
    envelope[i].setADLevels(255, 220);
    envelope[i].setTimes(10, 50, 600000, 200);
  }
}

// ==================== VARIABLES (non-Mozzi objects only - Mozzi objects are in .ino) ====================
// All Mozzi objects (Oscil, ADSR, etc.) are defined in clavier_v2.ino
// and declared as extern in synth_module.h

// ==================== CONVERSION NOTES / FREQUENCES ====================
float applyOctave(float hz, int8_t oct) {
  if (oct > 0) { for (int8_t i = 0; i < oct;  i++) hz *= 2.0f; }
  else         { for (int8_t i = 0; i > oct;  i--) hz *= 0.5f; }
  return constrain(hz, 20.0f, 6000.0f);
}

float keyToFreqColumnOrder(int key) {
  const float rootHz = 261.63f; // C4
  int row = key / COLS;
  int col = key % COLS;
  int semitone = col * 4 + row;
  float hz = rootHz * kSemiRatio[constrain(semitone, 0, 48)];
  return applyOctave(hz, octaveShift);
}

float keyToFreqPentatonic(int col) {
  static const int8_t pentatonic[COLS] = {0, 3, 5, 7, 10, 12, 15, 17};
  const float rootHz = 110.0f;
  int idx = constrain(col, 0, COLS - 1);
  float hz = rootHz * kSemiRatio[constrain((int)pentatonic[idx], 0, 48)];
  return applyOctave(hz, octaveShift);
}

float keyToFreqScale4x4(int row, int col) {
  extern uint8_t currentScaleIndex;
  int rr = constrain(row, 0, 3);
  int cc = constrain(col, 0, 3);
  int idx = rr * 4 + cc;
  float rootHz = 110.0f;
  int8_t semi = scaleMap[currentScaleIndex % 4][idx];
  float hz = rootHz * kSemiRatio[constrain((int)semi, 0, 48)];
  return applyOctave(hz, octaveShift);
}

float keyToFreqPentatonic4x4(int row, int col) {
  static const int8_t pent16[16] = {0,3,5,7, 10,12,15,17, 19,22,24,27, 29,31,34,36};
  int idx = constrain(row, 0, 3) * 4 + constrain(col, 0, 3);
  float rootHz = 110.0f;
  float hz = rootHz * kSemiRatio[constrain((int)pent16[idx], 0, 48)];
  return applyOctave(hz, octaveShift);
}

// ==================== GESTION DES ENVELOPPES ====================
void applyEnvPreset(int i, uint8_t envMode) {
  uint8_t idx = (uint8_t)constrain((int)envMode, 0, ENV_PRESET_COUNT - 1);
  envelope[i].setADLevels(255, envPresets[idx].sustainLevel);
  envelope[i].setTimes(
    envPresets[idx].attackMs,
    envPresets[idx].decayMs,
    600000,
    envPresets[idx].releaseMs
  );
}

// ==================== ARPEGGIATEUR ====================
int8_t currentArpSemitone() {
  if (cachedArpIndex <= 0) return 0;
  uint8_t stepCount = arpPresets[cachedArpIndex].stepCount;
  if (stepCount == 0) return 0;
  return arpPresets[cachedArpIndex].steps[arpStep % stepCount];
}

// ==================== GESTION DES VOIX ====================
void setVoicePlayFreq(int i, float playFreq) {
  voiceTargetFreq[i] = playFreq;
  if (voices[i].envMode != ENV_MODE_SLIDE || !voices[i].active) {
    voiceCurFreq[i] = playFreq;
  }
  setVoiceFreq(i, voiceCurFreq[i]);
}

void setVoiceFreq(int i, float freq) {
  oscSin[i].setFreq(freq);
  oscSaw[i].setFreq(freq);
  oscSaw2[i].setFreq(freq * 1.012f);   // légèrement désaccordé
  oscSqr[i].setFreq(freq);
  oscTri[i].setFreq(freq);
  oscCheby[i].setFreq(freq * 0.5f);    // sous-octave pour richesse
  oscCos[i].setFreq(freq * 1.5f);      // harmonique pour timbres plus brillants
  
  // Sample: ajuster la vitesse de lecture pour obtenir la fréquence souhaitée
  // Fréquence de base du sample = SAMPLERATE / NUM_CELLS
#if ENABLE_SAMPLE_INSTRUMENT_ENGINE
  float baseSampleFreq = (float)BURROUGHS1_18649_SAMPLERATE / (float)BURROUGHS1_18649_NUM_CELLS;
  oscSample[i].setFreq(baseSampleFreq * (freq / 261.63f));  // 261.63 Hz = C4 (référence)
#endif
}

void noteOnPatched(uint8_t key, float baseFreq, float playFreq, uint8_t shape, uint8_t envMode, bool loopVoice) {
  uint8_t safeShape = (uint8_t)constrain((int)shape, 0, SHAPE_COUNT - 1);
  uint8_t safeEnv = (uint8_t)constrain((int)envMode, 0, ENV_PRESET_COUNT - 1);

  if (safeEnv == ENV_MODE_SLIDE) {
    rememberSlideHeldNote(loopVoice, key, baseFreq, playFreq, safeShape, safeEnv);
    bool legato = false;
    int slideVoice = -1;
    unsigned long newestSlideMs = 0;
    for (int v = 0; v < VOICE_COUNT; v++) {
      if (voices[v].active && voices[v].gate && voices[v].loopVoice == loopVoice) {
        legato = true;
      }
      if (voices[v].active && voices[v].loopVoice == loopVoice && voiceNoteOnMs[v] >= newestSlideMs) {
        newestSlideMs = voiceNoteOnMs[v];
        slideVoice = v;
      }
    }

    if (slideVoice >= 0) {
      for (int v = 0; v < VOICE_COUNT; v++) {
        if (v == slideVoice) continue;
        if (voices[v].active && voices[v].loopVoice == loopVoice) {
          voices[v].active = false;
          voices[v].gate = false;
          envelope[v].noteOff();
        }
      }

      voices[slideVoice].active = true;
      voices[slideVoice].gate = true;
      voices[slideVoice].loopVoice = loopVoice;
      voices[slideVoice].key = key;
      voices[slideVoice].shape = safeShape;
      voices[slideVoice].envMode = safeEnv;
      voices[slideVoice].baseFreq = baseFreq;
      voiceSlideStartFreq[slideVoice] = voiceCurFreq[slideVoice];
      voiceSlideProgress[slideVoice] = 0.0f;
      voiceTargetFreq[slideVoice] = playFreq;
#if ENABLE_SOUND_SYNTH_BANKS
      if (isSoundSynthShape(safeShape)) {
        voiceSoundSamplePosQ16[slideVoice] = 0;
      }
#endif
      if (!legato) {
        applyEnvPreset(slideVoice, safeEnv);
        envelope[slideVoice].noteOn();
        voiceHoldGain[slideVoice] = 1.0f;
      }
      voiceNoteOnMs[slideVoice] = millis();
      return;
    }
  }

  for (int i = 0; i < VOICE_COUNT; i++) {
    if (voices[i].active && voices[i].key == key && voices[i].loopVoice == loopVoice) {
      voices[i].gate = true;
      voices[i].shape = safeShape;
      voices[i].envMode = safeEnv;
      voices[i].baseFreq = baseFreq;
      setVoicePlayFreq(i, playFreq);
#if ENABLE_SOUND_SYNTH_BANKS
      if (isSoundSynthShape(safeShape)) {
        voiceSoundSamplePosQ16[i] = 0;
      }
#endif
      voicePitchEnvSemi[i] = (safeEnv == ENV_MODE_PITCH) ? 7.0f : 0.0f;
      voiceHoldGain[i] = 1.0f;
      applyEnvPreset(i, safeEnv);
      envelope[i].noteOn();
      voiceNoteOnMs[i] = millis();
      return;
    }
  }

  int idx = -1;
  for (int i = 0; i < VOICE_COUNT; i++) {
    if (!voices[i].active) { idx = i; break; }
  }
  if (idx < 0) {
    unsigned long oldest = 0xFFFFFFFFUL;

    for (int i = 0; i < VOICE_COUNT; i++) {
      if (voices[i].loopVoice || voices[i].gate) continue;
      if (voiceNoteOnMs[i] < oldest) {
        oldest = voiceNoteOnMs[i];
        idx = i;
      }
    }

    if (idx < 0) {
      oldest = 0xFFFFFFFFUL;
      for (int i = 0; i < VOICE_COUNT; i++) {
        if (voices[i].gate) continue;
        if (voiceNoteOnMs[i] < oldest) {
          oldest = voiceNoteOnMs[i];
          idx = i;
        }
      }
    }

    if (idx < 0) {
      oldest = 0xFFFFFFFFUL;
      for (int i = 0; i < VOICE_COUNT; i++) {
        if (voices[i].loopVoice) continue;
        if (voiceNoteOnMs[i] < oldest) {
          oldest = voiceNoteOnMs[i];
          idx = i;
        }
      }
    }

    if (idx < 0) {
      oldest = 0xFFFFFFFFUL;
      for (int i = 0; i < VOICE_COUNT; i++) {
        if (voiceNoteOnMs[i] < oldest) {
          oldest = voiceNoteOnMs[i];
          idx = i;
        }
      }
    }
  }

  voices[idx] = {true, true, loopVoice, key, safeShape, safeEnv, baseFreq};
  voiceCurFreq[idx] = playFreq;
  setVoicePlayFreq(idx, playFreq);
#if ENABLE_SOUND_SYNTH_BANKS
  if (isSoundSynthShape(safeShape)) {
    voiceSoundSamplePosQ16[idx] = 0;
  }
#endif
  voicePitchEnvSemi[idx] = (safeEnv == ENV_MODE_PITCH) ? 7.0f : 0.0f;
  voiceHoldGain[idx] = 1.0f;
  voiceSlideStartFreq[idx] = playFreq;
  voiceSlideProgress[idx] = 1.0f;
  applyEnvPreset(idx, safeEnv);
  envelope[idx].noteOn();
  voiceNoteOnMs[idx] = millis();
}

void noteOn(uint8_t key, float baseFreq, float playFreq) {
  noteOnPatched(key, baseFreq, playFreq, (uint8_t)cachedShape, (uint8_t)cachedEnvIndex, false);
}

void noteOff(uint8_t key) {
  bool releasedSlide = false;
  forgetSlideHeldNote(false, key);
  bool slideFallback = newestSlideHeldNoteSlot(false) >= 0;
  for (int i = 0; i < VOICE_COUNT; i++) {
    if (voices[i].active && !voices[i].loopVoice && voices[i].key == key) {
      if (voices[i].envMode == ENV_MODE_SLIDE) {
        releasedSlide = true;
        if (slideFallback) {
          continue;
        }
      }
      voices[i].gate = false;
      envelope[i].noteOff();
    }
  }
  if (releasedSlide) {
    retargetSlideVoiceFromHeldStack(false);
  }
}

void noteOffLoopKey(uint8_t key) {
  bool releasedSlide = false;
  forgetSlideHeldNote(true, key);
  bool slideFallback = newestSlideHeldNoteSlot(true) >= 0;
  for (int i = 0; i < VOICE_COUNT; i++) {
    if (voices[i].active && voices[i].loopVoice && voices[i].key == key) {
      if (voices[i].envMode == ENV_MODE_SLIDE) {
        releasedSlide = true;
        if (slideFallback) {
          continue;
        }
      }
      voices[i].gate = false;
      envelope[i].noteOff();
    }
  }
  if (releasedSlide) {
    retargetSlideVoiceFromHeldStack(true);
  }
}

void allNotesOff() {
  clearSlideHeldNotes();
  for (int i = 0; i < VOICE_COUNT; i++) {
    voices[i].active = false;
    voices[i].gate = false;
    voices[i].loopVoice = false;
    envelope[i].noteOff();
  }
}

// ==================== UPDATE CONTROL ====================
void updateSynthControl() {
  // Retune immediat des notes tenues quand l'octave change
  if (octaveShift != lastOctaveShift) {
    for (int i = 0; i < VOICE_COUNT; i++) {
      if (voices[i].active) {
        if (voices[i].loopVoice && loopTrackLocked) continue;
        float base = keyToFreqColumnOrder(voices[i].key);
        float play = base;
        voices[i].baseFreq = base;
        if (cachedArpIndex > 0 && voices[i].gate) {
          int8_t arpSemi = currentArpSemitone();
          play *= kSemiRatio[constrain((int)arpSemi, 0, 48)];
        }

        // Keep internal voice state aligned so later slide/pitch/vibrato keeps the new octave.
        voiceTargetFreq[i] = play;
        voiceCurFreq[i] = play;
        setVoiceFreq(i, play);
      }
    }
    lastOctaveShift = octaveShift;
  }

  // Mise à jour enveloppes synth (doit être ici, pas dans updateAudio)
  for (int i = 0; i < VOICE_COUNT; i++) {
    envelope[i].update();
    
    // Pitch envelope et slide
    if (voices[i].envMode == ENV_MODE_PITCH && voicePitchEnvSemi[i] > 0.0f) {
      voicePitchEnvSemi[i] *= 0.985f;
      if (voicePitchEnvSemi[i] < 0.01f) voicePitchEnvSemi[i] = 0.0f;
      float pitchMul = semiToRatioFine(voicePitchEnvSemi[i]);
      setVoiceFreq(i, voiceCurFreq[i] * pitchMul);
    }
    
    if (voices[i].envMode == ENV_MODE_SLIDE) {
      float clampedBpm = constrain((float)bpm, 40.0f, 240.0f);
      float slideBeats = 0.35f;
      float slideSeconds = (60.0f * slideBeats) / clampedBpm;
      float speed = 1.0f / max(slideSeconds * (float)CONTROL_RATE, 1.0f);
      if (speed > 0.12f) speed = 0.12f;
      if (voiceSlideProgress[i] < 1.0f) {
        voiceSlideProgress[i] += speed;
        if (voiceSlideProgress[i] > 1.0f) voiceSlideProgress[i] = 1.0f;
        float t = voiceSlideProgress[i];
        float eased = t * t * (3.0f - 2.0f * t);
        voiceCurFreq[i] = voiceSlideStartFreq[i] + (voiceTargetFreq[i] - voiceSlideStartFreq[i]) * eased;
      } else {
        voiceCurFreq[i] += (voiceTargetFreq[i] - voiceCurFreq[i]) * 0.02f;
      }
      setVoiceFreq(i, voiceCurFreq[i]);
    }
    
    if (voices[i].envMode == ENV_MODE_FADE && voices[i].gate) {
      if (voiceHoldGain[i] > 0.0f) {
        voiceHoldGain[i] -= 0.0015f;
        if (voiceHoldGain[i] < 0.0f) voiceHoldGain[i] = 0.0f;
      }
    }

    if (!voices[i].active) {
      voiceModAmp[i] = 1.0f;
      continue;
    }

    // Mouvement lent par instrument: variations fines de fréquence + amplitude.
    float speed = 0.015f + 0.004f * (float)(voices[i].shape % 6);
    voiceModPhase[i] += speed;
    if (voiceModPhase[i] > 6.28318f) voiceModPhase[i] -= 6.28318f;

    float depthSemi = 0.04f + 0.015f * (float)(voices[i].shape % 5);
    // For tiny exponents (max ~0.00958 = 0.115/12), 2^x ≈ 1 + x*ln(2).
    // Maximum relative error at x=0.00958: |2^x - (1+x*ln2)| / 2^x < 0.003%.
    static const float kLn2 = 0.693147f;  // natural log of 2
    float exponent = (sinf(voiceModPhase[i]) * depthSemi) * (1.0f / 12.0f);
    float vibMul = 1.0f + exponent * kLn2;
    voiceModAmp[i] = 0.92f + 0.08f * (0.5f + 0.5f * sinf(voiceModPhase[i] * 0.7f + (float)i * 0.3f));

    float modFreq = voiceCurFreq[i] * vibMul;
    setVoiceFreq(i, modFreq);
  }
}
