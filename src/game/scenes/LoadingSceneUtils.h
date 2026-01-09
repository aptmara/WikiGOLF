#pragma once
/**
 * @file LoadingSceneUtils.h
 * @brief ローディング演出用の軽量ヘルパー
 */

#include <algorithm>

namespace game::scenes::loading_detail {

/// @brief イージング（ease-out cubic）
inline float EaseOutCubic(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  float inv = 1.0f - t;
  return 1.0f - inv * inv * inv;
}

/// @brief スポーン進行と静止率を混ぜて見栄えの良い進捗を返す
inline float BlendProgress(float spawnRatio, float settledRatio) {
  spawnRatio = std::clamp(spawnRatio, 0.0f, 1.0f);
  settledRatio = std::clamp(settledRatio, 0.0f, 1.0f);
  return std::clamp(spawnRatio * 0.65f + settledRatio * 0.35f, 0.0f, 1.0f);
}

} // namespace game::scenes::loading_detail
