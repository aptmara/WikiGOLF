#include "DebugGameplayCheckpoint.h"

#include "../../ecs/World.h"

namespace game::debug {

bool DebugGameplayCheckpoint::Save(ecs::World &world) {
  const auto *golf = world.GetGlobal<game::components::GolfGameState>();
  const auto *shot = world.GetGlobal<game::components::ShotState>();
  if (!golf || !shot) {
    return false;
  }
  const ecs::Entity ball = static_cast<ecs::Entity>(golf->ballEntity);
  const auto *transform = world.Get<game::components::Transform>(ball);
  const auto *body = world.Get<game::components::RigidBody>(ball);
  if (!transform || !body) {
    return false;
  }
  m_ballEntity = golf->ballEntity;
  m_transform = *transform;
  m_body = *body;
  m_golf = *golf;
  m_shot = *shot;
  m_saved = true;
  return true;
}

bool DebugGameplayCheckpoint::Restore(ecs::World &world) const {
  if (!m_saved) {
    return false;
  }
  auto *golf = world.GetGlobal<game::components::GolfGameState>();
  auto *shot = world.GetGlobal<game::components::ShotState>();
  if (!golf || !shot || golf->ballEntity != m_ballEntity) {
    return false;
  }
  const ecs::Entity ball = static_cast<ecs::Entity>(m_ballEntity);
  auto *transform = world.Get<game::components::Transform>(ball);
  auto *body = world.Get<game::components::RigidBody>(ball);
  if (!transform || !body) {
    return false;
  }
  *transform = m_transform;
  *body = m_body;
  *golf = m_golf;
  *shot = m_shot;
  return true;
}

} // namespace game::debug
