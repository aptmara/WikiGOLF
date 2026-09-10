#include "DebugFreeCameraMotion.h"

#include <algorithm>
#include <cmath>

namespace game::debug {

DebugFreeCameraPose StepDebugFreeCamera(DebugFreeCameraPose pose,
                                        const DebugFreeCameraInput &input,
                                        float deltaSeconds, float moveSpeed) {
  pose.yaw += input.yawDelta;
  pose.pitch = (std::clamp)(pose.pitch + input.pitchDelta, -1.5533f, 1.5533f);

  const float cosPitch = std::cos(pose.pitch);
  const DirectX::XMFLOAT3 forward = {
      std::sin(pose.yaw) * cosPitch, -std::sin(pose.pitch),
      std::cos(pose.yaw) * cosPitch};
  const DirectX::XMFLOAT3 right = {std::cos(pose.yaw), 0.0f,
                                   -std::sin(pose.yaw)};
  const float distance = deltaSeconds * moveSpeed * input.speedMultiplier;
  pose.position.x +=
      (forward.x * input.moveForward + right.x * input.moveRight) * distance;
  pose.position.y +=
      (forward.y * input.moveForward + input.moveUp) * distance;
  pose.position.z +=
      (forward.z * input.moveForward + right.z * input.moveRight) * distance;
  return pose;
}

} // namespace game::debug
