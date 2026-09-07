/**
 * @file WikiGolfSceneSupport.cpp
 * @brief WikiGolfシーンの共有初期化処理を実装します。
 */

#include "WikiGolfSceneSupport.h"
#include "../../core/GameContext.h"
#include "../../resources/ResourceManager.h"

namespace game::scenes::scene_detail {

void PreloadGameplayResources(core::GameContext& ctx) {
    ctx.resource.LoadMesh("builtin/cube");
    ctx.resource.LoadMesh("builtin/sphere");
    ctx.resource.LoadMesh("builtin/cylinder");
    ctx.resource.LoadMesh("builtin/quad");
    ctx.resource.LoadShader("Basic", L"Assets/shaders/BasicVS.hlsl",
                            L"Assets/shaders/BasicPS.hlsl");
    ctx.resource.LoadShader("Particle", L"shaders/ParticleVS.hlsl",
                            L"shaders/ParticlePS.hlsl");
    ctx.resource.LoadShader("Terrain", L"Assets/shaders/TerrainVS.hlsl",
                            L"Assets/shaders/TerrainPS.hlsl");
}

} // namespace game::scenes::scene_detail
