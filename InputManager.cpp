#include "Arduino.h"
#include "InputManager.h"

InputManager::InputManager() {
  activeMenu = 0;
}

void InputManager::UpdateInput(int keyIndex) {
  note = ' ';
  trackCommand = ' ';
  ledCommand = ' ';

  if (keyIndex < 0 || keyIndex >= 32) {
    return;
  }

  int row = keyIndex / 8;
  int col = keyIndex % 8;

  // Green silence key also cancels menu navigation without triggering actions.
  if (activeMenu != 0 && keyIndex == 31) {
    activeMenu = 0;
    ledCommand = 'T';
    return;
  }

  // In note mode, top row selects the menu.
  if (activeMenu == 0 && row == 0) {
    SelectMenu(col);
    return;
  }

  if (activeMenu == 0) {
    ProcessNoteGrid(row, col);
  } else {
    ProcessMenuAction(row, col);
  }
}

void InputManager::SelectMenu(int menuIndex) {
  // Keep 0 reserved for main note mode; F1..F8 become menus 1..8.
  activeMenu = menuIndex + 1;
  ledCommand = (char)('A' + (menuIndex % 8));
}

void InputManager::ProcessNoteGrid(int row, int col) {
  // Keep the silence key on bottom-right of the 3x8 note area.
  if (row == 3 && col == 7) {
    trackCommand = 'Z';
    trackCommandArgument = 0;
    return;
  }

  // Notes progress bottom->top inside each column.
  int noteRow = 3 - row;          // row1->2, row2->1, row3->0
  int noteSlot = (col * 3) + noteRow;

  trackCommand = 'N';
  trackCommandArgument = noteSlot;
}

void InputManager::ProcessMenuAction(int row, int col) {
  int col4 = col % 4;
  int action16 = row * 4 + col4;               // 0..15 within each 4x4 block
  int action16BottomToTop = (3 - row) * 4 + col4;

  switch (activeMenu) {
    case 1: {  // Basic instruments/scales
      // Layout bottom->top, left->right:
      // Row3 (bas, instrAction 0-3)  : DRUM0 DRUM1 INS2 INS3
      // Row2        (instrAction 4-7)  : INS4 INS5 INS6 INS7
      // Row1        (instrAction 8-11) : INS8 INS9 INS10 INS11
      // Row0 (haut, instrAction 12-15) : SFX6 SCA1 SCA2 SCA3
      int instrAction = action16BottomToTop;

      if (instrAction == 0) {
        trackCommand = 'I';
        trackCommandArgument = 0;  // DRUM0
      } else if (instrAction == 1) {
        trackCommand = 'I';
        trackCommandArgument = 1;  // DRUM1
      } else if (instrAction == 2) {
        trackCommand = 'I';
        trackCommandArgument = 2;  // INS2
      } else if (instrAction == 3) {
        trackCommand = 'I';
        trackCommandArgument = 3;  // INS3
      } else if (instrAction >= 4 && instrAction <= 7) {
        trackCommand = 'I';
        trackCommandArgument = instrAction;  // 4..7
      } else if (instrAction >= 8 && instrAction <= 11) {
        trackCommand = 'I';
        trackCommandArgument = instrAction;  // 8..11
      } else if (instrAction == 12) {
        trackCommand = 'I';
        trackCommandArgument = 12;  // SFX6
      } else if (instrAction >= 13 && instrAction <= 15) {
        trackCommand = 'S';
        trackCommandArgument = instrAction - 13;  // SCA1/2/3
      }
      break;
    }

    case 2:  // Base effects / envelope
      // Match overlay labels exactly:
      // Row0: MUTE VOL OVDR SOLO  -> V
      // Row1: ENV1 ENV2 ENV3 LOOP -> E
      // Row2: ECHO CHRD WOOS PTCH -> D
      // Row3: NOFX LOWP RTRG WOBB -> A
      if (action16 < 4) {
        trackCommand = 'V';
        trackCommandArgument = action16;
      } else if (action16 < 8) {
        trackCommand = 'E';
        trackCommandArgument = action16 - 4;
      } else if (action16 < 12) {
        trackCommand = 'D';
        trackCommandArgument = action16 - 8;
      } else {
        trackCommand = 'A';
        trackCommandArgument = action16 - 12;
      }
      break;

    case 3:  // Track/pattern utility
      // Match overlay labels exactly:
      // Row0: TRK1..4 -> T
      // Row1: CLT1..4 -> ^
      // Row2: PAT1..4 -> $
      // Row3: CLP1..4 -> #
      if (action16 < 4) {
        trackCommand = 'T';
        trackCommandArgument = action16;
      } else if (action16 < 8) {
        trackCommand = '^';
        trackCommandArgument = action16 - 4;
      } else if (action16 < 12) {
        trackCommand = '$';
        trackCommandArgument = action16 - 8;
      } else {
        trackCommand = '#';
        trackCommandArgument = action16 - 12;
      }
      break;

    case 4:  // Transport/system
      // Match overlay labels exactly:
      // Row0: NLNG NMED NSRT REC -> L0 L1 L2 P
      // Row1: B-5 B+5 B-10 B+10 -> B0..3
      // Row2: CPAT PPAT PALL N/SP -> *0..3
      // Row3: RSTS RSTL MVOL PTSN -> X0 X1 H C
      if (action16 == 0) {
        trackCommand = 'L';
        trackCommandArgument = 0;
      } else if (action16 == 1) {
        trackCommand = 'L';
        trackCommandArgument = 1;
      } else if (action16 == 2) {
        trackCommand = 'L';
        trackCommandArgument = 2;
      } else if (action16 == 3) {
        trackCommand = 'P';
        trackCommandArgument = 0;
      } else if (action16 >= 4 && action16 <= 7) {
        trackCommand = 'B';
        trackCommandArgument = action16 - 4;
      } else if (action16 >= 8 && action16 <= 11) {
        trackCommand = '*';
        trackCommandArgument = action16 - 8;
      } else if (action16 == 12) {
        trackCommand = 'X';
        trackCommandArgument = 0;
      } else if (action16 == 13) {
        trackCommand = 'X';
        trackCommandArgument = 1;
      } else if (action16 == 14) {
        trackCommand = 'H';
        trackCommandArgument = 0;
      } else {
        trackCommand = 'C';
        trackCommandArgument = 0;
      }
      break;

    case 5:  // Mastering: mute/unmute and volume +/- per track
      if (action16 < 4) {
        trackCommand = 'q';  // mute track
        trackCommandArgument = action16;
      } else if (action16 < 8) {
        trackCommand = 'u';  // unmute track
        trackCommandArgument = action16 - 4;
      } else if (action16 < 12) {
        trackCommand = '+';  // volume +
        trackCommandArgument = action16 - 8;
      } else {
        trackCommand = '-';  // volume -
        trackCommandArgument = action16 - 12;
      }
      break;

    case 6:  // Arpeggio menu (octave/major/minor/slide)
      // Unique arp layout with intentional empty cells:
      // Row0: NONE OCT MIN MAJ
      // Row1: SLIDE OCT2 --- ---
      if (action16 == 0) {
        trackCommand = 'Y';
        trackCommandArgument = 0;  // NONE
      } else if (action16 == 1) {
        trackCommand = 'Y';
        trackCommandArgument = 1;  // OCT
      } else if (action16 == 2) {
        trackCommand = 'Y';
        trackCommandArgument = 2;  // MIN
      } else if (action16 == 3) {
        trackCommand = 'Y';
        trackCommandArgument = 3;  // MAJ
      } else if (action16 == 4) {
        trackCommand = 'Y';
        trackCommandArgument = 4;  // SLIDE
      } else if (action16 == 5) {
        trackCommand = 'Y';
        trackCommandArgument = 5;  // OCT2
      }
      break;

    case 7:  // Experimental effects
      trackCommand = 'F';
      trackCommandArgument = action16;
      break;

    default: // 8th menu: experimental instruments
      trackCommand = 'U';
      trackCommandArgument = action16;
      break;
  }

  // Return to main only when an action is actually assigned.
  if (trackCommand != ' ') {
    activeMenu = 0;
    ledCommand = 'T';
  }
}

void InputManager::EndFrame() {
  ledCommand = ' ';
  trackCommand = ' ';
  trackCommandArgument = 0;
}
