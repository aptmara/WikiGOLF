#pragma once

#include <DirectXMath.h>

namespace ecs {
class World;
}

namespace game::debug {

enum class DebugBallImpulseResult {
  Success,
  MissingBall,
  ZeroMass
};

DebugBallImpulseResult ApplyDebugBallImpulse(
    ecs::World &world, const DirectX::XMFLOAT3 &impulse);

} // namespace game::debug
