#pragma once

#include <DirectXMath.h>

namespace ecs {
class World;
}

namespace game::debug {

enum class DebugBallTeleportResult {
  Success,
  MissingGameState,
  MissingBallComponents
};

bool CaptureDebugBallPosition(ecs::World &world,
                              DirectX::XMFLOAT3 &position);
DebugBallTeleportResult TeleportDebugBall(
    ecs::World &world, const DirectX::XMFLOAT3 &position, bool resetMotion);

} // namespace game::debug
