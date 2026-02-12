#pragma once
/**
 * @file PhysicsFriction.h
 * @brief 転がり摩擦の共通計算ヘルパー
 */

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

  float drop = ComputeRollingFrictionDrop(frictionCoeff, dtSeconds);
  if (drop <= 0.0f) {
    return speed;
  }

  return (speed <= drop) ? 0.0f : (speed - drop);
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

} // namespace game::systems
