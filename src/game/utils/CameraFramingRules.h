#pragma once

#include <DirectXMath.h>
#include <algorithm>
#include <cmath>

namespace game::utils {

struct CameraFramingAngles {
  float yaw = 0.0f;
  float pitch = 0.0f;
};

// 自動フレーミングで使うピッチ範囲（正 = 見下ろし）
constexpr float kFramingMinPitch = 0.12f;
constexpr float kFramingMaxPitch = 1.2f;
// 目標を置く画面上の高さ（画面中心=0、上端=1）
constexpr float kFramingTargetScreenY = 0.6f;

inline float NormalizeAngleDelta(float delta) {
  while (delta > DirectX::XM_PI) delta -= DirectX::XM_2PI;
  while (delta < -DirectX::XM_PI) delta += DirectX::XM_2PI;
  return delta;
}

/**
 * @brief オービットカメラ(中心を注視)から見た目標の画面上の高さを求める。
 * @return 画面中心=0、上端=1、下端=-1
 */
inline float TargetScreenYForOrbitPitch(float pitch, float orbitDistance,
                                        float horizontalDistance, float dy,
                                        float verticalFov) {
  const float camHeight = orbitDistance * std::sin(pitch);
  const float camBack = orbitDistance * std::cos(pitch);
  const float targetDownAngle =
      std::atan2(camHeight - dy, camBack + horizontalDistance);
  // 90度を超えるとtanが符号反転するため、真上/真下の手前で止める
  const float limit = DirectX::XM_PIDIV2 - 0.01f;
  const float offset = std::clamp(pitch - targetDownAngle, -limit, limit);
  return std::tan(offset) / std::tan(verticalFov * 0.5f);
}

/**
 * @brief 目標方向へ向き、中心(ボール)と目標が両方見えるYaw/Pitchを求める。
 * @details 中心は画面中央、目標は画面上部(kFramingTargetScreenY)に
 *          来るよう見下ろし角を二分探索で決める。
 */
inline CameraFramingAngles FindTargetCenteredOrbitAngles(
    const DirectX::XMFLOAT3 &orbitCenter, const DirectX::XMFLOAT3 &target,
    float currentYaw, float currentPitch, float orbitDistance,
    float verticalFov) {
  const float dx = target.x - orbitCenter.x;
  const float dy = target.y - orbitCenter.y;
  const float dz = target.z - orbitCenter.z;
  const float horizontalDistance = std::sqrt(dx * dx + dz * dz);
  if (horizontalDistance < 0.01f) {
    return {currentYaw, currentPitch};
  }

  const float centeredYaw =
      currentYaw + NormalizeAngleDelta(std::atan2(dx, dz) - currentYaw);

  // 見下ろすほど目標は画面上へ移動するため、目標の画面高さはピッチに対して単調増加
  auto screenY = [&](float pitch) {
    return TargetScreenYForOrbitPitch(pitch, orbitDistance,
                                      horizontalDistance, dy, verticalFov);
  };
  float pitch = kFramingMinPitch;
  if (screenY(kFramingMaxPitch) <= kFramingTargetScreenY) {
    pitch = kFramingMaxPitch;
  } else if (screenY(kFramingMinPitch) < kFramingTargetScreenY) {
    float lo = kFramingMinPitch;
    float hi = kFramingMaxPitch;
    for (int i = 0; i < 24; ++i) {
      const float mid = (lo + hi) * 0.5f;
      if (screenY(mid) < kFramingTargetScreenY) {
        lo = mid;
      } else {
        hi = mid;
      }
    }
    pitch = lo;
  }
  return {centeredYaw, pitch};
}

} // namespace game::utils
