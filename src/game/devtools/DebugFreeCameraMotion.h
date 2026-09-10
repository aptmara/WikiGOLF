#pragma once

#include <DirectXMath.h>

namespace game::debug {

struct DebugFreeCameraInput {
  float moveForward = 0.0f;
  float moveRight = 0.0f;
  float moveUp = 0.0f;
  float yawDelta = 0.0f;
  float pitchDelta = 0.0f;
  float speedMultiplier = 1.0f;
};

struct DebugFreeCameraPose {
  DirectX::XMFLOAT3 position{};
  float yaw = 0.0f;
  float pitch = 0.0f;
};

DebugFreeCameraPose StepDebugFreeCamera(DebugFreeCameraPose pose,
                                        const DebugFreeCameraInput &input,
                                        float deltaSeconds, float moveSpeed);

} // namespace game::debug
