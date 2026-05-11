#ifndef ScreenManager_h
#define ScreenManager_h
#include "Tracker.h"
#include <U8g2lib.h>

class ScreenManager {
public:
  ScreenManager();
  void Update(Tracker &tracker, U8G2 &screen, char ledCommandOLED, int volumeBars[4], String noteChars[12]);
  void ShowPotFeedback(const char *effectName, int value, int maxValue);

private:
  int lastNoteBeat;
  int noteBeatTime;
  uint32_t potFeedbackUntilMs;
  char potFeedbackEffect[16];
  int potFeedbackValue;
  int potFeedbackMax;
  void UpdateMainScreen(Tracker &tracker, U8G2 &screen, char ledCommandOLED, int volumeBars[4], String noteChars[12]);
  bool UpdateInstructionsScreen(Tracker &tracker, U8G2 &screen, char ledCommandOLED, int volumeBars[4], String noteChars[12]);
  void DrawPotFeedback(U8G2 &screen);
};

#endif