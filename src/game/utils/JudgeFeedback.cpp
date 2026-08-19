#include "JudgeFeedback.h"

namespace game::utils {

JudgeFeedback BuildJudgeFeedback(ShotJudgement judgement) {
  JudgeFeedback feedback{};

  // 各判定画像は元テクスチャの縦横比が異なる(perfect≈3.69:1, great≈3.44:1,
  // nice≈2.51:1, miss≈2.47:1)ため、比率を保ったまま表示サイズだけを指定する。
  // 以前はここを無視して全判定を一律512x256で描画していたため気付かれて
  // いなかったが、その値をそのまま使うと Great/Nice/Miss がデフォルトの
  // 200x80のまま極端に小さく表示されてしまっていた。
  switch (judgement) {
  case ShotJudgement::Special:
    feedback.texturePath = "ui_judge_perfect.png";
    feedback.displaySeconds = 3.0f;
    feedback.width = 460.0f;
    feedback.height = 125.0f;
    feedback.soundPath = "se_judge_perfect.mp3";
    feedback.soundVolume = 1.0f;
    break;
  case ShotJudgement::Great:
    feedback.texturePath = "ui_judge_great.png";
    feedback.displaySeconds = 3.0f;
    feedback.width = 400.0f;
    feedback.height = 116.0f;
    feedback.soundPath = "se_judge_great.mp3";
    feedback.soundVolume = 0.9f;
    break;
  case ShotJudgement::Nice:
    feedback.texturePath = "ui_judge_nice.png";
    feedback.displaySeconds = 3.0f;
    feedback.width = 380.0f;
    feedback.height = 151.0f;
    feedback.soundPath = "se_judge_nice.mp3";
    feedback.soundVolume = 0.8f;
    break;
  case ShotJudgement::Miss:
    feedback.texturePath = "ui_judge_miss.png";
    feedback.displaySeconds = 2.0f; // 打った瞬間＋数秒のみ
    feedback.width = 380.0f;
    feedback.height = 154.0f;
    feedback.soundPath = "se_judge_miss.mp3";
    feedback.soundVolume = 1.0f;
    break;
  default:
    break;
  }

  return feedback;
}

} // namespace game::utils
