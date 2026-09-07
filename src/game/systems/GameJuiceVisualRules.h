#pragma once
/**
 * @file GameJuiceVisualRules.h
 * @brief Game Juiceの粒子演出で共有する純粋な表示計算です。
*/

#include "GameJuiceSystem.h"
#include <DirectXMath.h>
#include <algorithm>
#include <cstdlib>

namespace game::systems::juice_detail {

/** @brief 0.0から1.0の乱数を返します。*/
inline float Rand01() {
  return static_cast<float>(rand() % 100) / 100.0f;
}

/** @brief -0.5から0.5の乱数を返します。*/
inline float RandCentered() {
  return Rand01() - 0.5f;
}

/** @brief 始点から終点へ向かう滑らかな減衰率を返します。*/
inline float SmoothFade(float value, float start, float end) {
  const float t = std::clamp((value - start) / (end - start), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

/** @brief 色を指定倍率で明るくします。*/
inline DirectX::XMFLOAT4 ScaleColor(const DirectX::XMFLOAT3 &color,
                                    float brightness,
                                    float alpha = 1.0f) {
  return {color.x * brightness, color.y * brightness, color.z * brightness,
          alpha};
}

/** @brief カップイン祝祭粒子用の色を返します。*/
inline DirectX::XMFLOAT3 CupInSparkleColor(int index) {
  static const DirectX::XMFLOAT3 kPalette[] = {
      {1.0f, 0.78f, 0.16f}, {1.0f, 0.94f, 0.45f},
      {1.0f, 1.0f, 0.82f},  {1.0f, 0.62f, 0.08f},
      {0.55f, 0.9f, 1.0f},  {1.0f, 0.86f, 0.24f},
  };
  return kPalette[index % (sizeof(kPalette) / sizeof(kPalette[0]))];
}

/** @brief ショット判定に対応した発光色を返します。*/
inline DirectX::XMFLOAT3 ShotJudgeColor(GameJuiceSystem::JudgeType judge,
                                         int index) {
  switch (judge) {
  case GameJuiceSystem::JudgeType::Special: {
    static const DirectX::XMFLOAT3 kSpecial[] = {
        {1.0f, 0.92f, 0.28f}, {0.45f, 0.95f, 1.0f},
        {1.0f, 0.55f, 1.0f},  {0.85f, 1.0f, 0.45f},
    };
    return kSpecial[index % (sizeof(kSpecial) / sizeof(kSpecial[0]))];
  }
  case GameJuiceSystem::JudgeType::Great:
    return {1.0f, 0.82f, 0.18f};
  case GameJuiceSystem::JudgeType::Nice:
    return {0.35f, 0.78f, 1.0f};
  case GameJuiceSystem::JudgeType::Miss:
    return {1.0f, 0.28f, 0.10f};
  default:
    return {1.0f, 0.86f, 0.42f};
  }
}

} // namespace game::systems::juice_detail
