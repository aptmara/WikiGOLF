#include "src/game/systems/PostProcessSystem.h"
#include <iostream>

#define CHECK(condition, message)                                               \
  do {                                                                          \
    if (!(condition)) {                                                         \
      std::cerr << "[FAIL] " << message << "\n";                                \
      return 1;                                                                 \
    } else {                                                                    \
      std::cout << "[PASS] " << message << "\n";                                \
    }                                                                           \
  } while (0)

int main() {
  game::systems::PostProcessSystem postProcess;
  game::components::EnvironmentState env;
  env.brightness = 0.6f;
  env.lightingMood = game::components::LightingMood::StormyDarkness;

  postProcess.UpdateFromEnvironment(env, 0.0f);
  const auto &constants = postProcess.GetConstants();

  CHECK(constants.colorTint.w >= 0.85f,
        "Brightness is clamped to keep text readable on dark themes");

  CHECK(constants.vignetteParams.x < 0.6f,
        "Vignette intensity eases when brightness is lifted");

  std::cout << "PostProcess readability tests passed.\n";
  return 0;
}
