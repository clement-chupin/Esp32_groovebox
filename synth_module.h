#pragma once

#include <Arduino.h>
#include <Oscil.h>
#include <ADSR.h>
#include <Sample.h>
#include <tables/sin2048_int8.h>
#include <tables/saw2048_int8.h>
#include <tables/square_no_alias_2048_int8.h>
#include <tables/triangle2048_int8.h>
#include <tables/waveshape_chebyshev_3rd_256_int8.h>
#include <tables/cos2048_int8.h>
#if ENABLE_SAMPLE_INSTRUMENT_ENGINE
#include <samples/burroughs1_18649_int8.h>
#endif
#include "config_module.h"

// ==================== INIT ====================
void initSynth();

// ==================== VOIX SYNTH ====================
struct Voice { 
  bool active; 
  bool gate; 
  bool loopVoice;
  uint8_t key; 
  uint8_t shape;
  uint8_t envMode;
  float baseFreq; 
};

extern Voice voices[VOICE_COUNT];
extern float voiceCurFreq[VOICE_COUNT];
extern float voiceTargetFreq[VOICE_COUNT];
extern float voicePitchEnvSemi[VOICE_COUNT];
extern float voiceHoldGain[VOICE_COUNT];
extern float voiceModPhase[VOICE_COUNT];
extern float voiceModAmp[VOICE_COUNT];
extern float voiceEnvFreqMul[VOICE_COUNT];
extern float voiceEnvAmpMul[VOICE_COUNT];
extern unsigned long voiceNoteOnMs[VOICE_COUNT];

extern Oscil<SIN2048_NUM_CELLS, AUDIO_RATE> oscSin[VOICE_COUNT];
extern Oscil<SAW2048_NUM_CELLS, AUDIO_RATE> oscSaw[VOICE_COUNT];
extern Oscil<SAW2048_NUM_CELLS, AUDIO_RATE> oscSaw2[VOICE_COUNT];
extern Oscil<SQUARE_NO_ALIAS_2048_NUM_CELLS, AUDIO_RATE> oscSqr[VOICE_COUNT];
extern Oscil<TRIANGLE2048_NUM_CELLS, AUDIO_RATE> oscTri[VOICE_COUNT];
extern Oscil<CHEBYSHEV_3RD_256_NUM_CELLS, AUDIO_RATE> oscCheby[VOICE_COUNT];
extern Oscil<COS2048_NUM_CELLS, AUDIO_RATE> oscCos[VOICE_COUNT];
#if ENABLE_SAMPLE_INSTRUMENT_ENGINE
extern Sample<BURROUGHS1_18649_NUM_CELLS, AUDIO_RATE> oscSample[VOICE_COUNT];
#endif
extern ADSR<CONTROL_RATE, AUDIO_RATE> envelope[VOICE_COUNT];

// ==================== SYNTH STATE ====================
extern volatile int cachedShape;
extern volatile int cachedEnvIndex;
extern int8_t octaveShift;
extern int8_t lastOctaveShift;

// ==================== ARPEGGIATOR ====================
extern int cachedArpIndex;
extern uint8_t arpStep;
extern unsigned long arpLastMs;
extern float arpRateHz;
extern int8_t userArpSteps[4];
extern uint8_t userArpWritePos;

// ==================== FUNCTIONS ====================
// Conversion notes/fréquences
float applyOctave(float hz, int8_t oct);
float keyToFreqColumnOrder(int key);
float keyToFreqPentatonic(int col);
float keyToFreqScale4x4(int row, int col);
float keyToFreqPentatonic4x4(int row, int col);

// Gestion des voix
void applyEnvPreset(int i, uint8_t envMode);
int8_t currentArpSemitone();
void setVoicePlayFreq(int i, float playFreq);
void setVoiceFreq(int i, float freq);
void noteOn(uint8_t key, float baseFreq, float playFreq);
void noteOnPatched(uint8_t key, float baseFreq, float playFreq, uint8_t shape, uint8_t envMode, bool loopVoice);
void noteOff(uint8_t key);
void noteOffLoopKey(uint8_t key);
void allNotesOff();
int16_t nextSoundInstrumentSample(uint8_t voiceIndex, uint8_t shapeIndex);
int16_t nextSmplDrumInstrumentSample(uint8_t voiceIndex, uint8_t key);

// Initialisation
void updateSynthControl();
