/**
 * @file Scene.cpp
 * @brief シーン基底クラスの実装
*/

#include "Scene.h"
#include "GameContext.h"

namespace core {

void Scene::DestroyAllEntities(GameContext& ctx) {
    const std::size_t trackedCount = m_entityOwner.GetTrackedCount();
    LOG_INFO("Scene", "Destroying {} entities for scene: {}", trackedCount,
             GetName());

    const std::size_t destroyedCount = m_entityOwner.DestroyAll(ctx.world);
    LOG_INFO("Scene", "Actually destroyed {}/{} entities", destroyedCount,
             trackedCount);
}

} // namespace core
