/**
 * @file WikiTerrainSurfaceResponse.cpp
 * @brief ボールが芝の上を転がるときの踏み跡の入力
 */

#include "WikiTerrainSystem.h"
#include "../../core/GameContext.h"
#include "../../ecs/World.h"
#include "../components/GrassField.h"
#include "../components/PhysicsComponents.h"
#include "../components/Transform.h"
#include "../components/WikiComponents.h"

namespace game::systems {

using namespace game::components;

void WikiTerrainSystem::UpdateSurfaceResponse(core::GameContext &ctx,
                                              ecs::Entity ballEntity) {
  GrassTrampleInput input;
  const auto *ballTransform = ctx.world.IsAlive(ballEntity)
                                  ? ctx.world.Get<Transform>(ballEntity)
                                  : nullptr;
  if (ballTransform) {
    const auto *ballBody = ctx.world.Get<RigidBody>(ballEntity);
    const auto *state = ctx.world.GetGlobal<GolfGameState>();
    input.position = ballTransform->position;
    if (ballBody) {
      input.velocity = {ballBody->velocity.x, ballBody->velocity.z};
    }
    input.touching = state && state->isBallGrounded &&
                     (state->currentMaterial == TerrainMaterial::Fairway ||
                      state->currentMaterial == TerrainMaterial::Rough ||
                      state->currentMaterial == TerrainMaterial::Green);
  }
  ctx.world.SetGlobal(std::move(input));
}

} // namespace game::systems
