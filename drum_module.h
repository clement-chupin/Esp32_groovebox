#pragma once

#include <Arduino.h>
#include <Oscil.h>
#include <ADSR.h>
#include <Sample.h>
#include <tables/sin2048_int8.h>
#if ENABLE_BURROUGHS_KICK_SAMPLE
#include <samples/burroughs1_18649_int8.h>
#endif
#include "config_module.h"

// ==================== INIT ====================
void initDrums();

// ==================== DRUM STATE ====================
extern bool drumActive[DRUM_ROWS];
extern unsigned long drumTrigMs[DRUM_ROWS];
extern float kickFreqCurrent;
extern float snareFreqCurrent;
extern float hatFreqCurrent;
extern float clapFreqCurrent;
extern uint8_t drumMixAmount;
extern float drumGlobalGain;

extern Oscil<SIN2048_NUM_CELLS, AUDIO_RATE> drumSinKick;
extern Oscil<SIN2048_NUM_CELLS, AUDIO_RATE> drumSinSnare;
extern Oscil<SIN2048_NUM_CELLS, AUDIO_RATE> drumSinHat;
extern Oscil<SIN2048_NUM_CELLS, AUDIO_RATE> drumSinClap;
extern ADSR<CONTROL_RATE, AUDIO_RATE> drumEnv[DRUM_ROWS];

#if ENABLE_BURROUGHS_KICK_SAMPLE
extern Sample<BURROUGHS1_18649_NUM_CELLS, AUDIO_RATE> drumSample;
extern volatile bool drumSampleActive;  // volatile: accédée depuis ISR audio
extern volatile uint8_t drumSampleRole;  // volatile: accédée depuis ISR audio
#endif

// ==================== SEQUENCER STATE ====================
extern bool drumPattern[DRUM_ROWS][DRUM_MAX_STEPS];
extern uint8_t drumStep;
extern unsigned long lastStepMs;
extern bool drumRun;
extern int currentDrumBank;
extern float drumPitch;
extern float drumAmplitude;
extern uint8_t drumDivisionIndex;  // 0->1/4, 1->1/8, 2->1/16
extern uint8_t drumEditPage;       // 0/1 when division is 1/16

// ==================== FUNCTIONS ====================
// LFSR pour génération de bruit
int8_t nextNoise();

// Déclenchement des drums
void triggerDrum(uint8_t row);

// Séquenceur
void runDrumSequencer();
void resetDrumTransport(bool keepRunning);
uint8_t currentDrumSteps();
bool isSoundDrumBank(uint8_t bankIndex);
int16_t nextSoundDrumSample(uint8_t row);
bool getSoundDrumPoolSample(uint8_t poolIndex, const int*& data, uint32_t& len);
void stopAllDrumVoices();

// Initialisation et update
void updateDrumsControl();
