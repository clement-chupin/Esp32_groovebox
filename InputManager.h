#ifndef InputManager_h
#define InputManager_h
class InputManager {
public:
  char note;
  char trackCommand;
  int trackCommandArgument;
  char ledCommand;
  int activeMenu;
  InputManager();
  void UpdateInput(int keyIndex);
  void EndFrame();

private:
  void SelectMenu(int menuIndex);
  void ProcessNoteGrid(int row, int col);
  void ProcessMenuAction(int row, int col);
};
#endif