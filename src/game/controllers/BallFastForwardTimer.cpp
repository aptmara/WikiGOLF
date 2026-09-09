/**
 * @file BallFastForwardTimer.cpp
 * @brief BallFastForwardTimerの実装
*/

#include "BallFastForwardTimer.h"
#include "../../ecs/World.h"
#include "../components/WikiComponents.h"

namespace game::controllers {

using game::components::GolfGameState;
using game::components::ShotState;
using game::utils::FastForwardTier;

void BallFastForwardTimer::Reset() {
  m_hasLanded = false;
  m_secondsSinceLanding = 0.0f;
  m_currentTier = FastForwardTier::Normal;
}

float BallFastForwardTimer::Update(core::GameContext &ctx, float dt) {
  auto *shot = ctx.world.GetGlobal<ShotState>();
  auto *state = ctx.world.GetGlobal<GolfGameState>();

  if (!shot || !state || shot->phase != ShotState::Phase::Executing) {
    // ショット実行中でなければ倍速演出の対象外。次のショットの着地に
    // 備えて毎フレームリセットしておく。
    Reset();
    return 1.0f;
  }

  if (!m_hasLanded) {
    if (state->isBallGrounded) {
      m_hasLanded = true;
      m_secondsSinceLanding = 0.0f;
    }
  } else {
    m_secondsSinceLanding += dt;
  }

  m_currentTier = game::utils::ResolveFastForwardTier(m_secondsSinceLanding);
  return game::utils::GetFastForwardSpeedMultiplier(m_currentTier);
}

} // namespace game::controllers
