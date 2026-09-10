#pragma once

#include <DirectXMath.h>
#include <cstdint>
#include <string>

namespace ecs {
class World;
}

namespace game::debug {

struct DebugCupInStatus {
  bool available = false;
  bool readyForCupIn = false;
  bool holeInOne = false;
  bool targetHole = false;
  bool withinHorizontalRange = false;
  bool withinVerticalRange = false;
  bool slowEnough = false;
  uint32_t holeEntity = 0;
  std::string linkTarget;
  float horizontalDistance = 0.0f;
  float captureRadius = 0.0f;
  float verticalOffset = 0.0f;
  float speed = 0.0f;
  DirectX::XMFLOAT3 ballPosition{};
  DirectX::XMFLOAT3 holePosition{};
};

DebugCupInStatus EvaluateCupInStatus(const DirectX::XMFLOAT3 &ballPosition,
                                     const DirectX::XMFLOAT3 &holePosition,
                                     float holeRadius,
                                     const DirectX::XMFLOAT3 &velocity,
                                     bool targetHole, int shotCount);
DebugCupInStatus CaptureCupInStatus(ecs::World &world);

} // namespace game::debug
