#pragma once

#include "../../ecs/Entity.h"
#include <DirectXMath.h>
#include <cstddef>
#include <deque>

namespace game::debug {

class DebugBallTrailHistory {
public:
  void Update(ecs::Entity entity, const DirectX::XMFLOAT3 &position,
              int sampleInterval, std::size_t maximumPoints);
  void Clear();
  const std::deque<DirectX::XMFLOAT3> &Points() const { return m_points; }

private:
  std::deque<DirectX::XMFLOAT3> m_points;
  ecs::Entity m_entity = ecs::NULL_ENTITY;
  int m_sampleFrame = 0;
};

} // namespace game::debug
