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
    t.scale = {0.08f, 0.08f, 0.08f};

    auto &mr = ctx.world.Add<MeshRenderer>(e);
    mr.mesh = ctx.resource.LoadMesh("builtin/sphere");
    mr.shader = ctx.resource.LoadShader("Trail", L"shaders/TrailVS.hlsl",
                                        L"shaders/ParticlePS.hlsl");
    mr.isTransparent = true;
    mr.blendMode = BlendMode::Add;

    // 白い先端からシアン、青紫へ移る一本の彗星状トレイル。
    float ratio = (float)i / (float)(kTrailCount - 1);
    const float middleBlend = std::clamp(ratio * 2.0f, 0.0f, 1.0f);
    const float tailBlend = std::clamp((ratio - 0.48f) * 1.92f, 0.0f, 1.0f);
    float r = 1.45f + (0.22f - 1.45f) * middleBlend;
    float g = 1.58f + (1.22f - 1.58f) * middleBlend;
    float b = 1.62f + (1.72f - 1.62f) * middleBlend;
    r += (0.50f - r) * tailBlend;
    g += (0.36f - g) * tailBlend;
    b += (1.45f - b) * tailBlend;
    const float alpha = 1.0f * std::pow(1.0f - ratio, 0.66f);
    mr.color = {r * 1.18f, g * 1.18f, b * 1.18f, alpha};
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

  // 飛行中だけ、全ショット共通のトレイルを表示する。
  auto *rb = ctx.world.Get<components::RigidBody>(targetEntity);
  auto *gameState = ctx.world.GetGlobal<components::GolfGameState>();
  bool isMoving = false;
  if (rb) {
    float speed = std::sqrt(rb->velocity.x * rb->velocity.x +
                            rb->velocity.y * rb->velocity.y +
                            rb->velocity.z * rb->velocity.z);
    isMoving = speed > 0.5f;
  }

  const bool isAirborne = !gameState || !gameState->isBallGrounded;
  if (!isMoving || !isAirborne) {
    if (!isAirborne) {
      for (auto &position : m_trailPositions) {
        position = targetT->position;
      }
      m_trailWriteIndex = 0;
    }
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
    const int olderHistoryIndex =
        (m_trailWriteIndex - i - 1 + kTrailCount) % kTrailCount;
    const DirectX::XMFLOAT3 olderPos = m_trailPositions[olderHistoryIndex];

    auto e = m_trailEntities[i];
    auto *t = ctx.world.Get<Transform>(e);
    auto *mr = ctx.world.Get<MeshRenderer>(e);

    if (t && mr) {
      // 現在位置を最大・不透明にし、古い履歴ほど小さく透明にする。
      float ratio = (float)i / (float)(kTrailCount - 1);
      float fade = std::pow(1.0f - ratio, 1.35f);
      float sizeEase = 0.32f + 0.86f * std::pow(1.0f - ratio, 2.1f);
      float pulse = 0.96f + std::sin(ratio * 17.0f + speed * 0.08f) * 0.04f;
      float scaleBase = (0.068f + speedNormalized * 0.058f) * sizeEase * pulse;
      const float segmentX = pos.x - olderPos.x;
      const float segmentY = pos.y - olderPos.y;
      const float segmentZ = pos.z - olderPos.z;
      const float segmentLength = std::sqrt(segmentX * segmentX +
                                            segmentY * segmentY +
                                            segmentZ * segmentZ);
      if (olderPos.y > -50.0f && segmentLength > 0.001f) {
        t->position = {(pos.x + olderPos.x) * 0.5f,
                       (pos.y + olderPos.y) * 0.5f,
                       (pos.z + olderPos.z) * 0.5f};
        const float horizontalLength =
            std::sqrt(segmentX * segmentX + segmentZ * segmentZ);
        const float yaw = std::atan2(segmentX, segmentZ);
        const float pitch = -std::atan2(segmentY, horizontalLength);
        XMStoreFloat4(&t->rotation,
                      XMQuaternionRotationRollPitchYaw(pitch, yaw, 0.0f));
        t->scale = {scaleBase, scaleBase,
                    std::max(scaleBase, segmentLength * 0.56f)};
      } else {
        t->position = pos;
        t->scale = {scaleBase, scaleBase, scaleBase};
      }

      DirectX::XMFLOAT4 baseColor = m_trailBaseColors[i];
      const float brightness = 0.96f + speedNormalized * 0.44f;
      mr->color.x = baseColor.x * brightness;
      mr->color.y = baseColor.y * brightness;
      mr->color.z = baseColor.z * brightness;
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
