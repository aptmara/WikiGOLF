#pragma once
/**
 * @file JudgeFeedback.h
 * @brief 判定演出（画像・サウンド）の共通設定
 */

#include "../components/WikiComponents.h"
#include <string>

namespace game::utils {

using game::components::ShotJudgement;

struct JudgeFeedback {
  std::string texturePath;
  float displaySeconds = 0.0f;
  float width = 200.0f;
  float height = 80.0f;
  std::string soundPath;
  float soundVolume = 1.0f;

  bool HasVisual() const {
    return !texturePath.empty() && displaySeconds > 0.0f;
  }

  bool HasSound() const { return !soundPath.empty(); }
};

/// @brief 判定種類に応じた演出設定を返す
JudgeFeedback BuildJudgeFeedback(ShotJudgement judgement);

} // namespace game::utils
