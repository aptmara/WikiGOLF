#include "src/game/scenes/LoadingSceneUtils.h"
#include <cmath>
#include <iostream>

#define CHECK_CLOSE(actual, expected, eps, message)                            \
  do {                                                                         \
    if (std::fabs((actual) - (expected)) > (eps)) {                            \
      std::cerr << "[FAIL] " << message << " (expected " << (expected)         \
                << ", got " << (actual) << ")\n";                              \
      std::exit(1);                                                            \
    } else {                                                                   \
      std::cout << "[PASS] " << message << "\n";                               \
    }                                                                          \
  } while (0)

int main() {
  using namespace game::scenes::loading_detail;

  CHECK_CLOSE(FadeOverlayAlpha(0.0f, false), 0.0f, 0.0001f,
              "FadeOverlayAlpha stays zero before start");
  CHECK_CLOSE(FadeOverlayAlpha(0.4f, true), 0.4f, 0.0001f,
              "FadeOverlayAlpha passes through active alpha");
  CHECK_CLOSE(FadeOverlayAlpha(1.5f, true), 1.0f, 0.0001f,
              "FadeOverlayAlpha clamps upper bound");
  CHECK_CLOSE(FadeOverlayAlpha(-0.2f, true), 0.0f, 0.0001f,
              "FadeOverlayAlpha clamps lower bound");

  std::cout << "All loading scene fade tests passed!\n";
  return 0;
}
