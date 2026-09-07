/**
 * @file GameJuiceSystemTrail.cpp
 * @brief Game Juiceの責務別実装です。
 */

#include "GameJuiceSystem.h"
#include "GameJuiceVisualRules.h"
#include "../utils/GameplayPhysicsConstants.h"
#include "../../core/GameContext.h"
#include "../../core/Logger.h"
#include "../../ecs/World.h"
#include "../components/Camera.h"
#include "../components/MeshRenderer.h"
#include "../components/PhysicsComponents.h"
#include "../components/Transform.h"
#include "../components/WikiComponents.h"
#include "PhysicsFriction.h"
#include <algorithm>
#include <cmath>
#include <windows.h>

#undef min
#undef max

namespace game::systems {

using namespace DirectX;
using namespace game::components;
using namespace juice_detail;

void GameJuiceSystem::CreateTrailEntities(core::GameContext &ctx) {
  m_trailEntities.clear();
  m_trailPositions.clear();
  m_trailBaseColors.clear();
  m_trailPositions.resize(kTrailCount, {0, -100, 0}); // 画面外で初期化
  m_trailBaseColors.resize(kTrailCount);

  for (int i = 0; i < kTrailCount; ++i) {
    auto e = m_entityOwner.Create(ctx.world);

    auto &t = ctx.world.Add<Transform>(e);
    t.position = {0, -100, 0};     // 画面外
    t.scale = {0.08f, 0.08f, 0.08f}; // 大きめ

    auto &mr = ctx.world.Add<MeshRenderer>(e);
    mr.mesh = ctx.resource.LoadMesh("builtin/sphere");
    mr.shader = ctx.resource.LoadShader("Trail", L"shaders/TrailVS.hlsl",
                                        L"shaders/ParticlePS.hlsl");
    mr.isTransparent = true;

    // 虹色グラデーション（派手に）
    float ratio = (float)i / (float)(kTrailCount - 1);
    float hue = ratio * 360.0f; // 0-360度
    float h = hue / 60.0f;
    int hi = (int)h % 6;
    float f = h - (int)h;
    float r, g, b;
    switch (hi) {
    case 0:
      r = 1.0f;
      g = f;
      b = 0.0f;
      break;
    case 1:
      r = 1.0f - f;
      g = 1.0f;
      b = 0.0f;
      break;
    case 2:
      r = 0.0f;
      g = 1.0f;
      b = f;
      break;
    case 3:
      r = 0.0f;
      g = 1.0f - f;
      b = 1.0f;
      break;
    case 4:
      r = f;
      g = 0.0f;
      b = 1.0f;
      break;
    default:
      r = 1.0f;
      g = 0.0f;
      b = 1.0f - f;
      break;
    }
    float alpha = 0.95f * (1.0f - ratio * 0.6f);
    mr.color = {r * 1.5f, g * 1.5f, b * 1.5f, alpha}; // 発光感
    m_trailBaseColors[i] = mr.color;
    mr.isVisible = false;

    m_trailEntities.push_back(e);
  }

  m_trailWriteIndex = 0;
  m_trailUpdateTimer = 0.0f;
}

void GameJuiceSystem::ResetTrail() {
  m_trailWriteIndex = 0;
  m_trailUpdateTimer = 0.0f;
  m_hasLastTrailTargetPosition = false;
  for (auto &pos : m_trailPositions) {
    pos = {0, -100, 0};
  }
}

void GameJuiceSystem::UpdateTrail(core::GameContext &ctx,
                                  ecs::Entity targetEntity) {
  if (!ctx.world.IsAlive(targetEntity))
    return;

  auto *targetT = ctx.world.Get<Transform>(targetEntity);
  if (!targetT)
    return;

  const float effectDt =
      std::min(ctx.dt, game::physics::kMaxSimulationDeltaTime);
  if (!m_hasLastTrailTargetPosition) {
    m_lastTrailTargetPosition = targetT->position;
    m_hasLastTrailTargetPosition = true;
  }

  // ボールが動いているかチェック
  auto *rb = ctx.world.Get<components::RigidBody>(targetEntity);
  bool isMoving = false;
  if (rb) {
    float speed = std::sqrt(rb->velocity.x * rb->velocity.x +
                            rb->velocity.y * rb->velocity.y +
                            rb->velocity.z * rb->velocity.z);
    isMoving = speed > 0.5f;
  }

  // 動いていない場合はトレイルを非表示
  if (!isMoving) {
    for (auto e : m_trailEntities) {
      auto *mr = ctx.world.Get<MeshRenderer>(e);
      if (mr) {
        mr->color.w *= 0.85f;
        if (mr->color.w < 0.02f) {
          mr->isVisible = false;
        }
      }
    }
    m_trailUpdateTimer = 0.0f;
    m_lastTrailTargetPosition = targetT->position;
    return;
  }

  float speed =
      std::sqrt(rb->velocity.x * rb->velocity.x + rb->velocity.y * rb->velocity.y +
                rb->velocity.z * rb->velocity.z);
  float speedNormalized = std::clamp(speed / 35.0f, 0.0f, 1.0f);
  auto *shotState = ctx.world.GetGlobal<components::ShotState>();
  components::ShotJudgement judgement = components::ShotJudgement::None;
  if (shotState) {
    judgement = shotState->judgement;
  }

  const float moveX = targetT->position.x - m_lastTrailTargetPosition.x;
  const float moveY = targetT->position.y - m_lastTrailTargetPosition.y;
  const float moveZ = targetT->position.z - m_lastTrailTargetPosition.z;
  const float frameDistance =
      std::sqrt(moveX * moveX + moveY * moveY + moveZ * moveZ);
  float measuredSpeed = 0.0f;
  if (effectDt > 0.0f) {
    measuredSpeed = frameDistance / effectDt;
  }
  float spatialInterval = kTrailUpdateInterval;
  if (measuredSpeed > 0.001f) {
    spatialInterval = kTrailMaxSpacing / measuredSpeed;
  }
  const float sampleInterval =
      std::clamp(std::min(kTrailUpdateInterval, spatialInterval),
                 effectDt / static_cast<float>(kTrailCount),
                 kTrailUpdateInterval);

  // フレーム内の移動区間を補間し、描画FPSと速度に依存しない密度で位置を記録する。
  const float elapsedBeforeFrame =
      std::min(m_trailUpdateTimer, sampleInterval);
  const float accumulated = elapsedBeforeFrame + effectDt;
  float sampleTime = sampleInterval - elapsedBeforeFrame;
  while (sampleTime <= effectDt + 1e-6f) {
    float ratio = 1.0f;
    if (effectDt > 0.0f) {
      ratio = std::clamp(sampleTime / effectDt, 0.0f, 1.0f);
    }
    DirectX::XMFLOAT3 samplePosition = {
        m_lastTrailTargetPosition.x +
            (targetT->position.x - m_lastTrailTargetPosition.x) * ratio,
        m_lastTrailTargetPosition.y +
            (targetT->position.y - m_lastTrailTargetPosition.y) * ratio,
        m_lastTrailTargetPosition.z +
            (targetT->position.z - m_lastTrailTargetPosition.z) * ratio};
    m_trailPositions[m_trailWriteIndex] = samplePosition;
    m_trailWriteIndex = (m_trailWriteIndex + 1) % kTrailCount;
    sampleTime += sampleInterval;
  }
  m_trailUpdateTimer = std::fmod(accumulated, sampleInterval);
  m_lastTrailTargetPosition = targetT->position;

  // 先端は必ず現在のボール中心へ置き、残りを新しい履歴から過去方向へ並べる。
  // m_trailWriteIndex は次の書き込み先なので、直前の履歴は writeIndex - 1。
  for (int i = 0; i < kTrailCount; ++i) {
    const int historyIndex =
        (m_trailWriteIndex - i + kTrailCount) % kTrailCount;
    DirectX::XMFLOAT3 pos = m_trailPositions[historyIndex];
    if (i == 0) {
      pos = targetT->position;
    }

    auto e = m_trailEntities[i];
    auto *t = ctx.world.Get<Transform>(e);
    auto *mr = ctx.world.Get<MeshRenderer>(e);

    if (t && mr) {
      t->position = pos;

      // 現在位置を最大・不透明にし、古い履歴ほど小さく透明にする。
      float ratio = (float)i / (float)(kTrailCount - 1);
      float fade = std::pow(1.0f - ratio, 1.5f);
      float sizeEase = 0.65f + 0.45f * std::pow(1.0f - ratio, 2.3f);
      float scaleBase = (0.07f + speedNormalized * 0.05f) * sizeEase;
      t->scale = {scaleBase, scaleBase, scaleBase};

      DirectX::XMFLOAT4 baseColor = m_trailBaseColors[i];
      DirectX::XMFLOAT3 tint = {1.0f, 0.5f + speedNormalized * 0.5f, 0.15f};
      if (judgement == components::ShotJudgement::Great) {
        tint = {1.2f, 1.0f, 0.35f};
      } else if (judgement == components::ShotJudgement::Nice) {
        tint = {0.4f, 0.9f, 1.2f};
      } else if (judgement == components::ShotJudgement::Miss) {
        tint = {0.9f, 0.3f, 0.3f};
      }

      float mix = 0.25f + speedNormalized * 0.55f;
      mr->color.x = baseColor.x * (1.0f - mix) + tint.x * mix;
      mr->color.y = baseColor.y * (1.0f - mix) + tint.y * mix;
      mr->color.z = baseColor.z * (1.0f - mix) + tint.z * mix;
      mr->color.w = baseColor.w * fade;

      // 画面外でなければ表示
      mr->isVisible = (pos.y > -50.0f);
    }
  }
}

// =============================================================================
// インパクトエフェクト
// =============================================================================

} // namespace game::systems
