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

  CHECK_CLOSE(EaseOutCubic(0.0f), 0.0f, 0.0001f, "EaseOutCubic at 0");
  CHECK_CLOSE(EaseOutCubic(0.5f), 0.875f, 0.001f, "EaseOutCubic at 0.5");
  CHECK_CLOSE(EaseOutCubic(1.2f), 1.0f, 0.0001f, "EaseOutCubic clamps to 1");

  CHECK_CLOSE(BlendProgress(0.5f, 0.0f), 0.325f, 0.0001f,
              "BlendProgress mixes spawn heavy");
  CHECK_CLOSE(BlendProgress(0.2f, 1.0f), 0.53f, 0.0001f,
              "BlendProgress respects settled weight");
  CHECK_CLOSE(BlendProgress(2.0f, 2.0f), 1.0f, 0.0001f,
              "BlendProgress clamps to 1");

  std::cout << "All loading progress tests passed!\n";
  return 0;
}
