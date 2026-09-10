#include "DebugBallTrailHistory.h"

#include <algorithm>

namespace game::debug {

void DebugBallTrailHistory::Update(ecs::Entity entity,
                                   const DirectX::XMFLOAT3 &position,
                                   int sampleInterval,
                                   std::size_t maximumPoints) {
  sampleInterval = (std::max)(sampleInterval, 1);
  maximumPoints = (std::max<std::size_t>)(maximumPoints, 2);
  if (entity != m_entity) {
    Clear();
    m_entity = entity;
  }
  if (m_sampleFrame == 0) {
    m_points.push_back(position);
    while (m_points.size() > maximumPoints) {
      m_points.pop_front();
    }
  }
  m_sampleFrame = (m_sampleFrame + 1) % sampleInterval;
}

void DebugBallTrailHistory::Clear() {
  m_points.clear();
  m_sampleFrame = 0;
}

} // namespace game::debug
