/**
 * @file EntityOwner.cpp
 * @brief ECS Entity所有クラスの実装
*/

#include "EntityOwner.h"
#include "World.h"
#include <algorithm>

namespace ecs {

Entity EntityOwner::Create(World &world) {
  const Entity entity = world.CreateEntity();
  m_entities.push_back(entity);
  return entity;
}

void EntityOwner::Track(Entity entity) {
  if (!IsValidEntity(entity)) {
    return;
  }

  if (Owns(entity)) {
    return;
  }

  m_entities.push_back(entity);
}

std::size_t EntityOwner::DestroyAll(World &world) {
  std::size_t destroyedCount = 0;
  for (const Entity entity : m_entities) {
    if (!world.IsAlive(entity)) {
      continue;
    }

    world.DestroyEntity(entity);
    ++destroyedCount;
  }

  m_entities.clear();
  return destroyedCount;
}

bool EntityOwner::Owns(Entity entity) const {
  const auto iterator = std::find(m_entities.begin(), m_entities.end(), entity);
  return iterator != m_entities.end();
}

} // namespace ecs
