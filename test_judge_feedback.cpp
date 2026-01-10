#include "src/game/utils/JudgeFeedback.h"
#include <cmath>
#include <iostream>

#define CHECK(condition, message)                                              \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << "[FAIL] " << message << "\n";                               \
      return 1;                                                                \
    } else {                                                                   \
      std::cout << "[PASS] " << message << "\n";                               \
    }                                                                          \
  } while (0)

int main() {
  using game::components::ShotJudgement;
  using game::utils::BuildJudgeFeedback;

  auto miss = BuildJudgeFeedback(ShotJudgement::Miss);
  CHECK(miss.HasVisual(), "Miss feedback provides a visual");
  CHECK(miss.texturePath == "ui_judge_miss.png",
        "Miss uses miss texture");
  CHECK(std::abs(miss.displaySeconds - 2.0f) < 1e-4f,
        "Miss display lasts a few seconds");
  CHECK(miss.HasSound(), "Miss feedback has sound");
  CHECK(miss.soundPath == "se_cancel.mp3", "Miss uses cancel sound");

  auto great = BuildJudgeFeedback(ShotJudgement::Great);
  CHECK(great.HasVisual(), "Great feedback provides a visual");
  CHECK(great.HasSound(), "Great feedback has sound");

  std::cout << "All JudgeFeedback tests passed.\n";
  return 0;
}
