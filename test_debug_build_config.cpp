#include "src/game/devtools/DebugBuildConfig.h"
#include <iostream>

int main() {
#ifdef WIKIGOLF_DEBUG_TOOLS
  static_assert(game::debug::kDebugToolsEnabled);
#else
  static_assert(!game::debug::kDebugToolsEnabled);
#endif
  std::cout << "Debug build configuration test passed!\n";
  return 0;
}
