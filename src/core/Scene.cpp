/**
 * @file Scene.cpp
 * @brief シーン基底クラスの実装
 */

#include "Scene.h"
#include "GameContext.h"

namespace core {

void Scene::DestroyAllEntities(GameContext& ctx) {
    LOG_INFO("Scene", "Destroying {} entities for scene: {}", m_entities.size(), GetName());
    int count = 0;
    for (auto e : m_entities) {
        if (ctx.world.IsAlive(e)) {
            ctx.world.DestroyEntity(e);
            count++;
        }
    }
    LOG_INFO("Scene", "Actually destroyed {}/{} entities", count, m_entities.size());
    m_entities.clear();
}

} // namespace core
