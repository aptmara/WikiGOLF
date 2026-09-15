#pragma once

#include <DirectXMath.h>
#include <algorithm>
#include <cmath>

namespace game::utils {

struct CameraFramingAngles {
  float yaw = 0.0f;
  float pitch = 0.0f;
};

inline float NormalizeAngleDelta(float delta) {
  while (delta > DirectX::XM_PI) delta -= DirectX::XM_2PI;
  while (delta < -DirectX::XM_PI) delta += DirectX::XM_2PI;
  return delta;
}

inline CameraFramingAngles FindTargetCenteredOrbitAngles(
    const DirectX::XMFLOAT3 &orbitCenter, const DirectX::XMFLOAT3 &target,
    float currentYaw, float currentPitch) {
  const float dx = target.x - orbitCenter.x;
  const float dy = target.y - orbitCenter.y;
  const float dz = target.z - orbitCenter.z;
  const float horizontalDistance = std::sqrt(dx * dx + dz * dz);
  if (horizontalDistance < 0.01f) {
    return {currentYaw, currentPitch};
  }

  const float centeredYaw =
      currentYaw + NormalizeAngleDelta(std::atan2(dx, dz) - currentYaw);
  const float centeredPitch = std::clamp(
      -std::atan2(dy, horizontalDistance), -1.5f, 1.5f);
  return {centeredYaw, centeredPitch};
}

} // namespace game::utils
