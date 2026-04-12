#pragma once
/**
 * @file PhysicsFriction.h
 * @brief 転がり摩擦の共通計算ヘルパー
 */

#include "../components/WikiComponents.h"
#include <algorithm>
#include <cmath>

namespace game::systems {

/// @brief 摩擦係数と時間から速度減少量を計算する
/// @param frictionCoeff 摩擦係数 (無次元)
/// @param dtSeconds 経過時間 (秒)
/// @return このフレームで減少する速度量
inline float ComputeRollingFrictionDrop(float frictionCoeff, float dtSeconds) {
  if (dtSeconds <= 0.0f || !std::isfinite(dtSeconds)) {
    return 0.0f;
  }

  float coeff = std::max(frictionCoeff, 0.0f);
  if (!std::isfinite(coeff)) {
    return 0.0f;
  }

  constexpr float gravity = 9.8f; // [m/s^2]
  return coeff * gravity * dtSeconds;
}

/// @brief 転がり摩擦を適用した後の速度を返す
/// @param speed 現在の速度量
/// @param frictionCoeff 摩擦係数
/// @param dtSeconds 経過時間 (秒)
/// @return 摩擦適用後の速度量
inline float ApplyRollingFriction(float speed, float frictionCoeff,
                                  float dtSeconds) {
  if (speed <= 0.0f || !std::isfinite(speed)) {
    return 0.0f;
  }

  if (dtSeconds <= 0.0f || !std::isfinite(dtSeconds)) {
    return speed;
  }

  float coeff = std::max(frictionCoeff, 0.0f);
  if (!std::isfinite(coeff) || coeff <= 0.0f) {
    return speed;
  }

  // 転がり摩擦は指数減衰ではなく、加速度ベースで速度を削る。
  // v = v0 - μgt
  float drop = ComputeRollingFrictionDrop(coeff, dtSeconds);

  // 速度を下回ったら停止。
  // ここを少しだけ持たせることで、極低速で震え続けるのを防ぐ。
  constexpr float stopEpsilon = 0.025f;
  if (speed <= drop + stopEpsilon) {
    return 0.0f;
  }

  return speed - drop;
}

/// @brief 静止摩擦で速度を止められるか判定する
/// @param speed 現在速度
/// @param frictionCoeff 摩擦係数
/// @param tangentialAccel 接地面に沿った加速度の大きさ
/// @param dtSeconds 経過時間 (秒)
/// @param stickSpeedThreshold 静止とみなす速度上限
inline bool CanStaticFrictionHold(float speed, float frictionCoeff,
                                  float tangentialAccel, float dtSeconds,
                                  float stickSpeedThreshold = 0.35f) {
  if (dtSeconds <= 0.0f || !std::isfinite(dtSeconds)) {
    return false;
  }
  if (!std::isfinite(speed) || speed > stickSpeedThreshold) {
    return false;
  }

  float coeff = std::max(frictionCoeff, 0.0f);
  if (!std::isfinite(coeff)) {
    return false;
  }

  float frictionDrop = ComputeRollingFrictionDrop(coeff, dtSeconds);
  if (frictionDrop <= 0.0f) {
    return false;
  }

  float tangentialPush =
      std::max(tangentialAccel, 0.0f) * dtSeconds; // a * dt = Δv
  return frictionDrop >= tangentialPush;
}

/**
 * @brief ゴルフ用の芝フリクション設定
 *
 * ベース係数と速度に応じたブーストをまとめて管理する。
 */
struct SurfaceFrictionSettings {
  float baseRollingFriction = 0.14f;
  float slowSpeedReference = 4.0f;
  float slowFrictionExponent = 0.8f;
  float slowFrictionMinMultiplier = 0.2f;
  float highSpeedBoostStart = 22.0f;
  float highSpeedBoostScale = 0.25f;
  float slopeFrictionFloor = 0.35f;
  float constantBrake = 0.08f;
  float greenMultiplier = 1.45f;
  float roughMultiplier = 2.5f;
  float bunkerMultiplier = 4.0f;
};

inline SurfaceFrictionSettings DefaultSurfaceFrictionSettings() {
  return SurfaceFrictionSettings{};
}

inline float
GetMaterialFrictionMultiplier(game::components::TerrainMaterial mat,
                              const SurfaceFrictionSettings &settings) {
  using game::components::TerrainMaterial;
  switch (mat) {
  case TerrainMaterial::Rough:
    return settings.roughMultiplier;
  case TerrainMaterial::Bunker:
    return settings.bunkerMultiplier;
  case TerrainMaterial::Green:
    return settings.greenMultiplier;
  case TerrainMaterial::Ice:
    return 0.08f;
  case TerrainMaterial::Water:
    return 8.0f;
  case TerrainMaterial::Stone:
    return 0.6f;
  case TerrainMaterial::Lava:
    return 10.0f;
  default:
    return 1.0f;
  }
}

/// @brief 低速域で粘るための非線形スケールを計算
inline float
ComputeSlowRollMultiplier(float speed,
                          const SurfaceFrictionSettings &settings) {
  float normalized = std::clamp(
      speed / std::max(settings.slowSpeedReference, 0.0001f), 0.0f, 1.0f);
  float eased = std::pow(normalized, settings.slowFrictionExponent);
  return settings.slowFrictionMinMultiplier +
         (1.0f - settings.slowFrictionMinMultiplier) * eased;
}

/// @brief 高速時の摩擦ブーストを計算
inline float ComputeHighSpeedBoost(float speed,
                                   const SurfaceFrictionSettings &settings) {
  float over = speed - settings.highSpeedBoostStart;
  float scale = settings.highSpeedBoostScale / 45.0f;
  float boost = over * scale;
  return std::clamp(boost, 0.0f, settings.highSpeedBoostScale);
}

/// @brief 斜面に応じた摩擦スケールを計算 (ny=1で1.0)
inline float ComputeSlopeScale(float normalY,
                               const SurfaceFrictionSettings &settings) {
  float ny = std::clamp(normalY, 0.0f, 1.0f);
  return settings.slopeFrictionFloor +
         (1.0f - settings.slopeFrictionFloor) * ny;
}

/// @brief ゴルフボールの芝上減速 [m/s^2] を計算
inline float
ComputeGrassRollingAcceleration(float speed, float normalY,
                                game::components::TerrainMaterial mat,
                                float terrainScale,
                                const SurfaceFrictionSettings &settings =
                                    DefaultSurfaceFrictionSettings()) {
  if (!std::isfinite(speed)) {
    speed = 0.0f;
  }
  float slowMul = ComputeSlowRollMultiplier(speed, settings);
  float speedBoost = ComputeHighSpeedBoost(speed, settings);
  float materialMul = GetMaterialFrictionMultiplier(mat, settings);
  float slopeScale = ComputeSlopeScale(normalY, settings);

  float frictionCoeff = settings.baseRollingFriction * slowMul *
                        (1.0f + speedBoost) * materialMul *
                        std::max(terrainScale, 0.0f);

  float frictionAccel = frictionCoeff * 9.8f * slopeScale;
  float brake =
      settings.constantBrake * (0.5f + 0.5f * slowMul); // 低速ほど粘らせる
  return frictionAccel + brake;
}

} // namespace game::systems
