#include "src/game/devtools/DebugInputCaptureRules.h"
#include <iostream>

int main() {
  if (!game::debug::IsGameInputMessage(WM_KEYDOWN) ||
      !game::debug::IsGameInputMessage(WM_LBUTTONDOWN) ||
      !game::debug::IsGameInputMessage(WM_MOUSEMOVE) ||
      game::debug::IsGameInputMessage(WM_SIZE) ||
      game::debug::IsGameInputMessage(WM_CLOSE)) {
    std::cerr << "ImGui input capture message classification failed\n";
    return 1;
  }
  return 0;
}
