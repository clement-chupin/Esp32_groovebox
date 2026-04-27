#pragma once

#include <Arduino.h>

float applyOctave(float hz, int8_t oct);
float keyToFreqColumnOrder(int key);
float keyToFreqPentatonic(int col);

void audioOutput(const AudioOutput f);
void applyEnvPreset(int i, uint8_t envMode);
int8_t currentArpSemitone();

void setVoicePlayFreq(int i, float playFreq);
void setVoiceFreq(int i, float freq);
void noteOn(uint8_t key, float baseFreq, float playFreq);
void noteOff(uint8_t key);

void triggerDrum(uint8_t row);
void triggerDrumVoice(uint8_t row, uint8_t variant);

void runDrumSequencer();
AudioOutput_t updateAudio();
