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
    voiceSoundSamplePos[voiceIdx] = 0.0f;
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

static float voiceSoundSamplePos[VOICE_COUNT] = {0.0f};

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

  float pos = voiceSoundSamplePos[voiceIndex];
  while (pos >= (float)desc.len) pos -= (float)desc.len;
  while (pos < 0.0f) pos += (float)desc.len;

  uint32_t idx = (uint32_t)pos;
  uint32_t idxNext = (idx + 1U < desc.len) ? (idx + 1U) : 0U;
  float frac = pos - (float)idx;

  int32_t a = (int32_t)desc.data[idx];
  int32_t b = (int32_t)desc.data[idxNext];
  int32_t interp = (int32_t)((1.0f - frac) * (float)a + frac * (float)b);

  float inc = (voiceCurFreq[voiceIndex] * (float)desc.len) / (float)AUDIO_RATE;
  if (inc < 0.05f) inc = 0.05f;
  pos += inc;
  while (pos >= (float)desc.len) pos -= (float)desc.len;
  voiceSoundSamplePos[voiceIndex] = pos;

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
    voiceSoundSamplePos[i] = 0.0f;
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
  int noteIndex = col * 4 + row;
  if (noteIndex < 0) noteIndex = 0;

  extern uint8_t currentScaleIndex;
  uint8_t scale = (uint8_t)(currentScaleIndex % 4);

  int semitone = noteIndex;
  if (scale == 1) {
    static const uint8_t major[7] = {0, 2, 4, 5, 7, 9, 11};
    semitone = (noteIndex / 7) * 12 + major[noteIndex % 7];
  } else if (scale == 2) {
    static const uint8_t minor[7] = {0, 2, 3, 5, 7, 8, 10};
    semitone = (noteIndex / 7) * 12 + minor[noteIndex % 7];
  } else if (scale == 3) {
    static const uint8_t penta[5] = {0, 2, 4, 7, 9};
    semitone = (noteIndex / 5) * 12 + penta[noteIndex % 5];
  }

  float hz = rootHz * powf(2.0f, (float)semitone / 12.0f);
  return applyOctave(hz, octaveShift);
}

float keyToFreqPentatonic(int col) {
  static const int8_t pentatonic[COLS] = {0, 3, 5, 7, 10, 12, 15, 17};
  const float rootHz = 110.0f;
  int idx = constrain(col, 0, COLS - 1);
  float hz = rootHz * powf(2.0f, (float)pentatonic[idx] / 12.0f);
  return applyOctave(hz, octaveShift);
}

float keyToFreqScale4x4(int row, int col) {
  extern uint8_t currentScaleIndex;
  int rr = constrain(row, 0, 3);
  int cc = constrain(col, 0, 3);
  int idx = rr * 4 + cc;
  float rootHz = 110.0f;
  int8_t semi = scaleMap[currentScaleIndex % 4][idx];
  float hz = rootHz * powf(2.0f, (float)semi / 12.0f);
  return applyOctave(hz, octaveShift);
}

float keyToFreqPentatonic4x4(int row, int col) {
  static const int8_t pent16[16] = {0,3,5,7, 10,12,15,17, 19,22,24,27, 29,31,34,36};
  int idx = constrain(row, 0, 3) * 4 + constrain(col, 0, 3);
  float rootHz = 110.0f;
  float hz = rootHz * powf(2.0f, (float)pent16[idx] / 12.0f);
  return applyOctave(hz, octaveShift);
}

// ==================== GESTION DES ENVELOPPES ====================
void applyEnvPreset(int i, uint8_t envMode) {
  uint8_t idx = (uint8_t)constrain((int)envMode, 0, ENV_PRESET_COUNT - 1);
  uint16_t sustainMs = 600000;
  switch (idx) {
    // Natural auto-decay to zero only for Pluck, Pad and Piano variants.
    case ENV_MODE_PLUCK: sustainMs = 240; break;
    case ENV_MODE_PAD:   sustainMs = 2600; break;
    case ENV_MODE_PIANO: sustainMs = 520; break;
    case ENV_MODE_PIANO2: sustainMs = 1200; break;
    default:             sustainMs = 600000; break;
  }
  envelope[i].setADLevels(255, envPresets[idx].sustainLevel);
  envelope[i].setTimes(
    envPresets[idx].attackMs,
    envPresets[idx].decayMs,
    sustainMs,
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
        voiceSoundSamplePos[slideVoice] = 0.0f;
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
        voiceSoundSamplePos[i] = 0.0f;
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
    voiceSoundSamplePos[idx] = 0.0f;
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
          play *= powf(2.0f, (float)currentArpSemitone() / 12.0f);
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
      float pitchMul = powf(2.0f, voicePitchEnvSemi[i] / 12.0f);
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
    float vibMul = powf(2.0f, (sinf(voiceModPhase[i]) * depthSemi) / 12.0f);
    voiceModAmp[i] = 0.92f + 0.08f * (0.5f + 0.5f * sinf(voiceModPhase[i] * 0.7f + (float)i * 0.3f));

    float modFreq = voiceCurFreq[i] * vibMul;
    setVoiceFreq(i, modFreq);
  }
}
