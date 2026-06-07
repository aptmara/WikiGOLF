#include "src/game/utils/ShotGaugeRules.h"
#include <cmath>
#include <cstdlib>
#include <iostream>

#define CHECK_TRUE(condition, message)                                          \
  do {                                                                         \
    if (!(condition)) {                                                         \
      std::cerr << "[FAIL] " << message << "\n";                              \
      std::exit(1);                                                            \
    }                                                                          \
    std::cout << "[PASS] " << message << "\n";                                \
  } while (0)

#define CHECK_CLOSE(actual, expected, eps, message)                             \
  do {                                                                         \
    if (std::fabs((actual) - (expected)) > (eps)) {                            \
      std::cerr << "[FAIL] " << message << " (expected " << (expected)         \
                << ", got " << (actual) << ")\n";                             \
      std::exit(1);                                                            \
    }                                                                          \
    std::cout << "[PASS] " << message << "\n";                                \
  } while (0)

int main() {
  using game::components::ShotJudgement;
  using game::utils::EvaluateImpactJudgement;
  using game::utils::GetImpactZoneVisualWidth;

  CHECK_TRUE(EvaluateImpactJudgement(0.5f) == ShotJudgement::Special,
             "center impact is Special");
  CHECK_TRUE(EvaluateImpactJudgement(0.5f + game::ui::kThresholdSpecial +
                                     0.0001f) ==
                 ShotJudgement::Great,
             "impact just outside Special is Great");
  CHECK_TRUE(EvaluateImpactJudgement(0.5f + game::ui::kThresholdGreat +
                                     0.0001f) ==
                 ShotJudgement::Nice,
             "impact just outside Great is Nice");
  CHECK_TRUE(EvaluateImpactJudgement(0.5f + game::ui::kThresholdNice +
                                     0.0001f) ==
                 ShotJudgement::Miss,
             "impact just outside Nice is Miss");

  CHECK_CLOSE(GetImpactZoneVisualWidth(game::ui::kThresholdSpecial),
              game::ui::kThresholdSpecial * 2.0f, 0.0001f,
              "Special visual zone covers both sides of threshold");
  CHECK_CLOSE(GetImpactZoneVisualWidth(game::ui::kThresholdGreat),
              game::ui::kThresholdGreat * 2.0f, 0.0001f,
              "Great visual zone covers both sides of threshold");
  CHECK_CLOSE(GetImpactZoneVisualWidth(game::ui::kThresholdNice),
              game::ui::kThresholdNice * 2.0f, 0.0001f,
              "Nice visual zone covers both sides of threshold");

  std::cout << "All shot gauge rule tests passed!\n";
  return 0;
}
