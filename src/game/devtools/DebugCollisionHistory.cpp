#include "DebugCollisionHistory.h"

namespace game::debug {

bool DebugCollisionHistory::Update(
    const game::components::CollisionEvents *events,
    bool simulationAdvanced) {
  if (!simulationAdvanced) {
    return false;
  }
  ++m_simulationFrame;
  if (!events || events->events.empty()) {
    return false;
  }
  for (const auto &event : events->events) {
    m_records.push_back({m_simulationFrame, event});
  }
  if (m_records.size() > kMaximumEvents) {
    const std::size_t excess = m_records.size() - kMaximumEvents;
    m_records.erase(m_records.begin(), m_records.begin() + excess);
  }
  return true;
}

void DebugCollisionHistory::Clear() { m_records.clear(); }

} // namespace game::debug
