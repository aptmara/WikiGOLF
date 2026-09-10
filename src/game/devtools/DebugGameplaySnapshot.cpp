#include "DebugGameplaySnapshot.h"

#include "../../ecs/World.h"
#include "../components/PhysicsComponents.h"
#include "../components/Transform.h"
#include "../components/WikiComponents.h"
#include <cmath>

namespace game::debug {
namespace {

using namespace game::components;

const char *TerrainMaterialName(TerrainMaterial material) {
  switch (material) {
  case TerrainMaterial::Fairway: return "フェアウェイ";
  case TerrainMaterial::Rough: return "ラフ";
  case TerrainMaterial::Bunker: return "バンカー";
  case TerrainMaterial::Green: return "グリーン";
  case TerrainMaterial::Ice: return "氷";
  case TerrainMaterial::Water: return "水";
  case TerrainMaterial::Lava: return "溶岩";
  case TerrainMaterial::Stone: return "石";
  case TerrainMaterial::None: return "なし";
  }
  return "不明";
}

const char *ShotPhaseName(ShotState::Phase phase) {
  switch (phase) {
  case ShotState::Phase::Idle: return "待機";
  case ShotState::Phase::PowerCharging: return "パワー入力";
  case ShotState::Phase::ImpactTiming: return "インパクト入力";
  case ShotState::Phase::Executing: return "ショット実行";
  case ShotState::Phase::ShowResult: return "結果表示";
  case ShotState::Phase::RestoringCamera: return "カメラ復帰";
  }
  return "不明";
}

const char *ShotJudgementName(ShotJudgement judgement) {
  switch (judgement) {
  case ShotJudgement::None: return "未判定";
  case ShotJudgement::Special: return "SPECIAL";
  case ShotJudgement::Great: return "GREAT";
  case ShotJudgement::Nice: return "NICE";
  case ShotJudgement::Miss: return "MISS";
  }
  return "不明";
}

} // namespace

DebugGameplaySnapshot CaptureGameplaySnapshot(ecs::World &world) {
  DebugGameplaySnapshot result;
  auto *golf = world.GetGlobal<GolfGameState>();
  if (golf) {
    result.golf = {true,
                   golf->currentPage,
                   golf->targetPage,
                   TerrainMaterialName(golf->currentMaterial),
                   golf->windDirection,
                   golf->lastShotPosition,
                   golf->windSpeed,
                   golf->currentBallSpeed,
                   golf->shotCount,
                   golf->par,
                   golf->moveCount,
                   golf->isBallGrounded,
                   golf->isOB,
                   golf->canShoot,
                   golf->isMapView,
                   golf->gameCleared};
  }

  if (auto *shot = world.GetGlobal<ShotState>()) {
    result.shot = {true,
                   ShotPhaseName(shot->phase),
                   ShotJudgementName(shot->judgement),
                   shot->powerGaugePos,
                   shot->powerGaugeDir,
                   shot->confirmedPower,
                   shot->impactGaugePos,
                   shot->impactPerfectCenter,
                   shot->confirmedImpact,
                   shot->maxPower,
                   shot->resultDisplayTime};
  }

  if (!golf) {
    return result;
  }
  const ecs::Entity ballEntity = static_cast<ecs::Entity>(golf->ballEntity);
  auto *transform = world.Get<Transform>(ballEntity);
  auto *body = world.Get<RigidBody>(ballEntity);
  if (!transform || !body) {
    return result;
  }
  const float speed = std::sqrt(body->velocity.x * body->velocity.x +
                                body->velocity.y * body->velocity.y +
                                body->velocity.z * body->velocity.z);
  result.ball = {true,
                 golf->ballEntity,
                 transform->position,
                 body->velocity,
                 body->acceleration,
                 body->angularVelocity,
                 speed,
                 body->mass,
                 body->drag,
                 body->rollingFriction,
                 body->restitution,
                 body->spinDecay};
  return result;
}

} // namespace game::debug
