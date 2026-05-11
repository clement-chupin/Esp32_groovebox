/*Universal 8bit Graphics Library (https://github.com/olikraus/u8g2/)

  Copyright (c) 2016, olikraus@gmail.com
  All rights reserved.

  Redistribution and use in source and binary forms, with or without modification, 
  are permitted provided that the following conditions are met:

  * Redistributions of source code must retain the above copyright notice, this list 
    of conditions and the following disclaimer.
    
  * Redistributions in binary form must reproduce the above copyright notice, this 
    list of conditions and the following disclaimer in the documentation and/or other 
    materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND 
  CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, 
  INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF 
  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR 
  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT 
  NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; 
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER 
  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, 
  STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF 
  ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.  

*/

#include <Arduino.h>
#include "ScreenManager.h"
#include "Tracker.h"

ScreenManager::ScreenManager() {
  lastNoteBeat = -1;
  noteBeatTime = 0;
  potFeedbackUntilMs = 0;
  potFeedbackValue = 0;
  potFeedbackMax = 0;
  memset(potFeedbackEffect, 0, sizeof(potFeedbackEffect));
}

void ScreenManager::Update(Tracker &tracker, U8G2 &screen, char ledCommandOLED, int volumeBars[4], String noteChars[12]) {
  bool isShowingInstructions = UpdateInstructionsScreen(tracker, screen, ledCommandOLED, volumeBars, noteChars);
  if (!isShowingInstructions) {
    UpdateMainScreen(tracker, screen, ledCommandOLED, volumeBars, noteChars);
    if (tracker.hintTime > 0) {
      screen.setColorIndex(0);
      screen.drawBox(0, 0, 126, 22);
      screen.setColorIndex(1);
      screen.drawRFrame(0, 0, 126, 22, 2);
      tracker.hintTime--;
      screen.drawStr(4, 16, tracker.hint);
    }
    DrawPotFeedback(screen);
  }
}

bool ScreenManager::UpdateInstructionsScreen(Tracker &tracker, U8G2 &screen, char ledCommandOLED, int volumeBars[4], String noteChars[12]) {
  (void)tracker;
  (void)volumeBars;
  (void)noteChars;

  screen.setFont(u8g2_font_6x13_tf);
  const int cellW = 32;
  const int cellH = 32;
  const int textX = 5;
  const int textY = 19;

  if (ledCommandOLED == 'D') {
    for (int x = 0; x < 4; x++) {
      for (int y = 0; y < 4; y++) {
        screen.drawRFrame(x * cellW, y * cellH, cellW - 1, cellH - 1, 3);
      }
    }
    screen.drawStr(cellW * 0 + textX, cellH * 0 + textY, "NLNG");
    screen.drawStr(cellW * 1 + textX, cellH * 0 + textY, "NMED");
    screen.drawStr(cellW * 2 + textX, cellH * 0 + textY, "NSRT");
    screen.drawStr(cellW * 3 + textX, cellH * 0 + textY, "REC");
    screen.drawStr(cellW * 0 + textX, cellH * 1 + textY, "B-5");
    screen.drawStr(cellW * 1 + textX, cellH * 1 + textY, "B+5");
    screen.drawStr(cellW * 2 + textX, cellH * 1 + textY, "B-10");
    screen.drawStr(cellW * 3 + textX, cellH * 1 + textY, "B+10");
    screen.drawStr(cellW * 0 + textX, cellH * 2 + textY, "CPAT");
    screen.drawStr(cellW * 1 + textX, cellH * 2 + textY, "PPAT");
    screen.drawStr(cellW * 2 + textX, cellH * 2 + textY, "PALL");
    screen.drawStr(cellW * 3 + textX, cellH * 2 + textY, "N/SP");
    screen.drawStr(cellW * 0 + textX, cellH * 3 + textY, "RSTS");
    screen.drawStr(cellW * 1 + textX, cellH * 3 + textY, "RSTL");
    screen.drawStr(cellW * 2 + textX, cellH * 3 + textY, "MVOL");
    screen.drawStr(cellW * 3 + textX, cellH * 3 + textY, "PTSN");
  } else if (ledCommandOLED == 'C') {
    for (int x = 0; x < 4; x++) {
      for (int y = 0; y < 4; y++) {
        screen.drawRFrame(x * cellW, y * cellH, cellW - 1, cellH - 1, 3);
      }
    }
    screen.drawStr(cellW * 0 + textX, cellH * 0 + textY, "TRK1");
    screen.drawStr(cellW * 1 + textX, cellH * 0 + textY, "TRK2");
    screen.drawStr(cellW * 2 + textX, cellH * 0 + textY, "TRK3");
    screen.drawStr(cellW * 3 + textX, cellH * 0 + textY, "TRK4");
    screen.drawStr(cellW * 0 + textX, cellH * 1 + textY, "CLT1");
    screen.drawStr(cellW * 1 + textX, cellH * 1 + textY, "CLT2");
    screen.drawStr(cellW * 2 + textX, cellH * 1 + textY, "CLT3");
    screen.drawStr(cellW * 3 + textX, cellH * 1 + textY, "CLT4");
    screen.drawStr(cellW * 0 + textX, cellH * 2 + textY, "PAT1");
    screen.drawStr(cellW * 1 + textX, cellH * 2 + textY, "PAT2");
    screen.drawStr(cellW * 2 + textX, cellH * 2 + textY, "PAT3");
    screen.drawStr(cellW * 3 + textX, cellH * 2 + textY, "PAT4");
    screen.drawStr(cellW * 0 + textX, cellH * 3 + textY, "CLP1");
    screen.drawStr(cellW * 1 + textX, cellH * 3 + textY, "CLP2");
    screen.drawStr(cellW * 2 + textX, cellH * 3 + textY, "CLP3");
    screen.drawStr(cellW * 3 + textX, cellH * 3 + textY, "CLP4");
  } else if (ledCommandOLED == 'B') {
    for (int x = 0; x < 4; x++) {
      for (int y = 0; y < 4; y++) {
        screen.drawRFrame(x * cellW, y * cellH, cellW - 1, cellH - 1, 3);
      }
    }
    screen.drawStr(cellW * 0 + textX, cellH * 0 + textY, "MUTE");
    screen.drawStr(cellW * 1 + textX, cellH * 0 + textY, "VOL");
    screen.drawStr(cellW * 2 + textX, cellH * 0 + textY, "OVDR");
    screen.drawStr(cellW * 3 + textX, cellH * 0 + textY, "SOLO");
    screen.drawStr(cellW * 0 + textX, cellH * 1 + textY, "ENV1");
    screen.drawStr(cellW * 1 + textX, cellH * 1 + textY, "ENV2");
    screen.drawStr(cellW * 2 + textX, cellH * 1 + textY, "ENV3");
    screen.drawStr(cellW * 3 + textX, cellH * 1 + textY, "LOOP");
    screen.drawStr(cellW * 0 + textX, cellH * 2 + textY, "ECHO");
    screen.drawStr(cellW * 1 + textX, cellH * 2 + textY, "CHRD");
    screen.drawStr(cellW * 2 + textX, cellH * 2 + textY, "WOOS");
    screen.drawStr(cellW * 3 + textX, cellH * 2 + textY, "PTCH");
    screen.drawStr(cellW * 0 + textX, cellH * 3 + textY, "NOFX");
    screen.drawStr(cellW * 1 + textX, cellH * 3 + textY, "LOWP");
    screen.drawStr(cellW * 2 + textX, cellH * 3 + textY, "RTRG");
    screen.drawStr(cellW * 3 + textX, cellH * 3 + textY, "WOBB");
  } else if (ledCommandOLED == 'A') {
    for (int x = 0; x < 4; x++) {
      for (int y = 0; y < 4; y++) {
        screen.drawRFrame(x * cellW, y * cellH, cellW - 1, cellH - 1, 3);
      }
    }
    screen.drawStr(cellW * 0 + textX, cellH * 0 + textY, "SFX6");
    screen.drawStr(cellW * 1 + textX, cellH * 0 + textY, "SCA1");
    screen.drawStr(cellW * 2 + textX, cellH * 0 + textY, "SCA2");
    screen.drawStr(cellW * 3 + textX, cellH * 0 + textY, "SCA3");
    screen.drawStr(cellW * 0 + textX, cellH * 1 + textY, "INS8");
    screen.drawStr(cellW * 1 + textX, cellH * 1 + textY, "INS9");
    screen.drawStr(cellW * 2 + textX, cellH * 1 + textY, "INS10");
    screen.drawStr(cellW * 3 + textX, cellH * 1 + textY, "INS11");
    screen.drawStr(cellW * 0 + textX, cellH * 2 + textY, "INS4");
    screen.drawStr(cellW * 1 + textX, cellH * 2 + textY, "INS5");
    screen.drawStr(cellW * 2 + textX, cellH * 2 + textY, "INS6");
    screen.drawStr(cellW * 3 + textX, cellH * 2 + textY, "INS7");
    screen.drawStr(cellW * 0 + textX, cellH * 3 + textY, "DRUM0");
    screen.drawStr(cellW * 1 + textX, cellH * 3 + textY, "DRUM1");
    screen.drawStr(cellW * 2 + textX, cellH * 3 + textY, "INS2");
    screen.drawStr(cellW * 3 + textX, cellH * 3 + textY, "INS3");
  } else if (ledCommandOLED == 'E') {
    for (int x = 0; x < 4; x++) {
      for (int y = 0; y < 4; y++) {
        screen.drawRFrame(x * cellW, y * cellH, cellW - 1, cellH - 1, 3);
      }
    }
    screen.drawStr(cellW * 0 + textX, cellH * 0 + textY, "MUT1");
    screen.drawStr(cellW * 1 + textX, cellH * 0 + textY, "MUT2");
    screen.drawStr(cellW * 2 + textX, cellH * 0 + textY, "MUT3");
    screen.drawStr(cellW * 3 + textX, cellH * 0 + textY, "MUT4");
    screen.drawStr(cellW * 0 + textX, cellH * 1 + textY, "UNM1");
    screen.drawStr(cellW * 1 + textX, cellH * 1 + textY, "UNM2");
    screen.drawStr(cellW * 2 + textX, cellH * 1 + textY, "UNM3");
    screen.drawStr(cellW * 3 + textX, cellH * 1 + textY, "UNM4");
    screen.drawStr(cellW * 0 + textX, cellH * 2 + textY, "V+T1");
    screen.drawStr(cellW * 1 + textX, cellH * 2 + textY, "V+T2");
    screen.drawStr(cellW * 2 + textX, cellH * 2 + textY, "V+T3");
    screen.drawStr(cellW * 3 + textX, cellH * 2 + textY, "V+T4");
    screen.drawStr(cellW * 0 + textX, cellH * 3 + textY, "V-T1");
    screen.drawStr(cellW * 1 + textX, cellH * 3 + textY, "V-T2");
    screen.drawStr(cellW * 2 + textX, cellH * 3 + textY, "V-T3");
    screen.drawStr(cellW * 3 + textX, cellH * 3 + textY, "V-T4");
  } else if (ledCommandOLED == 'F') {
    for (int x = 0; x < 4; x++) {
      for (int y = 0; y < 4; y++) {
        screen.drawRFrame(x * cellW, y * cellH, cellW - 1, cellH - 1, 3);
      }
    }
    screen.drawStr(cellW * 0 + textX, cellH * 0 + textY, "NONE");
    screen.drawStr(cellW * 1 + textX, cellH * 0 + textY, "OCTV");
    screen.drawStr(cellW * 2 + textX, cellH * 0 + textY, "MINR");
    screen.drawStr(cellW * 3 + textX, cellH * 0 + textY, "MAJR");
    screen.drawStr(cellW * 0 + textX, cellH * 1 + textY, "SLID");
    screen.drawStr(cellW * 1 + textX, cellH * 1 + textY, "OCT2");
    screen.drawStr(cellW * 2 + textX, cellH * 1 + textY, "----");
    screen.drawStr(cellW * 3 + textX, cellH * 1 + textY, "----");
    screen.drawStr(cellW * 0 + textX, cellH * 2 + textY, "----");
    screen.drawStr(cellW * 1 + textX, cellH * 2 + textY, "----");
    screen.drawStr(cellW * 2 + textX, cellH * 2 + textY, "----");
    screen.drawStr(cellW * 3 + textX, cellH * 2 + textY, "----");
    screen.drawStr(cellW * 0 + textX, cellH * 3 + textY, "----");
    screen.drawStr(cellW * 1 + textX, cellH * 3 + textY, "----");
    screen.drawStr(cellW * 2 + textX, cellH * 3 + textY, "----");
    screen.drawStr(cellW * 3 + textX, cellH * 3 + textY, "----");
  } else if (ledCommandOLED == 'G') {
    for (int x = 0; x < 4; x++) {
      for (int y = 0; y < 4; y++) {
        screen.drawRFrame(x * cellW, y * cellH, cellW - 1, cellH - 1, 3);
      }
    }
    screen.drawStr(cellW * 0 + textX, cellH * 0 + textY, "FUZZ");
    screen.drawStr(cellW * 1 + textX, cellH * 0 + textY, "DLY+");
    screen.drawStr(cellW * 2 + textX, cellH * 0 + textY, "LPUP");
    screen.drawStr(cellW * 3 + textX, cellH * 0 + textY, "LPDN");
    screen.drawStr(cellW * 0 + textX, cellH * 1 + textY, "RTRG");
    screen.drawStr(cellW * 1 + textX, cellH * 1 + textY, "WOBB");
    screen.drawStr(cellW * 2 + textX, cellH * 1 + textY, "WOSH");
    screen.drawStr(cellW * 3 + textX, cellH * 1 + textY, "PIT+");
    screen.drawStr(cellW * 0 + textX, cellH * 2 + textY, "PIT-");
    screen.drawStr(cellW * 1 + textX, cellH * 2 + textY, "CHDM");
    screen.drawStr(cellW * 2 + textX, cellH * 2 + textY, "CHDm");
    screen.drawStr(cellW * 3 + textX, cellH * 2 + textY, "DRY");
    screen.drawStr(cellW * 0 + textX, cellH * 3 + textY, "LADR");
    screen.drawStr(cellW * 1 + textX, cellH * 3 + textY, "FLNG");
    screen.drawStr(cellW * 2 + textX, cellH * 3 + textY, "DRY");
    screen.drawStr(cellW * 3 + textX, cellH * 3 + textY, "----");
  } else if (ledCommandOLED == 'H') {
    for (int x = 0; x < 4; x++) {
      for (int y = 0; y < 4; y++) {
        screen.drawRFrame(x * cellW, y * cellH, cellW - 1, cellH - 1, 3);
      }
    }
    screen.drawStr(cellW * 0 + textX, cellH * 0 + textY, "NVOX");
    screen.drawStr(cellW * 1 + textX, cellH * 0 + textY, "SSAW");
    screen.drawStr(cellW * 2 + textX, cellH * 0 + textY, "TBOX");
    screen.drawStr(cellW * 3 + textX, cellH * 0 + textY, "MTLP");
    screen.drawStr(cellW * 0 + textX, cellH * 1 + textY, "NBAS");
    screen.drawStr(cellW * 1 + textX, cellH * 1 + textY, "FRMT");
    screen.drawStr(cellW * 2 + textX, cellH * 1 + textY, "GLID");
    screen.drawStr(cellW * 3 + textX, cellH * 1 + textY, "VCHP");
    screen.drawStr(cellW * 0 + textX, cellH * 2 + textY, "AIRP");
    screen.drawStr(cellW * 1 + textX, cellH * 2 + textY, "PNBS");
    screen.drawStr(cellW * 2 + textX, cellH * 2 + textY, "HRMN");
    screen.drawStr(cellW * 3 + textX, cellH * 2 + textY, "GRAN");
    screen.drawStr(cellW * 0 + textX, cellH * 3 + textY, "EXP");
    screen.drawStr(cellW * 1 + textX, cellH * 3 + textY, "INST");
    screen.drawStr(cellW * 2 + textX, cellH * 3 + textY, "BANK");
    screen.drawStr(cellW * 3 + textX, cellH * 3 + textY, "2/2");
  } else {
    return false;
  }
  return true;
}

void ScreenManager::UpdateMainScreen(Tracker &tracker, U8G2 &screen, char ledCommandOLED, int volumeBars[4], String noteChars[12]) {
  (void)ledCommandOLED;

  char buffa[16];
  if (tracker.lastNoteTrackIndex == lastNoteBeat) {
    if (noteBeatTime > 0) {
      noteBeatTime--;
    }
  } else {
    lastNoteBeat = tracker.lastNoteTrackIndex;
    noteBeatTime = 200;
  }

  const int splitX = 50;
  const int topBottomSplitY = 62;

  screen.drawFrame(0, 0, 128, 128);
  screen.drawLine(0, topBottomSplitY, 127, topBottomSplitY);
  screen.drawLine(splitX, 0, splitX, topBottomSplitY);

  // Compact left column for context only.
  screen.setFont(u8g2_font_6x13_tf);
  screen.drawStr(4, 12, tracker.oledInstString);

  snprintf(buffa, sizeof(buffa), "T%d", tracker.selectedTrack + 1);
  screen.drawStr(4, 27, buffa);

  snprintf(buffa, sizeof(buffa), "O%d", tracker.voices[tracker.selectedTrack].octave);
  screen.drawStr(4, 41, buffa);

  screen.drawStr(4, 55, tracker.voices[tracker.selectedTrack].samplerMode ? "SMP" : "NOT");

  // Bigger right column with large step readout.
  char stepBuf[12];
  if (!tracker.pressedOnce) {
    snprintf(stepBuf, sizeof(stepBuf), "00/%02d", tracker.patternLength);
  } else {
    int step = tracker.trackIndex % tracker.patternLength;
    snprintf(stepBuf, sizeof(stepBuf), "%02d/%02d", step, tracker.patternLength);
  }

  screen.setFont(u8g2_font_logisoso20_tf);
  screen.drawStr(splitX + 2, 31, stepBuf);

  screen.setFont(u8g2_font_6x13_tf);
  snprintf(buffa, sizeof(buffa), "P%d", tracker.currentPattern + 1);
  screen.drawStr(splitX + 2, 48, buffa);

  if (noteBeatTime > 0) {
    // Keep the blinking beat marker that you liked.
    screen.drawDisc(121, 10, 3);
    screen.setDrawColor(0);
    screen.drawDisc(121, 10, 1);
    screen.setDrawColor(1);
  }

  // Vertical track meters (legacy style): 4 columns with pulse and trigger dot.
  const int panelTop = 66;
  const int panelBottom = 125;
  const int barBottom = panelBottom - 9;
  const int barHeightMax = 44;
  const int colW = 30;
  const int barW = 16;
  const int barXOff = 7;

  for (int i = 0; i < 4; i++) {
    int val = abs(tracker.lastSamples[i]) / 110;
    if (val > 20) val = 20;

    if (volumeBars[i] < val) {
      volumeBars[i] = val;
    } else {
      volumeBars[i] -= 1;
      if (volumeBars[i] < 0) volumeBars[i] = 0;
    }

    int colX = i * colW;
    int barX = colX + barXOff;
    int barY = barBottom - barHeightMax;
    bool selected = (tracker.selectedTrack == i);

    if (selected) {
      screen.drawBox(colX + 2, panelTop + 1, colW - 4, 8);
      screen.setDrawColor(0);
      snprintf(buffa, sizeof(buffa), "T%d", i + 1);
      screen.setFont(u8g2_font_5x7_tf);
      screen.drawStr(colX + 10, panelTop + 8, buffa);
      screen.setDrawColor(1);
    } else {
      snprintf(buffa, sizeof(buffa), "T%d", i + 1);
      screen.setFont(u8g2_font_5x7_tf);
      screen.drawStr(colX + 10, panelTop + 8, buffa);
    }

    int fillH = (volumeBars[i] * barHeightMax) / 20;
    if (fillH < 0) fillH = 0;
    if (fillH > barHeightMax) fillH = barHeightMax;

    if (fillH > 0) {
      screen.drawBox(barX, barBottom - fillH, barW, fillH);
    }

    int noteAtStep = tracker.tracks[i][tracker.trackIndex];
    if (noteAtStep > 0) {
      screen.drawDisc(colX + (colW / 2), panelBottom - 2, 2);
    }
  }
}

void ScreenManager::ShowPotFeedback(const char *effectName, int value, int maxValue) {
  potFeedbackUntilMs = millis() + 160;
  strncpy(potFeedbackEffect, effectName, sizeof(potFeedbackEffect) - 1);
  potFeedbackEffect[sizeof(potFeedbackEffect) - 1] = '\0';
  potFeedbackValue = value;
  potFeedbackMax = maxValue;
}

void ScreenManager::DrawPotFeedback(U8G2 &screen) {
  if ((int32_t)(potFeedbackUntilMs - millis()) <= 0) return;
  
  // Draw compact popup so it disappears quickly and does not hide the whole UI.
  screen.setDrawColor(0);
  screen.drawBox(20, 44, 88, 40);
  screen.setDrawColor(1);
  screen.drawRFrame(20, 44, 88, 40, 3);
  
  screen.setFont(u8g2_font_5x7_tf);
  screen.drawStr(26, 54, potFeedbackEffect);
  
  int barWidth = 72;
  int barX = 28;
  int barY = 60;
  screen.drawRFrame(barX, barY, barWidth, 10, 2);
  
  if (potFeedbackMax > 0) {
    int filledWidth = (potFeedbackValue * barWidth) / potFeedbackMax;
    if (filledWidth > 0) {
      screen.drawBox(barX + 1, barY + 1, filledWidth - 1, 8);
    }
  }
  
  char valueBuf[16];
  snprintf(valueBuf, sizeof(valueBuf), "%d/%d", potFeedbackValue, potFeedbackMax);
  screen.drawStr(42, 79, valueBuf);
}
