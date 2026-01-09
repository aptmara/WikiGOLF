/**
 * @file Scene.cpp
 * @brief シーン基底クラスの実装
 */

#include "Scene.h"
#include "GameContext.h"

namespace core {

void Scene::DestroyAllEntities(GameContext& ctx) {
    for (auto e : m_entities) {
        ctx.world.DestroyEntity(e);
    }
    m_entities.clear();
}

} // namespace core
