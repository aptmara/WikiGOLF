/**
 * @file PhysicsSystemAudio.cpp
 * @brief 物理更新に連動する走行音の制御
*/

#include "PhysicsSystemInternals.h"
#include "../../audio/AudioSystem.h"

namespace game::systems {

using namespace game::components;

void UpdateRollingAudio(PhysicsUpdateContext &frame, const BodyInfo &body,
                        bool isGrounded, float speed, uint8_t material,
                        int step) {
  auto &ctx = frame.gameContext;
  if (!ctx.audio || body.entity != frame.ballEntity ||
      step != frame.subSteps - 1 ||
      frame.rollingAudioTimer < (1.0f / 30.0f)) {
    return;
  }

  if (!isGrounded || speed <= 0.5f) {
    ctx.audio->SetLoopingSE(ctx, "BallRoll", "", 0.0f);
    return;
  }

  std::string seName = "se_Fairway";
  float pitchBase = 0.0f;
  switch (static_cast<TerrainMaterial>(material)) {
  case TerrainMaterial::Bunker:
    seName = "se_Bunker";
    pitchBase = -0.2f;
    break;
  case TerrainMaterial::Rough:
    seName = "se_Rough";
    pitchBase = -0.1f;
    break;
  case TerrainMaterial::Green:
    seName = "se_Fairway";
    pitchBase = 0.1f;
    break;
  default:
    break;
  }

  float volume = std::clamp(speed / 20.0f, 0.0f, 1.0f) * 0.4f;
  float pitch = pitchBase + speed / 40.0f;
  ctx.audio->SetLoopingSE(ctx, "BallRoll", seName, volume, pitch);
}

} // namespace game::systems
