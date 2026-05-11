#ifndef Voice_h
#define Voice_h

#include <cstdint>

class CrunchVoice {
public:
  bool samplerMode;
  int bps;
  int arpNum;
  int delay;
  int octave;
  bool overdrive;
  int recOctave;
  CrunchVoice();
  int UpdateVoice();
  int phaserMult;
  int lowPassMult;
  int reverbMult;
  int chordMult;
  int pitchMult;
  int arpMode;
  int delayMult;
  int whooshMult;
  int ladderMult;
  int flangerMult;
  bool slideMode;
  void AdvanceArpStep();
  void SetNote(int val, bool delay, int optOctave, int optInstrument);
  void SetVolume(int val);
  void SetOctave(int val);
  void SetDelay(int val);
  void SetEnvelopeNum(int val);
  void SetEnvelopeLength(int val);
  void SetEffectNum(int val);
  void ResetEffects();
  int volume;
  bool soloMute;
  bool mute;

private:
  int16_t noteFreqLookup[48];
  int pitchDur;
  int chordStep;
  int arpBeatIndex;
  int arpSemitoneOffset;
  int arpOctaveOffset;
  int whooshOffset;
  int16_t whooshSin[101];
  int oldArpNum;
  int16_t envelopes[4][101];
  int envelopeIndex;
  int sample;
  int effect;
  int sampleLen;
  int phaserOffset;
  int phaserDir;
  bool isDelay;
  int envelopeLength;
  int envelope;
  int envelopeNum;
  int voiceNum;
  int output;
  int note;
  int ladderState1;
  int ladderState2;
  int ladderState3;
  int ladderState4;
  int flangerPhase;
  // Keep history conservative for ESP32-WROOM DRAM budget.
  static constexpr int HISTORY_SIZE = 1024;
  static constexpr int HISTORY_STEPS = HISTORY_SIZE * 2;
  int16_t sampleHistory[HISTORY_SIZE];
  int sampleHistoryIndex = 0;
  int lastSample;
  int baseFreq;
  int baseFreqTarget;
  int baseFreq_ch1;
  int baseFreq_ch1Target;
  int baseFreq_ch2;
  int baseFreq_ch2Target;
  int baseFreq_ch3;
  int baseFreq_ch3Target;
  int baseFreq_ch4;
  int baseFreq_ch4Target;
  long sampleIndex;
  int sampleIndexNext;
  int subSampleIndex;
  int volumeNum;
  int ReadWaveform();
  int ReadDrumWaveform();
  int ReadSfxWaveform();
  int GetBaseFreq(int val, int ioctave);
  float GetVolumeRatio();
  void UpdateHistory(int);
  int GetHistorySample(int backOffset);
};
#endif
