#include <Arduino.h>
#include "Voice.h"
#include "config_module.h"

//drums

#include "SOUNDS/MothOS_Sample/kick3.h"
#include "SOUNDS/MothOS_Sample/hihat2.h"
#include "SOUNDS/MothOS_Sample/snareB1.h"
#include "SOUNDS/MothOS_Sample/snareB2.h"
#include "SOUNDS/MothOS_Sample/clap1.h"
#include "SOUNDS/MothOS_Sample/crash1.h"
#include "SOUNDS/MothOS_Sample/ride1.h"

//sfx

#include "SOUNDS/MothOS_Sample/sfx1.h"
#include "SOUNDS/MothOS_Sample/sfx2.h"
#include "SOUNDS/MothOS_Sample/sfx3.h"
#include "SOUNDS/MothOS_Sample/sfx5.h"
// sfx6 is used for instrument slot 12; include it outside the full-bank guard to avoid duplication.
#if !USE_FULL_SFX_BANK
#include "SOUNDS/MothOS_Sample/sfx6.h"
#endif
#if USE_FULL_SFX_BANK
#include "SOUNDS/MothOS_Sample/sfx4.h"
#include "SOUNDS/MothOS_Sample/sfx6.h"
#include "SOUNDS/MothOS_Sample/sfx7.h"
#include "SOUNDS/MothOS_Sample/sfx8.h"
#include "SOUNDS/MothOS_Sample/sfx9.h"
#include "SOUNDS/MothOS_Sample/sfx10.h"
#include "SOUNDS/MothOS_Sample/sfx11.h"
#include "SOUNDS/MothOS_Sample/sfx12.h"
#endif

//instruments
#include "SOUNDS/MothOS_Sample/bass1.h"
#include "SOUNDS/MothOS_Sample/jbass2.h"
#include "SOUNDS/MothOS_Sample/pad1.h"
#include "SOUNDS/MothOS_Sample/jpad1.h"
#include "SOUNDS/MothOS_Sample/pad3.h"
#include "SOUNDS/MothOS_Sample/guitar1.h"
#include "SOUNDS/MothOS_Sample/synth2.h"
#include "SOUNDS/MothOS_Sample/jbass1.h"
#include "SOUNDS/MothOS_Sample/jlead1.h"
#include "SOUNDS/MothOS_Sample/jlead2.h"

CrunchVoice::CrunchVoice() {
  samplerMode = false;
  bps = 1;
  arpNum = 0;
  delay = 0;
  overdrive = false;
  recOctave = 0;
  phaserMult = 0;
  lowPassMult = 0;
  reverbMult = 0;
  chordMult = 0;
  pitchMult = 0;
  arpMode = 0;
  delayMult = 0;
  whooshMult = 0;
  ladderMult = 0;
  flangerMult = 0;
  slideMode = false;
  soloMute = false;
  mute = false;
  pitchDur = 0;
  chordStep = 0;
  arpBeatIndex = 0;
  arpSemitoneOffset = 0;
  arpOctaveOffset = 0;
  whooshOffset = 0;
  oldArpNum = 0;
  envelopeIndex = 0;
  sample = 0;
  effect = 0;
  sampleLen = 0;
  phaserOffset = 0;
  phaserDir = 1;
  isDelay = false;
  envelopeLength = 0;
  envelope = 0;
  envelopeNum = 0;
  voiceNum = 2;
  output = 0;
  note = 7;
  ladderState1 = 0;
  ladderState2 = 0;
  ladderState3 = 0;
  ladderState4 = 0;
  flangerPhase = 0;
  sampleHistoryIndex = 0;
  lastSample = 0;
  baseFreq = 500;
  baseFreqTarget = 500;
  baseFreq_ch1 = 0;
  baseFreq_ch1Target = 0;
  baseFreq_ch2 = 0;
  baseFreq_ch2Target = 0;
  baseFreq_ch3 = 0;
  baseFreq_ch3Target = 0;
  baseFreq_ch4 = 0;
  baseFreq_ch4Target = 0;
  sampleIndexNext = 0;
  subSampleIndex = 0;
  volumeNum = 0;

  for (int i = 0; i < HISTORY_SIZE; i++) {
    sampleHistory[i] = 0;
  }

  for (int i = 0; i < 48; i++) {
    int valOut = (int)(250 * pow(((i + 12) / 12.0), 2));
    noteFreqLookup[i] = valOut;
  }

  octave = 1;
  sampleIndex = kick3Length * 1000;
  SetEnvelopeLength(1);

  volume = 2;
  ResetEffects();

  for (int i = 0; i <= 100; i++) {

    whooshSin[i] = (int)((cos((float)i / 50.0 * 3.14) + 1.0) * 5.0);

    int i2 = (200 - i * 2);
    int i3 = i * 2;
    if (i2 > 100)
      i2 = 100;
    if (i3 > 100)
      i3 = 100;


    envelopes[0][i] = i2;
    envelopes[1][i] = i3;
    envelopes[2][i] = 100;
    envelopes[3][i] = 100;
    if (i > 95) {
      //fade out envs
      envelopes[0][i] /= 1 + ((i - 95) * 20);
      envelopes[1][i] /= 1 + ((i - 95) * 20);
    }
  }
}


int CrunchVoice::UpdateVoice() {

  int sample = 0;
  if (voiceNum > 1 || samplerMode) {
    sample = ReadWaveform();
  } else {
    sample = ReadDrumWaveform();
  }

  if (phaserMult > 0) {
    if (phaserDir >= 0) {
      phaserOffset++;
      if (phaserOffset > 2000 * phaserMult) {
        phaserDir = -1;
      }
    } else {
      phaserOffset--;
      if (phaserOffset < 0) {
        phaserDir = 1;
      }
    }
    int rSample = GetHistorySample(phaserOffset);
    sample = (sample + rSample) / 2;
  }

  if (delayMult > 0) {
    sample += GetHistorySample(delayMult * 11600 / bps) / 5;
  }

  if (whooshMult > 0) {

    whooshOffset += whooshMult * bps / 2;
    if (whooshOffset > 15000)
      whooshOffset = 0;
    int whoosh = whooshSin[whooshOffset / 150] + 1;
    for (int i = 0; i < whoosh; i++) {
      sample += GetHistorySample(i + 1);
    }
    sample /= whoosh + 1;
  }

  if (lowPassMult > 0) {
    for (int i = 1; i < 4 * lowPassMult; i++) {
      sample += GetHistorySample(i);
    }
    sample /= 4 * lowPassMult;
  }

  if (ladderMult > 0) {
    int poleShift = 4 - ladderMult;
    if (poleShift < 1) poleShift = 1;
    int resonanceQ8 = 120 + (ladderMult * 36);
    int input = sample - ((ladderState4 * resonanceQ8) >> 8);
    ladderState1 += (input - ladderState1) >> poleShift;
    ladderState2 += (ladderState1 - ladderState2) >> poleShift;
    ladderState3 += (ladderState2 - ladderState3) >> poleShift;
    ladderState4 += (ladderState3 - ladderState4) >> poleShift;
    sample = ladderState4;
  }

  if (flangerMult > 0) {
    flangerPhase += (2 + flangerMult);
    if (flangerPhase >= 1024) flangerPhase -= 1024;
    int tri = (flangerPhase < 512) ? flangerPhase : (1023 - flangerPhase);
    int baseDelay = 18;
    int depth = 30 + (flangerMult * 28);
    int back = baseDelay + ((tri * depth) >> 9);
    int delayed = GetHistorySample(back);
    sample = (sample * 3 + delayed * 2) / 5;
  }

  UpdateHistory(sample);

  if (reverbMult > 0) {
    int wetSample = 0;
    int tapCount = 0;
    const int tapSpacing = 280 * reverbMult;
    for (int i = 1; i <= 4; i++) {
      wetSample += GetHistorySample(i * tapSpacing) / (i + 1);
      tapCount++;
    }

    if (tapCount > 0) {
      wetSample /= tapCount;
    }

    // Keep the original attack and mix in a softer tail to avoid metallic artifacts.
    sample = ((sample * 3) + (wetSample * 2)) / 5;
  }
  if (soloMute || mute) {
    sample = 0;
  }

  if (overdrive) {
    sample = sample * 10 / 7;
    int limit = 5000;
    if (sample > limit) {
      sample = limit;
    } else if (sample < -limit) {
      sample = -limit;
    }
  }
  return sample;
}

//look up how to refactor this in c++, use a list or dict not a switch
int CrunchVoice::ReadWaveform() {
  int sampleLen = 0;
  int sampleNext = 0;
  int sampleIndexReduced = sampleIndex / 1000;
  int vSel = voiceNum;

  if (slideMode && !samplerMode && voiceNum > 1) {
    int d = baseFreqTarget - baseFreq;
    if (d != 0) {
      int step = d / 32;
      if (step == 0) {
        step = (d > 0) ? 1 : -1;
      }
      baseFreq += step;
    }

    d = baseFreq_ch1Target - baseFreq_ch1;
    if (d != 0) {
      int step = d / 32;
      if (step == 0) {
        step = (d > 0) ? 1 : -1;
      }
      baseFreq_ch1 += step;
    }

    d = baseFreq_ch2Target - baseFreq_ch2;
    if (d != 0) {
      int step = d / 32;
      if (step == 0) {
        step = (d > 0) ? 1 : -1;
      }
      baseFreq_ch2 += step;
    }

    d = baseFreq_ch3Target - baseFreq_ch3;
    if (d != 0) {
      int step = d / 32;
      if (step == 0) {
        step = (d > 0) ? 1 : -1;
      }
      baseFreq_ch3 += step;
    }

    d = baseFreq_ch4Target - baseFreq_ch4;
    if (d != 0) {
      int step = d / 32;
      if (step == 0) {
        step = (d > 0) ? 1 : -1;
      }
      baseFreq_ch4 += step;
    }
  }

  int baseFreqLocal = baseFreq;

  if (arpMode > 0 && arpMode != 4 && !samplerMode && vSel > 1) {
    int ioct = (recOctave > -1) ? recOctave : octave;
    baseFreqLocal = GetBaseFreq(note + arpSemitoneOffset, ioct + arpOctaveOffset);
  }

  if (samplerMode) {
    vSel = note;
    int oct = octave + 1;
    if (recOctave > -1)
      oct = recOctave + 1;

    baseFreqLocal = oct * 500;
    if (vSel < 2) {
      return 0;
    }
  }

  switch (vSel) {
    case 2:
      sampleLen = bass1Length;
      sample = bass1[sampleIndexReduced];
      break;
    case 3:
      sampleLen = jbass2Length;
      sample = jbass2[sampleIndexReduced];
      break;
    case 4:
      sampleLen = pad1Length;
      sample = pad1[sampleIndexReduced];
      break;
    case 5:
      sampleLen = jpad1Length;
      sample = jpad1[sampleIndexReduced];
      break;
    case 6:
      sampleLen = pad3Length;
      sample = pad3[sampleIndexReduced];
      break;
    case 7:
      sampleLen = guitar1Length;
      sample = guitar1[sampleIndexReduced];
      break;
    case 8:
      sampleLen = synth2Length;
      sample = synth2[sampleIndexReduced];
      break;
    case 9:
      sampleLen = jbass1Length;
      sample = jbass1[sampleIndexReduced];
      break;
    case 10:
      sampleLen = jlead1Length;
      sample = jlead1[sampleIndexReduced];
      break;
    case 11:
      sampleLen = jlead2Length;
      sample = jlead2[sampleIndexReduced];
      break;
    case 12:
      sampleLen = sfx6Length;
      sample = sfx6[sampleIndexReduced];
      break;
  }

  if (pitchMult > 0) {
    if (pitchDur > 0) {
      pitchDur -= pitchMult;
      if (pitchMult == 1)
        baseFreqLocal -= pitchDur / 5;
      if (pitchMult == 2)
        baseFreqLocal += pitchDur / 5;
    }
  }

  if (chordMult > 0) {
    chordStep++;
    int step = 1500;
    if (chordMult == 1) {
      if (chordStep < step) {

      } else if (chordStep < step * 2) {
        baseFreqLocal = baseFreq_ch1;
      } else if (chordStep < step * 3) {
        baseFreqLocal = baseFreq_ch2;
      } else {
        chordStep = 0;
      }
    }

    if (chordMult == 2) {
      if (chordStep < step) {

      } else if (chordStep < step * 2) {
        baseFreqLocal = baseFreq_ch3;
      } else if (chordStep < step * 3) {
        baseFreqLocal = baseFreq_ch4;
      } else {
        chordStep = 0;
      }
    }
  }

  sampleIndex += baseFreqLocal;

  if (sampleIndex >= sampleLen * 1000) {

    if (envelopeNum == 2) {
      sampleIndex = (sampleLen - 1) * 1000;
    } else {
      sampleIndex = sampleIndex - sampleLen * 1000;
    }
  }

  envelopeIndex += envelopeLength;
  if (envelopeIndex > 50000) {
    envelopeIndex = 50000;
    if (envelopeNum == 3)
      envelopeIndex = 1;
  }

  sample = (sample * volume * envelopes[envelopeNum][envelopeIndex / 500]) / 300;  //300 because of 3 volume levels

  if (isDelay) {
    sample /= 3;
  }
  return sample;
}

int CrunchVoice::ReadDrumWaveform() {
  (void)voiceNum;
  subSampleIndex = sampleIndex / 1000;
  if (envelopeNum > 1) {
    subSampleIndex = sampleLen - sampleIndex / 1000 - 1;
  }

  switch (note) {
    case 0:
      sampleLen = kick3Length;
      sample = kick3[subSampleIndex];
      break;
    case 1:
      sampleLen = snareB2Length;
      sample = snareB2[subSampleIndex];
      break;
    case 2:
      sampleLen = snareB1Length;
      sample = snareB1[subSampleIndex];
      break;
    case 3:
      sampleLen = hihat2Length;
      sample = hihat2[subSampleIndex];
      break;
    case 4:
      sampleLen = kick3Length;
      sample = kick3[subSampleIndex];
      break;
    case 5:
      sampleLen = snareB2Length;
      sample = snareB2[subSampleIndex];
      break;
    case 6:
      sampleLen = clap1Length;
      sample = clap1[subSampleIndex];
      break;
    case 7:
      sampleLen = hihat2Length;
      sample = hihat2[subSampleIndex];
      break;
    case 8:
      sampleLen = kick3Length;
      sample = kick3[subSampleIndex];
      break;
    case 9:
      sampleLen = snareB1Length;
      sample = snareB1[subSampleIndex];
      break;
    case 10:
      sampleLen = crash1Length;
      sample = crash1[subSampleIndex];
      break;
    case 11:
      sampleLen = ride1Length;
      sample = ride1[subSampleIndex];
      break;
  }
  if (sampleIndex >= sampleLen * 1000) {
    sample = 0;
  } else {
    int oct = octave + 1;
    if (recOctave > -1)
      oct = recOctave + 1;

    sampleIndex += oct * 500;
    if (pitchMult > 0) {
      if (pitchDur > 0) {
        pitchDur -= pitchMult;
        if (pitchMult == 1)
          sampleIndex -= pitchDur / 5;
        if (pitchMult == 2)
          sampleIndex += pitchDur / 5;
      }
    }
  }
  sample = (sample * volume / 3);
  if (isDelay) {
    sample /= 3;
  }
  return sample;
}



int CrunchVoice::ReadSfxWaveform() {

  subSampleIndex = sampleIndex / 1000;
  if (envelopeNum > 1) {
    subSampleIndex = sampleLen - sampleIndex / 1000 - 1;
  }

  switch (note) {
    case 0:
      sampleLen = sfx1Length;
      sample = sfx1[subSampleIndex];
      break;
    case 1:
      sampleLen = sfx2Length;
      sample = sfx2[subSampleIndex];
      break;
    case 2:
      sampleLen = sfx3Length;
      sample = sfx3[subSampleIndex];
      break;
#if USE_FULL_SFX_BANK
    case 3:
      sampleLen = sfx4Length;
      sample = sfx4[subSampleIndex];
      break;
    case 4:
      sampleLen = sfx5Length;
      sample = sfx5[subSampleIndex];
      break;
    case 5:
      sampleLen = sfx6Length;
      sample = sfx6[subSampleIndex];
      break;
    case 6:
      sampleLen = sfx7Length;
      sample = sfx7[subSampleIndex];
      break;
    case 7:
      sampleLen = sfx8Length;
      sample = sfx8[subSampleIndex];
      break;
    case 8:
      sampleLen = sfx9Length;
      sample = sfx9[subSampleIndex];
      break;
    case 9:
      sampleLen = sfx10Length;
      sample = sfx10[subSampleIndex];
      break;
    case 10:
      sampleLen = sfx11Length;
      sample = sfx11[subSampleIndex];
      break;
    case 11:
      sampleLen = sfx12Length;
      sample = sfx12[subSampleIndex];
      break;
#else
    case 3:
      sampleLen = sfx5Length;
      sample = sfx5[subSampleIndex];
      break;
    case 4:
      sampleLen = sfx1Length;
      sample = sfx1[subSampleIndex];
      break;
    case 5:
      sampleLen = sfx2Length;
      sample = sfx2[subSampleIndex];
      break;
    case 6:
      sampleLen = sfx3Length;
      sample = sfx3[subSampleIndex];
      break;
    case 7:
      sampleLen = sfx5Length;
      sample = sfx5[subSampleIndex];
      break;
    case 8:
      sampleLen = sfx1Length;
      sample = sfx1[subSampleIndex];
      break;
    case 9:
      sampleLen = sfx2Length;
      sample = sfx2[subSampleIndex];
      break;
    case 10:
      sampleLen = sfx3Length;
      sample = sfx3[subSampleIndex];
      break;
    case 11:
      sampleLen = sfx5Length;
      sample = sfx5[subSampleIndex];
      break;
#endif
  }
  if (sampleIndex >= sampleLen * 1000) {
    sample = 0;
  } else {
    int oct = octave + 1;
    if (recOctave > -1)
      oct = recOctave + 1;

    sampleIndex += oct * 500;

    if (pitchMult > 0) {
      if (pitchDur > 0) {
        pitchDur -= pitchMult;
        if (pitchMult == 1)
          sampleIndex -= pitchDur / 5;
        if (pitchMult == 2)
          sampleIndex += pitchDur / 5;
      }
    }
  }
  sample = (sample * volume / 3);
  if (isDelay) {
    sample /= 3;
  }
  return sample;
}

int CrunchVoice::GetBaseFreq(int val, int ioctave) {
  while (val > 11) {
    val -= 12;
    ioctave++;
  }
  while (val < 0) {
    val += 12;
    ioctave--;
  }

  int idx = val + (ioctave * 12);
  if (idx < 0) idx = 0;
  if (idx > 47) idx = 47;
  int valOut = noteFreqLookup[idx];
  return valOut;
}

void CrunchVoice::SetNote(int val, bool delay, int optOctave, int optInstrument) {
  int ioct = (optOctave == -1) ? octave : optOctave;
  int newBase = GetBaseFreq(val, ioct);
  int newBaseCh1 = GetBaseFreq(val - 4, ioct);
  int newBaseCh2 = GetBaseFreq(val + 3, ioct);
  int newBaseCh3 = GetBaseFreq(val - 5, ioct);
  int newBaseCh4 = GetBaseFreq(val + 7, ioct);

  bool canSlide = slideMode && optInstrument > 1 && !samplerMode && sampleIndex > 0;
  if (!canSlide) {
    sampleIndex = 0;
    envelopeIndex = 0;
  }
  pitchDur = 7000;
  note = val;

  voiceNum = optInstrument;
  recOctave = optOctave;

  baseFreqTarget = newBase;
  baseFreq_ch1Target = newBaseCh1;
  baseFreq_ch2Target = newBaseCh2;
  baseFreq_ch3Target = newBaseCh3;
  baseFreq_ch4Target = newBaseCh4;

  if (!canSlide) {
    baseFreq = newBase;
    baseFreq_ch1 = newBaseCh1;
    baseFreq_ch2 = newBaseCh2;
    baseFreq_ch3 = newBaseCh3;
    baseFreq_ch4 = newBaseCh4;
  } else {
    // Keep continuity for glide: pitch moves progressively toward target.
  }

  isDelay = delay;
}

void CrunchVoice::AdvanceArpStep() {
  if (arpMode <= 0 || arpMode == 4) {
    arpSemitoneOffset = 0;
    arpOctaveOffset = 0;
    return;
  }

  arpBeatIndex = (arpBeatIndex + 1) & 0x3;  // 4-step patterns synced to beat

  switch (arpMode) {
    case 1: {
      // OCT: 0, +1, 0, +1 octave
      static const int8_t octPat[4] = {0, 1, 0, 1};
      arpSemitoneOffset = 0;
      arpOctaveOffset = octPat[arpBeatIndex];
      break;
    }
    case 2: {
      // MIN: root, m3, 5th, m3
      static const int8_t minPat[4] = {0, 3, 7, 3};
      arpSemitoneOffset = minPat[arpBeatIndex];
      arpOctaveOffset = 0;
      break;
    }
    case 3: {
      // MAJ: root, M3, 5th, M3
      static const int8_t majPat[4] = {0, 4, 7, 4};
      arpSemitoneOffset = majPat[arpBeatIndex];
      arpOctaveOffset = 0;
      break;
    }
    case 5: {
      // OCT2: 0, +1, 0, -1 octave
      static const int8_t oct2Pat[4] = {0, 1, 0, -1};
      arpSemitoneOffset = 0;
      arpOctaveOffset = oct2Pat[arpBeatIndex];
      break;
    }
    default:
      arpSemitoneOffset = 0;
      arpOctaveOffset = 0;
      break;
  }
}

void CrunchVoice::SetDelay(int val) {
  delay = val;
}

void CrunchVoice::SetVolume(int val) {
  volumeNum = val;
  switch (val) {
    case 0:
      mute = !mute;
      break;
    case 1:
      if (volume == 2) {
        volume = 3;
      } else {
        volume = 2;
      }
      break;
    case 2:
      overdrive = !overdrive;
      break;
  }
}

void CrunchVoice::SetOctave(int val) {
  octave = val;
  // Recalculate baseFreq immediately so the running note updates pitch.
  baseFreq = GetBaseFreq(note, octave);
  baseFreq_ch1 = GetBaseFreq(note - 4, octave);
  baseFreq_ch2 = GetBaseFreq(note + 3, octave);
  baseFreq_ch3 = GetBaseFreq(note - 5, octave);
  baseFreq_ch4 = GetBaseFreq(note + 7, octave);
}

void CrunchVoice::SetEnvelopeNum(int val) {
  envelopeNum = val;
}

void CrunchVoice::SetEnvelopeLength(int val) {
  envelopeLength = 4 - val;
}

void CrunchVoice::SetEffectNum(int val) {

  if (val == 0) {
    ResetEffects();
  }

  if (val == 1) {
    lowPassMult = (lowPassMult > 0) ? 0 : 2;
  }

  if (val == 2) {
    reverbMult = (reverbMult > 0) ? 0 : 2;
  }

  if (val == 3) {
    phaserMult = (phaserMult > 0) ? 0 : 2;
  }
  if (val == 4) {
    delayMult = (delayMult > 0) ? 0 : 2;
  }

  if (val == 5) {
    chordMult = (chordMult > 0) ? 0 : 1;
  }

  if (val == 6) {
    whooshMult = (whooshMult > 0) ? 0 : 2;
  }

  if (val == 7) {
    pitchMult = (pitchMult > 0) ? 0 : 1;
  }
}

void CrunchVoice::ResetEffects() {
  samplerMode = false;
  phaserMult = 0;
  delayMult = 0;
  reverbMult = 0;
  lowPassMult = 0;
  chordMult = 0;
  whooshMult = 0;
  pitchMult = 0;
  ladderMult = 0;
  flangerMult = 0;
}

void CrunchVoice::UpdateHistory(int sample) {
  sampleHistory[sampleHistoryIndex / 2] = sample;
  sampleHistoryIndex++;
  if (sampleHistoryIndex > HISTORY_STEPS - 2) {
    sampleHistoryIndex = 0;
  }
}

int CrunchVoice::GetHistorySample(int backoffset) {
  int ind = sampleHistoryIndex - backoffset;

  if (ind < 0) {
    ind = HISTORY_STEPS + ind;
  }
  return sampleHistory[ind / 2];
}


