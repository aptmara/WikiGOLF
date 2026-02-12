#include "JudgeFeedback.h"

namespace game::utils {

JudgeFeedback BuildJudgeFeedback(ShotJudgement judgement) {
  JudgeFeedback feedback{};

  switch (judgement) {
  case ShotJudgement::Special:
    feedback.texturePath = "ui_judge_perfect.png";
    feedback.displaySeconds = 3.0f;
    feedback.width = 300.0f;
    feedback.height = 100.0f;
    feedback.soundPath = "se_shot_hard.mp3";
    feedback.soundVolume = 1.0f;
    break;
  case ShotJudgement::Great:
    feedback.texturePath = "ui_judge_great.png";
    feedback.displaySeconds = 3.0f;
    feedback.soundPath = "se_shot_hard.mp3";
    feedback.soundVolume = 0.9f;
    break;
  case ShotJudgement::Nice:
    feedback.texturePath = "ui_judge_nice.png";
    feedback.displaySeconds = 3.0f;
    feedback.soundPath = "se_shot.mp3";
    feedback.soundVolume = 0.8f;
    break;
  case ShotJudgement::Miss:
    feedback.texturePath = "ui_judge_miss.png";
    feedback.displaySeconds = 2.0f; // 打った瞬間＋数秒のみ
    feedback.soundPath = "se_cancel.mp3";
    feedback.soundVolume = 1.0f;
    break;
  default:
    break;
  }

  return feedback;
}

} // namespace game::utils
