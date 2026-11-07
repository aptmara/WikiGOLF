/**
 * @file WikiTerrainSurfaceResponse.cpp
 * @brief ボールが芝の上を転がるときの踏み跡の入力と、芝の上への着地
 */

#include "WikiTerrainSystem.h"
#include "../../core/GameContext.h"
#include "../../ecs/World.h"
#include "../components/GrassField.h"
#include "../components/PhysicsComponents.h"
#include "../components/Transform.h"
#include "../components/WikiComponents.h"
#include "GrassFieldRenderer.h"
#include "GrassImpulseRules.h"

namespace game::systems {

using namespace game::components;

void WikiTerrainSystem::UpdateSurfaceResponse(core::GameContext &ctx,
                                              ecs::Entity ballEntity) {
  GrassTrampleInput input;
  const GrassTrampleInput previous =
      ctx.world.GetGlobal<GrassTrampleInput>() ? *ctx.world.GetGlobal<GrassTrampleInput>()
                                               : GrassTrampleInput{};
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
    const bool onGrass = state && (state->currentMaterial == TerrainMaterial::Fairway ||
                                   state->currentMaterial == TerrainMaterial::Rough ||
                                   state->currentMaterial == TerrainMaterial::Green);
    input.touching = state && state->isBallGrounded && onGrass;
    input.airborne = state && !state->isBallGrounded;
    if (input.airborne && ballBody) {
      input.fallSpeed = std::max(-ballBody->velocity.y, 0.0f);
    }
    if (previous.airborne && !input.airborne && onGrass &&
        previous.fallSpeed >= kGrassLandingMinFallSpeed) {
      GrassImpulse impulse;
      impulse.kind = GrassImpulseKind::Landing;
      impulse.position = ballTransform->position;
      impulse.strength = GrassLandingStrength(previous.fallSpeed);
      AddGrassImpulse(ctx, impulse);
    }
  }
  ctx.world.SetGlobal(std::move(input));
}

} // namespace game::systems
