#pragma once

#include "../components/PhysicsComponents.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace game::debug {

struct DebugCollisionRecord {
  uint64_t simulationFrame = 0;
  game::components::CollisionEvent event;
};

class DebugCollisionHistory {
public:
  static constexpr std::size_t kMaximumEvents = 100;

  bool Update(const game::components::CollisionEvents *events,
              bool simulationAdvanced);
  void Clear();
  const std::vector<DebugCollisionRecord> &Records() const {
    return m_records;
  }

private:
  uint64_t m_simulationFrame = 0;
  std::vector<DebugCollisionRecord> m_records;
};

} // namespace game::debug
