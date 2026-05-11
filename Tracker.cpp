#include <Arduino.h>
#include "Tracker.h"
#include "Voice.h"
#include "config_module.h"

Tracker::Tracker() {
  patternLength = 32;
  isPlaying = true;
  masterGainQ8 = 192;
  scaleIndex = 0;
  bpms[0] = 120;
  bpms[1] = 140;
  bpms[2] = 95;
  bpms[3] = 180;
  SetBPM(0);

  for (int j = 0; j < 4; j++) {
    trackGainQ8[j] = 255;
    trackMuted[j] = false;
    fxPresetSlot[j] = -1;
    for (int i = 0; i < 512; i++) {
      tracks[j][i] = 0;
      trackFxPreset[j][i] = -1;
    }
  }

  ClearAll(0);
}

int Tracker::UpdateTracker() {

  float curTime = millis();
  float delta = curTime - lastMillis;
  if (delta < 0.0f) delta = 0.0f;
  // Large scheduling hiccups should not cause sequencer catch-up bursts.
  if (delta > 40.0f) delta = 40.0f;
  tempoBlink = 0;
  lastMillis = curTime;

  float dbps = delta * bps;

  noteTime += dbps;

  int stepsToAdvance = 0;
  if (noteTime >= 250.0f) {
    stepsToAdvance = (int)(noteTime / 250.0f);
    // Keep timing stable under load by limiting catch-up work per audio callback.
    if (stepsToAdvance > 2) {
      stepsToAdvance = 2;
      noteTime = 0.0f;
    } else {
      noteTime -= 250.0f * (float)stepsToAdvance;
    }
  }

  for (int step = 0; step < stepsToAdvance; step++) {
    barCount++;
    if (barCount > 3) {
      tempoBlink = 30;
      barCount = 0;
      if (!pressedOnce) {
        SetNote(7, 0);
      }
    }

    for (int i = 0; i < 4; i++) {
      int note = tracks[i][trackIndex];
      int optOctave = trackOctaves[i][trackIndex];
      int optInstrument = trackInstruments[i][trackIndex];
      int optFxPreset = trackFxPreset[i][trackIndex];

      if (fxPresetSlot[i] != optFxPreset) {
        ApplyFxPresetToTrack(i, optFxPreset);
      }

      if (note > 0) {
        heldNotes[i] = note;
        heldInsturments[i] = currentVoice;
        voices[i].SetNote(note - 1, false, optOctave, optInstrument);
      }

      // BPM-synced arpeggiator: one arp step per tracker beat.
      voices[i].AdvanceArpStep();
    }

    trackIndex++;

    if (trackIndex >= patternLength * (currentPattern + 1)) {
      trackIndex = patternLength * (currentPattern + 0);
      if (allPatternPlay) {
        currentPattern++;
        if (currentPattern > 3) {
          currentPattern -= 4;
        }
        trackIndex = patternLength * (currentPattern + 0);
      }
    }
  }

  sample = 0;

  for (int i = 0; i < 4; i++) {
    voices[i].bps = bps;
    int samp = (voices[i].UpdateVoice() * masterGainQ8) / 256;
    samp = (samp * trackGainQ8[i]) / 255;
    if (trackMuted[i]) {
      samp = 0;
    }
    samp = samp / (2 + masterVolume * 5);
    sample += samp;
    lastSamples[i] = samp;
  }

  return 0;
}

// Scale tables: 12 keys → semitone offset.
// Keep all 12 entries distinct and ascending even if they cross octaves.
static const int8_t scaleTables[3][12] = {
  { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11},  // chromatic
  { 0, 2, 4, 7, 9,12,14,16,19,21,24,26},  // pentatonic (stacked across octaves)
  { 0, 2, 4, 5, 7, 9,11,12,14,16,17,19},  // major (stacked across octaves)
};

void Tracker::ApplyPotControls(int masterRaw, int octaveRaw, int reverbRaw, int delayRaw, int phaserRaw) {
  if (masterRaw >= 0) {
    int v = map(masterRaw, 0, 4095, 0, 255);
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    masterGainQ8 = v;
  }

  if (selectedTrack >= 0 && selectedTrack < 4) {
    if (octaveRaw >= 0) {
      int oct = map(octaveRaw, 0, 3800, 0, 3);
      oct = constrain(oct, 0, 3);
      // Only apply if the discrete value actually changed.
      if (oct != voices[selectedTrack].octave) {
        SetOctave(oct);
        BuildOLEDHintString(String("Octave: ") + String(oct));
      }
    }

    if (reverbRaw >= 0) {
      int amt = map(reverbRaw, 0, 4095, 0, 4);
      amt = constrain(amt, 0, POT_EFFECT_MAX);
      voices[selectedTrack].reverbMult = amt;
    }

    if (delayRaw >= 0) {
      int amt = map(delayRaw, 0, 4095, 0, 4);
      amt = constrain(amt, 0, POT_EFFECT_MAX);
      voices[selectedTrack].delayMult = amt;
    }

    if (phaserRaw >= 0) {
      int amt = map(phaserRaw, 0, 4095, 0, 4);
      amt = constrain(amt, 0, POT_EFFECT_MAX);
      voices[selectedTrack].phaserMult = amt;
    }
  }
}

void Tracker::BuildOLEDHintString(String string) {
  // Keep hints brief so UI stays responsive.
  hintTime = 10;
  string.toCharArray(hint, 15);
}

void Tracker::SetCommand(char command, int val) {
  switch (command) {
    case 'T':
      SetTrackNum(val);
      BuildOLEDHintString(String("Track: " + String(val + 1)));
      break;
    case 'B':
      {
        int deltaBpm = 0;
        switch (val) {
          case 0: deltaBpm = -5; break;
          case 1: deltaBpm = +5; break;
          case 2: deltaBpm = -10; break;
          case 3: deltaBpm = +10; break;
        }
        int nextBpm = constrain((int)bpm + deltaBpm, 40, 280);
        bpm = nextBpm;
        bps = bpm / 60;
        BuildOLEDHintString(String("BPM: ") + String((int)bpm));
      }
      break;
    case 'N':
      if (!pressedOnce) {
        noteTime = 0;
        trackIndex = 0;
        lastMillis = millis();
        barCount = 0;
        tempoBlink = 30;
      }
      pressedOnce = true;
      SetNote(val, selectedTrack);
      break;
    case 'Z':
      tracks[selectedTrack][trackIndex] = 0;
      trackOctaves[selectedTrack][trackIndex] = voices[selectedTrack].octave;
      trackInstruments[selectedTrack][trackIndex] = currentVoice;
      trackFxPreset[selectedTrack][trackIndex] = fxPresetSlot[selectedTrack];
      lastNoteTrackIndex = trackIndex % patternLength;
      BuildOLEDHintString(String("Silence"));
      break;
    case 'O':
      SetOctave(val);
      BuildOLEDHintString(String("Octave: " + String(val)));
      break;
    case 'L':
      SetEnvelopeLength(val);
      BuildOLEDHintString(String("Note Len: " + String(val + 1)));
      break;
    case 'E':
      SetEnvelopeNum(val);
      switch (val) {
        case 0:
          BuildOLEDHintString(String("Fade Out"));
          break;
        case 1:
          BuildOLEDHintString(String("Fade In"));
          break;
        case 2:
          BuildOLEDHintString(String("No Fade"));
          break;
        case 3:
          BuildOLEDHintString(String("Loop"));
          break;
      }
      break;
    case 'V':
      if (val == 3) {
        SoloTrack(false);

      } else {
        SetVolume(val);
        if (val == 2) {
          if (voices[selectedTrack].overdrive) {
            BuildOLEDHintString(String("ODrv On"));
          }else{
            BuildOLEDHintString(String("ODrv Off"));
          }
        } else if (val == 0) {
          if (voices[selectedTrack].mute) {
            BuildOLEDHintString(String("Mute"));
          }else{
            BuildOLEDHintString(String("Unmute"));
          }
        } else {
          BuildOLEDHintString(String("Volume: " + String(voices[selectedTrack].volume)));
        }
      }
      break;
    case 'D':
      SetEffect(val + 4);
      switch (val) {
        case 0:
          BuildOLEDHintString(String("Echo: " + String(voices[selectedTrack].delayMult)));
          break;
        case 1:
          BuildOLEDHintString(String("ArpChord: " + String(voices[selectedTrack].chordMult)));
          break;
        case 2:
          BuildOLEDHintString(String("Whoosh: " + String(voices[selectedTrack].whooshMult)));
          break;
        case 3:
          BuildOLEDHintString(String("Pitchbend: " + String(voices[selectedTrack].pitchMult)));
          break;
      }
      break;
    case 'A':
      SetEffect(val);
      switch (val) {
        case 0:
          BuildOLEDHintString(String("Effects Off"));
          break;
        case 1:
          BuildOLEDHintString(String("Low Pass: " + String(voices[selectedTrack].lowPassMult)));
          break;
        case 2:
          BuildOLEDHintString(String("Retrig: " + String(voices[selectedTrack].reverbMult)));
          break;
        case 3:
          BuildOLEDHintString(String("Wobble: " + String(voices[selectedTrack].phaserMult)));
          break;
      }
      break;
    case '^':
      ClearTrackNum(val);
      BuildOLEDHintString(String("Clr Track: " + String(val + 1)));
      break;
    case '$':
      SetPatternNum(val);
      BuildOLEDHintString(String("Pattern: " + String(val + 1)));
      break;
    case '#':
      ClearPatternNum(val);
      BuildOLEDHintString(String("Clr Pattern: " + String(val + 1)));
      break;
    case 'X':
      ClearAll(val);
      BuildOLEDHintString(String("New Song: " + String((32 * (val + 1)))));
      break;
    case 'P':
      TogglePlayStop();
      if (isPlaying) {
        BuildOLEDHintString(String("Rec On"));
      } else {
        BuildOLEDHintString(String("Rec Off"));
      }
      break;
    case 'I':
      currentVoice = val;
      if (val > 1) {
        BuildOLEDHintString(String("Instrument: " + String(val)));
        if (val == 12) {
          String("SFX6").toCharArray(oledInstString, 8);
        } else {
          String("INS" + String(val)).toCharArray(oledInstString, 8);
        }
      } else if (val == 1) {
        BuildOLEDHintString(String("Drum Bank 1"));
        String("DRM1").toCharArray(oledInstString, 6);
      } else {
        BuildOLEDHintString(String("Drum Bank 0"));
        String("DRM0").toCharArray(oledInstString, 6);
      }
      break;
    case 'H':
      masterVolume++;
      if (masterVolume > 1)
        masterVolume = 0;

      BuildOLEDHintString(String("Mstr Volume: " + String(2 - masterVolume)));
      break;
    case 'K':

      break;
    case 'S':
      SetScale(constrain(val, 0, 2));
      {
        const char* sn[] = {"CHR", "PNT", "MAJ"};
        BuildOLEDHintString(String("Scale: ") + String(sn[scaleIndex]));
      }
      break;
    case 'C':
      allPatternPlay = !allPatternPlay;
      if (allPatternPlay) {
        BuildOLEDHintString(String("Song Mode"));
      } else {
        BuildOLEDHintString(String("Pattern Mode"));
      }
      break;
    case 'q':
      SetTrackMute(constrain(val, 0, 3), true);
      BuildOLEDHintString(String("Mute T") + String(val + 1));
      break;
    case 'u':
      SetTrackMute(constrain(val, 0, 3), false);
      BuildOLEDHintString(String("Unmute T") + String(val + 1));
      break;
    case '+':
      ChangeTrackGain(constrain(val, 0, 3), +16);
      BuildOLEDHintString(String("T") + String(val + 1) + String(" Vol:") + String((trackGainQ8[constrain(val, 0, 3)] * 100) / 255));
      break;
    case '-':
      ChangeTrackGain(constrain(val, 0, 3), -16);
      BuildOLEDHintString(String("T") + String(val + 1) + String(" Vol:") + String((trackGainQ8[constrain(val, 0, 3)] * 100) / 255));
      break;
    case 'Y':
      {
        int mode = constrain(val, 0, 5);
        // NONE / OCT / MIN / MAJ / SLIDE / OCT2 presets.
        if (mode == 0) {
          voices[selectedTrack].arpMode = 0;
          voices[selectedTrack].slideMode = false;
          BuildOLEDHintString(String("Arp None"));
        } else if (mode == 1) {
          voices[selectedTrack].arpMode = 1;
          voices[selectedTrack].slideMode = false;
          BuildOLEDHintString(String("Arp Oct"));
        } else if (mode == 2) {
          voices[selectedTrack].arpMode = 2;
          voices[selectedTrack].slideMode = false;
          BuildOLEDHintString(String("Arp Minor"));
        } else if (mode == 3) {
          voices[selectedTrack].arpMode = 3;
          voices[selectedTrack].slideMode = false;
          BuildOLEDHintString(String("Arp Major"));
        } else if (mode == 4) {
          voices[selectedTrack].arpMode = 4;
          voices[selectedTrack].slideMode = true;
          BuildOLEDHintString(String("Arp Slide"));
        } else if (mode == 5) {
          voices[selectedTrack].arpMode = 5;
          voices[selectedTrack].slideMode = false;
          BuildOLEDHintString(String("Arp Oct2"));
        }
      }
      break;
    case 'F':
      {
        int mode = constrain(val, 0, 15);
        int tr = selectedTrack;
        bool forceDry = (mode == 11 || mode == 14 || mode == 15);
        if (forceDry || fxPresetSlot[tr] == mode) {
          voices[selectedTrack].ResetEffects();
          fxPresetSlot[tr] = -1;
          if (isPlaying && pressedOnce) {
            trackFxPreset[tr][trackIndex] = -1;
          }
          BuildOLEDHintString(String("FX Dry"));
          break;
        }

        voices[selectedTrack].ResetEffects();
        switch (mode) {
          case 0: voices[selectedTrack].overdrive = true; fxPresetSlot[tr] = mode; BuildOLEDHintString(String("FX Fuzz")); break;
          case 1: voices[selectedTrack].delayMult = 2; fxPresetSlot[tr] = mode; BuildOLEDHintString(String("FX Delay")); break;
          case 2: voices[selectedTrack].lowPassMult = 2; fxPresetSlot[tr] = mode; BuildOLEDHintString(String("FX LPFUp")); break;
          case 3: voices[selectedTrack].lowPassMult = 1; fxPresetSlot[tr] = mode; BuildOLEDHintString(String("FX LPFDwn")); break;
          case 4: voices[selectedTrack].reverbMult = 2; fxPresetSlot[tr] = mode; BuildOLEDHintString(String("FX Retrig")); break;
          case 5: voices[selectedTrack].phaserMult = 2; fxPresetSlot[tr] = mode; BuildOLEDHintString(String("FX Wobble")); break;
          case 6: voices[selectedTrack].whooshMult = 2; fxPresetSlot[tr] = mode; BuildOLEDHintString(String("FX Whoosh")); break;
          case 7: voices[selectedTrack].pitchMult = 2; fxPresetSlot[tr] = mode; BuildOLEDHintString(String("FX Pitch+")); break;
          case 8: voices[selectedTrack].pitchMult = 1; fxPresetSlot[tr] = mode; BuildOLEDHintString(String("FX Pitch-")); break;
          case 9: voices[selectedTrack].chordMult = 1; fxPresetSlot[tr] = mode; BuildOLEDHintString(String("FX ChordM")); break;
          case 10: voices[selectedTrack].chordMult = 2; fxPresetSlot[tr] = mode; BuildOLEDHintString(String("FX Chordm")); break;
          case 12: voices[selectedTrack].ladderMult = 3; fxPresetSlot[tr] = mode; BuildOLEDHintString(String("FX Ladder")); break;
          case 13: voices[selectedTrack].flangerMult = 3; fxPresetSlot[tr] = mode; BuildOLEDHintString(String("FX Flanger")); break;
          default: fxPresetSlot[tr] = -1; BuildOLEDHintString(String("FX Dry")); break;
        }

        if (isPlaying && pressedOnce) {
          trackFxPreset[tr][trackIndex] = fxPresetSlot[tr];
        }
      }
      break;
    case 'U':
      {
        static const int expInstMap[12] = {10, 8, 11, 6, 9, 5, 10, 12, 4, 2, 7, 3};
        static const char* expInstName[12] = {
          "NeoVox", "SuperSaw", "TalkBox", "MetalPad",
          "NeuroBass", "Formant", "GlideLd", "VoxChip",
          "AirPad", "PunchBs", "Harmonic", "Grain"
        };
        int mode = constrain(val, 0, 11);
        currentVoice = expInstMap[mode];
        BuildOLEDHintString(String(expInstName[mode]));
        String(expInstName[mode]).toCharArray(oledInstString, sizeof(oledInstString));
      }
      break;
    case '*':
      if (val == 0) {
        BuildOLEDHintString(String("Copy Pattern"));
        CopyPattern();
      }
      if (val == 1) {
        BuildOLEDHintString(String("Paste Pattern"));
        PastePattern();
      }
      if (val == 2) {
        BuildOLEDHintString(String("Paste All Patt"));
        PastePatternAll();
      }
      if (val == 3) {
        voices[selectedTrack].samplerMode = !voices[selectedTrack].samplerMode;
        voices[selectedTrack].SetEnvelopeNum(2);
        BuildOLEDHintString(String("Samp Mode: " + String(voices[selectedTrack].samplerMode)));
      }
      break;
  }
}

void Tracker::SoloTrack(bool repeat) {
  if (!repeat) {
    solo = !solo;
  }

  if (solo) {
    for (int i = 0; i < 4; i++) {
      if (i == selectedTrack) {
        voices[i].soloMute = false;
      } else {
        voices[i].soloMute = true;
      }
    }
    BuildOLEDHintString(String("Solo On"));
  } else {
    for (int i = 0; i < 4; i++) {
      voices[i].soloMute = false;
    }
    BuildOLEDHintString(String("Solo Off"));
  }
}

void Tracker::SetEffect(int val) {
  voices[selectedTrack].SetEffectNum(val);
};

void Tracker::ApplyFxPresetToTrack(int track, int preset) {
  if (track < 0 || track > 3) return;

  voices[track].ResetEffects();
  fxPresetSlot[track] = -1;

  switch (preset) {
    case 0: voices[track].overdrive = true; fxPresetSlot[track] = 0; break;
    case 1: voices[track].delayMult = 2; fxPresetSlot[track] = 1; break;
    case 2: voices[track].lowPassMult = 2; fxPresetSlot[track] = 2; break;
    case 3: voices[track].lowPassMult = 1; fxPresetSlot[track] = 3; break;
    case 4: voices[track].reverbMult = 2; fxPresetSlot[track] = 4; break;
    case 5: voices[track].phaserMult = 2; fxPresetSlot[track] = 5; break;
    case 6: voices[track].whooshMult = 2; fxPresetSlot[track] = 6; break;
    case 7: voices[track].pitchMult = 2; fxPresetSlot[track] = 7; break;
    case 8: voices[track].pitchMult = 1; fxPresetSlot[track] = 8; break;
    case 9: voices[track].chordMult = 1; fxPresetSlot[track] = 9; break;
    case 10: voices[track].chordMult = 2; fxPresetSlot[track] = 10; break;
    case 12: voices[track].ladderMult = 3; fxPresetSlot[track] = 12; break;
    case 13: voices[track].flangerMult = 3; fxPresetSlot[track] = 13; break;
    default: break;
  }
}

void Tracker::SetBPM(int val) {
  bpm = bpms[val];
  bps = bpm / 60;
};

void Tracker::SetDelay(int val) {
  if (val > 0)
    val += 1;
  voices[selectedTrack].delay = val;
};
void Tracker::SetEnvelopeNum(int val) {
  voices[selectedTrack].SetEnvelopeNum(val);
};

void Tracker::SetEnvelopeLength(int val) {
  voices[selectedTrack].SetEnvelopeLength((val));
};

void Tracker::SetOctave(int val) {
  voices[selectedTrack].SetOctave(val);
};

void Tracker::SetScale(int val) {
  scaleIndex = constrain(val, 0, 2);
};

void Tracker::SetVolume(int val) {
  voices[selectedTrack].SetVolume(val);
};

void Tracker::SetTrackMute(int track, bool muteOn) {
  if (track < 0 || track > 3) return;
  trackMuted[track] = muteOn;
}

void Tracker::ChangeTrackGain(int track, int deltaQ8) {
  if (track < 0 || track > 3) return;
  trackGainQ8[track] = constrain(trackGainQ8[track] + deltaQ8, 0, 255);
}

void Tracker::SetNote(int val, int track) {
  val = constrain(val, 0, 23);
  int octaveShift = val / 12;
  int noteInOctave = val % 12;
  int finalVal = noteInOctave;
  int noteOctaveOverride = -1;

  // Keep 24-pad behavior for drum kits by shifting drum playback octave
  // instead of folding pads 13..24 onto the same 12 notes.
  if (currentVoice <= 1) {
    noteOctaveOverride = constrain(voices[track].octave + octaveShift, 0, 3);
  }

  if (currentVoice > 1) {
    finalVal = (int)scaleTables[scaleIndex][noteInOctave] + (octaveShift * 12);
  }

  if (isPlaying && pressedOnce) {
    //one behind trick
    tracks[track][trackIndex] = finalVal + 1;
    trackOctaves[track][trackIndex] = (noteOctaveOverride == -1) ? voices[track].octave : noteOctaveOverride;
    trackInstruments[track][trackIndex] = currentVoice;
    trackFxPreset[track][trackIndex] = fxPresetSlot[track];
    lastNoteTrackIndex = trackIndex % patternLength;
  } else {
    voices[track].SetNote(finalVal, false, noteOctaveOverride, currentVoice);
  }
};

void Tracker::SetTrackNum(int val) {
  selectedTrack = val;
  SoloTrack(true);
};

void Tracker::ClearTrackNum(int val) {
  for (int i = patternLength * (currentPattern); i < patternLength * (currentPattern + 1); i++) {
    tracks[val][i] = 0;
    trackFxPreset[val][i] = -1;
  }
  voices[val].arpNum = 0;
};

void Tracker::SetPatternNum(int val) {

  trackIndex = trackIndex - (patternLength * currentPattern);
  currentPattern = val;
  trackIndex += (patternLength * currentPattern);
};

void Tracker::ClearPatternNum(int val) {
  for (int j = 0; j < 4; j++) {
    for (int i = patternLength * (currentPattern); i < patternLength * (currentPattern + 1); i++) {
      tracks[j][i] = 0;
      trackFxPreset[j][i] = -1;
    }
    voices[j].arpNum = 0;
  }
};

void Tracker::TogglePlayStop() {
  isPlaying = !isPlaying;
};

//TBD
void Tracker::CopyTrack(){};
void Tracker::PasteTrack(){};

void Tracker::CopyPattern() {
  for (int j = 0; j < 4; j++) {
    int c = 0;
    for (int i = patternLength * (currentPattern); i < patternLength * (currentPattern + 1); i++) {
      patternCopy[j][c] = tracks[j][i];
      patternCopyInstruments[j][c] = trackInstruments[j][i];
      patternCopyOctaves[j][c] = trackOctaves[j][i];
      patternCopyFxPreset[j][c] = trackFxPreset[j][i];
      c++;
    }
  }
};

void Tracker::PastePattern() {
  for (int j = 0; j < 4; j++) {
    int c = 0;
    for (int i = patternLength * (currentPattern); i < patternLength * (currentPattern + 1); i++) {
      tracks[j][i] = patternCopy[j][c];
      trackInstruments[j][i] = patternCopyInstruments[j][c];
      trackOctaves[j][i] = patternCopyOctaves[j][c];
      trackFxPreset[j][i] = patternCopyFxPreset[j][c];
      c++;
    }
  }
};

void Tracker::PastePatternAll() {
  for (int r = 0; r < 4; r++) {
    for (int j = 0; j < 4; j++) {
      int c = 0;
      for (int i = patternLength * (r); i < patternLength * (r + 1); i++) {
        tracks[j][i] = patternCopy[j][c];
        trackInstruments[j][i] = patternCopyInstruments[j][c];
        trackOctaves[j][i] = patternCopyOctaves[j][c];
        trackFxPreset[j][i] = patternCopyFxPreset[j][c];
        c++;
      }
    }
  }
};

void Tracker::ClearAll(int val) {
  selectedTrack = 0;
  currentPattern = 0;
  isPlaying = true;
  pressedOnce = false;
  allPatternPlay = false;
  currentVoice = 0;
  String("DRUMS").toCharArray(oledInstString, 6);
  for (int j = 0; j < 4; j++) {
    for (int i = 0; i < 512; i++) {
      tracks[j][i] = 0;
      trackInstruments[j][i] = 0;
      trackFxPreset[j][i] = -1;
    }
    voices[j].SetDelay(0);
    voices[j].ResetEffects();
    voices[j].SetEnvelopeNum(0);
    voices[j].volume = 2;
    voices[j].SetOctave(1);
    voices[j].arpMode = 0;
    voices[j].slideMode = false;
    trackGainQ8[j] = 255;
    trackMuted[j] = false;
    fxPresetSlot[j] = -1;
  }
  patternLength = 32 + (32 * val);
};
