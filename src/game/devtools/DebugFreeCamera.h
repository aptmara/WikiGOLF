#pragma once

#include "DebugFreeCameraMotion.h"
#include "../../ecs/Entity.h"

namespace core {
struct GameContext;
}

namespace game::debug {

class DebugFreeCamera {
public:
  void DrawControls();
  void Apply(core::GameContext &ctx, float realDeltaSeconds);

private:
  bool m_enabled = false;
  float m_moveSpeed = 15.0f;
  float m_fastMultiplier = 4.0f;
  float m_mouseSensitivity = 0.003f;
  ecs::Entity m_cameraEntity = ecs::NULL_ENTITY;
  DebugFreeCameraPose m_pose;
};

} // namespace game::debug
