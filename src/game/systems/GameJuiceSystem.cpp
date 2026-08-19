/**
 * @file GameJuiceSystem.cpp
 * @brief ゲームの演出効果（Game Juice）システム実装
 */

#include "GameJuiceSystem.h"
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

// Windowsマクロ対策
#include <windows.h> // 必要なら
#undef min
#undef max

namespace game::systems {

using namespace DirectX;
using namespace game::components;

/**
 * @brief 0.0〜1.0の乱数を返します。
 */
static float Rand01() { return static_cast<float>(rand() % 100) / 100.0f; }

/**
 * @brief -0.5〜0.5の乱数を返します。
 */
static float RandCentered() { return Rand01() - 0.5f; }

/**
 * @brief 始点から終点へ向かうなめらかな減衰率を返します。
 */
static float SmoothFade(float value, float start, float end) {
  float t = std::clamp((value - start) / (end - start), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

/**
 * @brief 色を指定倍率で明るくします。
 */
static XMFLOAT4 ScaleColor(const XMFLOAT3 &color, float brightness,
                           float alpha = 1.0f) {
  return {color.x * brightness, color.y * brightness, color.z * brightness,
          alpha};
}

/**
 * @brief カップイン祝祭粒子用の色を返します。
 */
static XMFLOAT3 CupInSparkleColor(int index) {
  static const XMFLOAT3 kPalette[] = {
      {1.0f, 0.78f, 0.16f}, {1.0f, 0.94f, 0.45f},
      {1.0f, 1.0f, 0.82f},  {1.0f, 0.62f, 0.08f},
      {0.55f, 0.9f, 1.0f},  {1.0f, 0.86f, 0.24f},
  };
  return kPalette[index % (sizeof(kPalette) / sizeof(kPalette[0]))];
}

/**
 * @brief ショット判定に対応した発光色を返します。
 */
static XMFLOAT3 ShotJudgeColor(GameJuiceSystem::JudgeType judge, int index) {
  switch (judge) {
  case GameJuiceSystem::JudgeType::Special: {
    static const XMFLOAT3 kSpecial[] = {
        {1.0f, 0.92f, 0.28f}, {0.45f, 0.95f, 1.0f},
        {1.0f, 0.55f, 1.0f},  {0.85f, 1.0f, 0.45f},
    };
    return kSpecial[index % (sizeof(kSpecial) / sizeof(kSpecial[0]))];
  }
  case GameJuiceSystem::JudgeType::Great:
    return {1.0f, 0.82f, 0.18f};
  case GameJuiceSystem::JudgeType::Nice:
    return {0.35f, 0.78f, 1.0f};
  case GameJuiceSystem::JudgeType::Miss:
    return {1.0f, 0.28f, 0.10f};
  default:
    return {1.0f, 0.86f, 0.42f};
  }
}

void GameJuiceSystem::Initialize(core::GameContext &ctx) {
  LOG_INFO("GameJuice", "Initializing Game Juice System...");

  // トレイルエンティティ作成
  CreateTrailEntities(ctx);

  // インパクトパーティクルエンティティ作成
  CreateImpactParticleEntities(ctx);

  // 環境パーティクルエンティティ作成
  CreateEnvironmentParticleEntities(ctx);

  // バンカーの窪みとボール周囲の砂
  CreateSandSurfaceEntities(ctx);

  // リップルエフェクト
  CreateRippleEntities(ctx);

  // FOV初期化
  m_baseFov = 60.0f;
  m_currentFov = m_baseFov;
  m_targetFov = m_baseFov;

  LOG_INFO("GameJuice", "Game Juice System initialized.");
}

void GameJuiceSystem::Update(core::GameContext &ctx, ecs::Entity cameraEntity,
                             ecs::Entity targetEntity) {
  // カメラシェイク更新
  UpdateCameraShake(ctx, cameraEntity);

  // FOV更新
  UpdateFov(ctx, cameraEntity);

  // トレイル更新
  UpdateTrail(ctx, targetEntity);

  // インパクトパーティクル更新
  UpdateImpactParticles(ctx);

  // 環境パーティクル更新
  EmitEnvironmentParticles(ctx, targetEntity);
  UpdateEnvironmentParticles(ctx, targetEntity);
  UpdateSandSurfaceEffects(ctx, targetEntity);

  // リップル更新
  UpdateRipples(ctx);
}

// =============================================================================
// タイムコントロール
// =============================================================================

void GameJuiceSystem::TriggerHitStop(float duration, float timeScale) {
  m_hitStopDuration = std::max(0.0f, duration);
  m_hitStopTimer = m_hitStopDuration;
  m_hitStopScale = std::clamp(timeScale, 0.0f, 1.0f);
}

void GameJuiceSystem::TriggerSlowMotion(float duration, float scale) {
  m_slowMoDuration = std::max(0.0f, duration);
  m_slowMoTimer = std::max(m_slowMoTimer, m_slowMoDuration);
  m_slowMoScale = std::clamp(scale, 0.05f, 1.0f);
}

float GameJuiceSystem::ConsumeTimeScale(float unscaledDt) {
  // ヒットストップ優先
  float activeScale = 1.0f;
  if (m_hitStopTimer > 0.0f) {
    m_hitStopTimer = std::max(0.0f, m_hitStopTimer - unscaledDt);
    activeScale = m_hitStopScale;
  }

  if (m_slowMoTimer > 0.0f && m_slowMoDuration > 0.0f) {
    m_slowMoTimer = std::max(0.0f, m_slowMoTimer - unscaledDt);
    float progress = 1.0f - (m_slowMoTimer / m_slowMoDuration);
    float easeOut = 1.0f - std::pow(std::clamp(progress, 0.0f, 1.0f), 2.0f);
    float slowScale = m_slowMoScale + (1.0f - m_slowMoScale) * easeOut;
    activeScale = std::min(activeScale, slowScale);
  }

  m_timeScale = std::clamp(activeScale, 0.0f, 1.0f);
  return m_timeScale;
}

// =============================================================================
// カメラシェイク
// =============================================================================

void GameJuiceSystem::TriggerCameraShake(float intensity, float duration) {
  m_shakeIntensity = intensity;
  m_shakeDuration = duration;
  m_shakeTimer = 0.0f;
  LOG_DEBUG("GameJuice",
            "Camera shake triggered: intensity={:.2f}, duration={:.2f}",
            intensity, duration);
}

void GameJuiceSystem::UpdateCameraShake(core::GameContext &ctx,
                                        ecs::Entity cameraEntity) {
  if (m_shakeDuration <= 0.0f)
    return;

  if (!ctx.world.IsAlive(cameraEntity))
    return;

  auto *camT = ctx.world.Get<Transform>(cameraEntity);
  if (!camT)
    return;

  m_shakeTimer += ctx.dt;
  m_shakeDuration -= ctx.dt;

  if (m_shakeDuration <= 0.0f) {
    m_shakeDuration = 0.0f;
    m_shakeIntensity = 0.0f;
    return;
  }

  // 減衰計算（時間とともに弱くなる）
  float decay = m_shakeDuration / (m_shakeDuration + ctx.dt * 2.0f);
  float currentIntensity = m_shakeIntensity * decay;

  // Perlinノイズ風に複数周波数を重ね合わせ
  float t = m_shakeTimer;
  float offsetX = std::sin(t * m_shakeFrequency) * 0.5f +
                  std::sin(t * m_shakeFrequency * 2.3f) * 0.3f +
                  std::sin(t * m_shakeFrequency * 5.7f) * 0.2f;
  float offsetY = std::cos(t * m_shakeFrequency * 1.1f) * 0.5f +
                  std::cos(t * m_shakeFrequency * 3.1f) * 0.3f +
                  std::cos(t * m_shakeFrequency * 4.3f) * 0.2f;

  // カメラ位置にオフセット適用（一時的）
  camT->position.x += offsetX * currentIntensity;
  camT->position.y += offsetY * currentIntensity * 0.5f; // Y方向は控えめ
}

// =============================================================================
// FOV変化
// =============================================================================

void GameJuiceSystem::SetTargetFov(float fov) {
  m_targetFov = std::clamp(fov, 30.0f, 120.0f);
}

void GameJuiceSystem::ResetFov() { m_targetFov = m_baseFov; }

void GameJuiceSystem::UpdateFov(core::GameContext &ctx,
                                ecs::Entity cameraEntity) {
  // 滑らかに補間
  float speed = 8.0f;
  float punchOffset = UpdateFovPunch(ctx.dt);
  float desiredFov = m_targetFov - punchOffset;
  m_currentFov += (desiredFov - m_currentFov) * speed * ctx.dt;

  if (!ctx.world.IsAlive(cameraEntity))
    return;

  auto *cam = ctx.world.Get<Camera>(cameraEntity);
  if (cam) {
    cam->fov = XMConvertToRadians(m_currentFov);
  }
}

float GameJuiceSystem::UpdateFovPunch(float dt) {
  if (m_fovPunchTimer <= 0.0f || m_fovPunchDuration <= 0.0f ||
      m_fovPunchStrength <= 0.0f) {
    m_fovPunchTimer = 0.0f;
    return 0.0f;
  }

  m_fovPunchTimer = std::max(0.0f, m_fovPunchTimer - dt);
  float progress = 1.0f - (m_fovPunchTimer / m_fovPunchDuration);
  float eased = std::sin(std::clamp(progress, 0.0f, 1.0f) * XM_PI);
  return m_fovPunchStrength * eased;
}

// =============================================================================
// トレイル
// =============================================================================

void GameJuiceSystem::CreateTrailEntities(core::GameContext &ctx) {
  m_trailEntities.clear();
  m_trailPositions.clear();
  m_trailBaseColors.clear();
  m_trailPositions.resize(kTrailCount, {0, -100, 0}); // 画面外で初期化
  m_trailBaseColors.resize(kTrailCount);

  for (int i = 0; i < kTrailCount; ++i) {
    auto e = ctx.world.CreateEntity();

    auto &t = ctx.world.Add<Transform>(e);
    t.position = {0, -100, 0};     // 画面外
    t.scale = {0.08f, 0.08f, 0.08f}; // 大きめ

    auto &mr = ctx.world.Add<MeshRenderer>(e);
    mr.mesh = ctx.resource.LoadMesh("builtin/sphere");
    mr.shader = ctx.resource.LoadShader("Particle", L"shaders/ParticleVS.hlsl",
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
    return;
  }

  float speed =
      std::sqrt(rb->velocity.x * rb->velocity.x + rb->velocity.y * rb->velocity.y +
                rb->velocity.z * rb->velocity.z);
  float speedNormalized = std::clamp(speed / 35.0f, 0.0f, 1.0f);
  auto *shotState = ctx.world.GetGlobal<components::ShotState>();
  components::ShotJudgement judgement =
      shotState ? shotState->judgement : components::ShotJudgement::None;

  // 一定間隔で位置を記録
  m_trailUpdateTimer += ctx.dt;
  if (m_trailUpdateTimer >= kTrailUpdateInterval) {
    m_trailUpdateTimer = 0.0f;

    // リングバッファに書き込み
    m_trailPositions[m_trailWriteIndex] = targetT->position;
    m_trailWriteIndex = (m_trailWriteIndex + 1) % kTrailCount;
  }

  // トレイルエンティティの位置とスケールを更新
  for (int i = 0; i < kTrailCount; ++i) {
    // リングバッファのインデックス計算（古い順）
    int posIndex = (m_trailWriteIndex + i) % kTrailCount;
    auto &pos = m_trailPositions[posIndex];

    auto e = m_trailEntities[i];
    auto *t = ctx.world.Get<Transform>(e);
    auto *mr = ctx.world.Get<MeshRenderer>(e);

    if (t && mr) {
      t->position = pos;

      // 古いほど小さく
      float ratio = (float)i / (float)(kTrailCount - 1);
      float fade = std::pow(1.0f - ratio, 1.5f);
      float sizeEase = 0.65f + 0.45f * std::pow(1.0f - ratio, 2.3f);
      float scaleBase = (0.07f + speedNormalized * 0.05f) * sizeEase;
      t->scale = {scaleBase, scaleBase, scaleBase};

      DirectX::XMFLOAT4 baseColor = m_trailBaseColors[posIndex];
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

void GameJuiceSystem::CreateImpactParticleEntities(core::GameContext &ctx) {
  m_impactParticles.clear();
  m_impactParticles.reserve(kImpactParticleCount);

  for (int i = 0; i < kImpactParticleCount; ++i) {
    auto e = ctx.world.CreateEntity();

    auto &t = ctx.world.Add<Transform>(e);
    t.position = {0, -100, 0}; // 画面外
    t.scale = {0.1f, 0.1f, 0.1f};

    auto &mr = ctx.world.Add<MeshRenderer>(e);
    mr.mesh = ctx.resource.LoadMesh("builtin/cube");
    mr.shader = ctx.resource.LoadShader("Particle", L"shaders/ParticleVS.hlsl",
                                        L"shaders/ParticlePS.hlsl");
    mr.color = {1.0f, 0.8f, 0.2f, 1.0f}; // 黄金色
    mr.isVisible = false;
    mr.isTransparent = true;
    mr.blendMode = BlendMode::Alpha;
    mr.customFlags = {0, 0, 0, 0};

    ImpactParticle particle;
    particle.entity = e;
    particle.lifetime = 0.0f;
    particle.kind = ImpactParticleKind::Burst;
    m_impactParticles.push_back(particle);
  }
}

void GameJuiceSystem::TriggerImpactEffect(core::GameContext &ctx,
                                          const DirectX::XMFLOAT3 &position,
                                          float power, JudgeType judge) {
  LOG_DEBUG("GameJuice",
            "Impact effect triggered at ({:.2f}, {:.2f}, {:.2f}) power={:.2f} "
            "judge={}",
            position.x, position.y, position.z, power, (int)judge);

  // 演出テンポ: ヒットストップ + FOVパンチ
  TriggerHitStop(0.06f + power * 0.04f);
  m_fovPunchStrength = 8.0f + power * 6.0f;
  m_fovPunchDuration = 0.18f;
  m_fovPunchTimer = m_fovPunchDuration;

  // 判定によって派手さを調整
  float speedMultiplier = 1.0f;
  float sizeMultiplier = 1.0f;
  float lifetimeMultiplier = 1.0f;

  switch (judge) {
  case JudgeType::Great:
    speedMultiplier = 1.8f;    // より高速に飛び散る
    sizeMultiplier = 1.6f;     // 大きめ
    lifetimeMultiplier = 1.35f; // 長寿命
    break;
  case JudgeType::Special:
    speedMultiplier = 2.0f;
    sizeMultiplier = 1.8f;
    lifetimeMultiplier = 1.45f;
    break;
  case JudgeType::Nice:
    speedMultiplier = 1.2f;
    sizeMultiplier = 1.2f;
    lifetimeMultiplier = 1.1f;
    break;
  case JudgeType::Miss:
    speedMultiplier = 0.7f; // 弱め
    sizeMultiplier = 0.8f;
    lifetimeMultiplier = 0.8f;
    break;
  default:
    break;
  }

  float baseSpeed = (8.0f + power * 15.0f) * speedMultiplier;
  float spreadFactor = 1.0f + power * 0.5f;

  for (int i = 0; i < kImpactBurstCount; ++i) {
    auto &p = m_impactParticles[i];

    // === 多層構造の爆発エフェクト ===
    int layer = i % 4; // 4層構造
    float layerOffset = layer * 0.25f;
    float layerSpeed = baseSpeed * (1.0f - layerOffset * 0.3f);

    // 放射状に速度を設定（スパイラル風）
    float baseAngle = (float)i / (float)kImpactBurstCount * XM_2PI;
    float spiralOffset = (float)layer * 0.3f;
    float angle = baseAngle + spiralOffset;

    // 上向きのばらつき（花火風に上に多く）
    float upAngle =
        XM_PIDIV4 * (1.0f + ((float)(rand() % 100) / 100.0f) * 1.5f);
    if (layer == 0)
      upAngle *= 1.3f;

    p.velocity.x =
        std::cos(angle) * std::cos(upAngle) * layerSpeed * spreadFactor;
    p.velocity.y = std::sin(upAngle) * layerSpeed * 1.2f;
    p.velocity.z =
        std::sin(angle) * std::cos(upAngle) * layerSpeed * spreadFactor;

    // ランダムなばらつき
    p.velocity.x += ((float)(rand() % 100) / 100.0f - 0.5f) * 5.0f;
    p.velocity.y += ((float)(rand() % 100) / 100.0f) * 3.0f;
    p.velocity.z += ((float)(rand() % 100) / 100.0f - 0.5f) * 5.0f;

    // 寿命
    p.lifetime =
        (0.6f + ((float)(rand() % 100) / 100.0f) * 0.5f + layer * 0.15f) *
        lifetimeMultiplier;
    p.maxLifetime = p.lifetime;

    // 初期位置設定
    auto *t = ctx.world.Get<Transform>(p.entity);
    if (t) {
      t->position = position;
      t->position.x += ((float)(rand() % 100) / 100.0f - 0.5f) * 0.3f;
      t->position.y += 0.1f + ((float)(rand() % 100) / 100.0f) * 0.2f;
      t->position.z += ((float)(rand() % 100) / 100.0f - 0.5f) * 0.3f;

      float baseScale = (0.15f + power * 0.1f) * sizeMultiplier;
      float scaleVar = 0.8f + ((float)(rand() % 100) / 100.0f) * 0.4f;
      float scale = baseScale * scaleVar;
      t->scale = {scale, scale, scale};
      p.baseScale = scale;
    }

    auto *mr = ctx.world.Get<MeshRenderer>(p.entity);
    if (mr) {
      mr->isVisible = true;
      mr->mesh = ctx.resource.LoadMesh("builtin/cube");
      mr->blendMode = BlendMode::Alpha;
      mr->customFlags = {0, 0, 0, 0};
      p.kind = ImpactParticleKind::Burst;
      p.angularVelocity = {0, 0, 0};

      float r, g, b;

      // === 判定ごとの色設定 ===
      switch (judge) {
      case JudgeType::Great:
        // 金色～白の豪華な爆発
        {
          float goldShift = (float)(rand() % 100) / 100.0f * 0.3f;
          r = 1.0f;
          g = 0.8f + goldShift;
          b = 0.2f + goldShift * 0.5f;
          if (layer == 0) {
            r = 1.0f;
            g = 1.0f;
            b = 0.9f;
          }
        }
        break;
      case JudgeType::Special:
        // 虹色に近い強調
        {
          float hue = ((float)(rand() % 360)) * (XM_PI / 180.0f);
          r = 0.6f + std::sin(hue) * 0.4f;
          g = 0.6f + std::sin(hue + 2.094f) * 0.4f;
          b = 0.6f + std::sin(hue + 4.188f) * 0.4f;
        }
        break;
      case JudgeType::Nice:
        // 青白い
        {
          r = 0.4f;
          g = 0.7f;
          b = 1.0f;
        }
        break;
      case JudgeType::Miss:
        // 赤
        {
          r = 1.0f;
          g = 0.3f;
          b = 0.1f;
        }
        break;
      default:
        r = 1.0f;
        g = 0.8f;
        b = 0.4f;
        break;
      }

      // 発光感
      float brightness = 2.0f + (1.0f - layerOffset) * 1.5f;
      p.baseColor = {r * brightness, g * brightness, b * brightness, 1.0f};
      mr->color = p.baseColor;
    }
  }
}

void GameJuiceSystem::TriggerShotEffect(core::GameContext &ctx,
                                        const DirectX::XMFLOAT3 &position,
                                        const DirectX::XMFLOAT3 &direction,
                                        float power, JudgeType judge) {
  XMVECTOR dirVec = XMLoadFloat3(&direction);
  if (XMVectorGetX(XMVector3LengthSq(dirVec)) < 0.0001f) {
    dirVec = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
  }
  dirVec = XMVector3Normalize(XMVectorSetY(dirVec, 0.0f));
  XMFLOAT3 dir;
  XMStoreFloat3(&dir, dirVec);

  XMVECTOR rightVec =
      XMVector3Normalize(XMVector3Cross(XMVectorSet(0, 1, 0, 0), dirVec));
  XMFLOAT3 right;
  XMStoreFloat3(&right, rightVec);

  float quality = 0.85f;
  if (judge == JudgeType::Special) {
    quality = 1.55f;
    TriggerHitStop(0.07f, 0.0f);
    TriggerCameraShake(0.32f, 0.22f);
  } else if (judge == JudgeType::Great) {
    quality = 1.25f;
    TriggerHitStop(0.045f, 0.0f);
    TriggerCameraShake(0.22f, 0.18f);
  } else if (judge == JudgeType::Nice) {
    quality = 0.95f;
    TriggerCameraShake(0.14f, 0.14f);
  } else if (judge == JudgeType::Miss) {
    quality = 0.62f;
    TriggerCameraShake(0.18f, 0.16f);
  }

  const int count = std::min(kImpactBurstCount, 34);
  const float power01 = std::clamp(power, 0.0f, 1.0f);
  for (int i = 0; i < count; ++i) {
    auto &p = m_impactParticles[i];
    auto *t = ctx.world.Get<Transform>(p.entity);
    auto *mr = ctx.world.Get<MeshRenderer>(p.entity);
    if (!t || !mr) {
      continue;
    }

    const int layer = i % 5;
    const float side = RandCentered();
    const float forward = 0.12f + Rand01() * 0.34f;
    t->position = position;
    t->position.x += right.x * side * 0.72f + dir.x * forward;
    t->position.y += 0.05f + Rand01() * 0.16f;
    t->position.z += right.z * side * 0.72f + dir.z * forward;

    XMVECTOR rot = XMQuaternionRotationRollPitchYaw(Rand01() * XM_2PI,
                                                    Rand01() * XM_2PI,
                                                    Rand01() * XM_2PI);
    XMStoreFloat4(&t->rotation, rot);

    XMFLOAT3 tint = ShotJudgeColor(judge, i);
    const bool isMiss = judge == JudgeType::Miss;
    const bool isSpark =
        judge == JudgeType::Special || judge == JudgeType::Great;

    if (layer == 0 && isSpark) {
      p.kind = ImpactParticleKind::ShotRing;
      mr->mesh = ctx.resource.LoadMesh("builtin/quad");
      mr->blendMode = BlendMode::Add;
      mr->customFlags = {0.0f, 1.0f, 0.0f, 0.0f};
      p.baseScale = 0.32f + power01 * 0.26f + quality * 0.08f;
      p.maxLifetime = 0.38f + quality * 0.12f;
      p.baseColor = ScaleColor(tint, 2.4f + quality * 0.8f, 0.92f);
      p.velocity = {dir.x * (2.0f + quality * 2.2f),
                    0.25f + quality * 0.42f,
                    dir.z * (2.0f + quality * 2.2f)};
      p.angularVelocity = {0.0f, 10.0f + Rand01() * 8.0f, 0.0f};
    } else if (layer <= 2 && !isMiss) {
      p.kind = ImpactParticleKind::ShotSpark;
      mr->mesh = ctx.resource.LoadMesh("builtin/quad");
      mr->blendMode = BlendMode::Add;
      mr->customFlags = {0.0f, 1.0f, 0.0f, 0.0f};
      p.baseScale = 0.08f + power01 * 0.07f + Rand01() * 0.04f;
      p.maxLifetime = 0.42f + Rand01() * 0.22f + quality * 0.12f;
      p.baseColor = ScaleColor(tint, 2.0f + quality * 1.1f, 0.95f);
      float speed = 3.8f + power01 * 4.5f + quality * 2.2f;
      p.velocity = {dir.x * speed + right.x * side * 5.2f,
                    0.7f + Rand01() * 1.4f + quality * 0.35f,
                    dir.z * speed + right.z * side * 5.2f};
      p.angularVelocity = {RandCentered() * 8.0f, RandCentered() * 12.0f,
                           RandCentered() * 8.0f};
    } else {
      p.kind = ImpactParticleKind::ShotDust;
      mr->mesh = ctx.resource.LoadMesh("builtin/sphere");
      mr->blendMode = BlendMode::Alpha;
      mr->customFlags = {1.0f, isMiss ? 1.0f : 0.0f, 0.0f, 0.0f};
      p.baseScale =
          (isMiss ? 0.22f : 0.14f) + power01 * 0.08f + Rand01() * 0.06f;
      p.maxLifetime = (isMiss ? 0.75f : 0.52f) + Rand01() * 0.24f;
      p.baseColor = isMiss ? XMFLOAT4{0.72f, 0.46f, 0.30f, 0.9f}
                           : XMFLOAT4{0.86f, 0.78f, 0.54f, 0.72f};
      float speed = 1.4f + power01 * 2.0f;
      p.velocity = {-dir.x * speed + right.x * side * 2.8f,
                    0.35f + Rand01() * 0.8f,
                    -dir.z * speed + right.z * side * 2.8f};
      p.angularVelocity = {RandCentered() * 4.0f, RandCentered() * 4.0f,
                           RandCentered() * 4.0f};
    }

    p.lifetime = p.maxLifetime;
    t->scale = {p.baseScale, p.baseScale, p.baseScale};
    mr->color = p.baseColor;
    mr->isTransparent = true;
    mr->isVisible = true;
  }
}

void GameJuiceSystem::UpdateImpactParticles(core::GameContext &ctx) {
  const float gravity = 15.0f;

  for (auto &p : m_impactParticles) {
    if (p.lifetime <= 0.0f)
      continue;

    p.lifetime -= ctx.dt;

    float particleGravity = gravity;
    float drag = 0.98f;
    if (p.kind == ImpactParticleKind::Star) {
      particleGravity = 4.0f;
      drag = 0.985f;
    } else if (p.kind == ImpactParticleKind::Sparkle) {
      particleGravity = 2.0f;
      drag = 0.975f;
    } else if (p.kind == ImpactParticleKind::Glint) {
      particleGravity = 0.0f;
      drag = 0.965f;
    } else if (p.kind == ImpactParticleKind::Confetti) {
      particleGravity = 7.5f;
      drag = 0.99f;
    } else if (p.kind == ImpactParticleKind::ShotSpark) {
      particleGravity = 6.0f;
      drag = 0.965f;
    } else if (p.kind == ImpactParticleKind::ShotDust) {
      particleGravity = 3.2f;
      drag = 0.94f;
    } else if (p.kind == ImpactParticleKind::ShotRing) {
      particleGravity = 0.0f;
      drag = 0.955f;
    }

    // 物理更新
    p.velocity.y -= particleGravity * ctx.dt;
    p.velocity.x *= drag; // 空気抵抗
    p.velocity.z *= drag;

    auto *t = ctx.world.Get<Transform>(p.entity);
    auto *mr = ctx.world.Get<MeshRenderer>(p.entity);

    if (t) {
      t->position.x += p.velocity.x * ctx.dt;
      t->position.y += p.velocity.y * ctx.dt;
      t->position.z += p.velocity.z * ctx.dt;

      // 縮小しながらフェードアウト
      float lifeRatio = std::max(0.0f, p.lifetime / p.maxLifetime);
      float progress = 1.0f - lifeRatio;
      if (p.kind == ImpactParticleKind::Star) {
        float pulse = 0.86f + std::sin(progress * XM_2PI * 3.0f) * 0.14f;
        float scale = p.baseScale * pulse * (0.35f + lifeRatio * 0.85f);
        t->scale = {scale, scale, scale * 0.18f};
      } else if (p.kind == ImpactParticleKind::Sparkle) {
        float scale = p.baseScale * (0.15f + lifeRatio * 1.15f);
        t->scale = {scale * 0.35f, scale * 2.2f, scale * 0.35f};
      } else if (p.kind == ImpactParticleKind::Glint) {
        float pulse = std::sin(std::clamp(progress * 1.4f, 0.0f, 1.0f) * XM_PI);
        float scale = p.baseScale * (0.4f + pulse * 1.25f);
        t->scale = {scale * 2.8f, scale * 0.18f, scale * 2.8f};
      } else if (p.kind == ImpactParticleKind::Confetti) {
        float scale = p.baseScale * std::pow(lifeRatio, 0.35f);
        t->scale = {scale * 1.55f, scale * 0.22f, scale * 0.95f};
      } else if (p.kind == ImpactParticleKind::ShotSpark) {
        float scale = p.baseScale * (0.25f + lifeRatio * 1.05f);
        t->scale = {scale * 0.32f, scale * 1.9f, scale * 0.32f};
      } else if (p.kind == ImpactParticleKind::ShotDust) {
        float scale = p.baseScale * (1.0f + progress * 2.35f);
        t->scale = {scale * 1.2f, scale * 0.5f, scale * 1.2f};
      } else if (p.kind == ImpactParticleKind::ShotRing) {
        float pulse = std::sin(std::clamp(progress * 1.35f, 0.0f, 1.0f) * XM_PI);
        float scale = p.baseScale * (0.45f + progress * 3.2f + pulse * 0.45f);
        t->scale = {scale * 2.2f, scale * 0.08f, scale * 2.2f};
      } else {
        float scale = p.baseScale * std::pow(lifeRatio, 0.5f);
        t->scale = {scale, scale, scale};
      }

      XMVECTOR rot = XMLoadFloat4(&t->rotation);
      XMVECTOR angVel = XMLoadFloat3(&p.angularVelocity);
      XMVECTOR deltaRot = XMQuaternionRotationRollPitchYaw(
          angVel.m128_f32[0] * ctx.dt, angVel.m128_f32[1] * ctx.dt,
          angVel.m128_f32[2] * ctx.dt);
      rot = XMQuaternionMultiply(rot, deltaRot);
      XMStoreFloat4(&t->rotation, rot);
    }

    if (mr) {
      float lifeRatio = std::max(0.0f, p.lifetime / p.maxLifetime);
      float progress = 1.0f - lifeRatio;
      mr->color = p.baseColor;
      if (p.kind == ImpactParticleKind::Star) {
        float twinkle = 0.78f + std::sin(progress * XM_2PI * 5.0f) * 0.22f;
        mr->color.w = p.baseColor.w * std::pow(lifeRatio, 0.45f) * twinkle;
      } else if (p.kind == ImpactParticleKind::Sparkle) {
        mr->color.w = p.baseColor.w * std::pow(lifeRatio, 0.85f);
      } else if (p.kind == ImpactParticleKind::Glint) {
        float pulse = std::sin(std::clamp(progress * 1.4f, 0.0f, 1.0f) * XM_PI);
        mr->color.w = p.baseColor.w * pulse * std::pow(lifeRatio, 0.35f);
      } else if (p.kind == ImpactParticleKind::Confetti) {
        mr->color.w = p.baseColor.w * std::pow(lifeRatio, 0.7f);
      } else if (p.kind == ImpactParticleKind::ShotSpark) {
        float twinkle = 0.78f + std::sin(progress * XM_2PI * 6.0f) * 0.22f;
        mr->color.w = p.baseColor.w * std::pow(lifeRatio, 0.72f) * twinkle;
      } else if (p.kind == ImpactParticleKind::ShotDust) {
        float hold = 1.0f - SmoothFade(progress, 0.5f, 1.0f);
        mr->color.w = p.baseColor.w * std::clamp(hold, 0.0f, 1.0f);
      } else if (p.kind == ImpactParticleKind::ShotRing) {
        float pulse = std::sin(std::clamp(progress * 1.35f, 0.0f, 1.0f) * XM_PI);
        mr->color.w = p.baseColor.w * pulse * std::pow(lifeRatio, 0.45f);
      } else {
        mr->color.w = lifeRatio; // アルファ減衰
      }

      if (p.lifetime <= 0.0f) {
        mr->isVisible = false;
      }
    }
  }
}

// =============================================================================
// マテリアルエフェクト
// =============================================================================

void GameJuiceSystem::TriggerMaterialEffect(
    core::GameContext &ctx, const DirectX::XMFLOAT3 &position,
    game::components::TerrainMaterial material, float strength) {
  const bool isBunker = material == TerrainMaterial::Bunker;
  const int count = isBunker
                        ? std::clamp(34 + static_cast<int>(strength * 22.0f),
                                     34, 56)
                        : std::clamp(static_cast<int>(strength * 6.0f), 1, 6);
  const auto particleShader = ctx.resource.LoadShader(
      "Particle", L"shaders/ParticleVS.hlsl", L"shaders/ParticlePS.hlsl");
  const auto basicShader = ctx.resource.LoadShader(
      "Basic", L"Assets/shaders/BasicVS.hlsl",
      L"Assets/shaders/BasicPS.hlsl");

  if (isBunker) {
    SpawnSandImprint(ctx, position, 0.17f + strength * 0.18f,
                     1.15f + strength * 0.80f,
                     {1.02f, 0.89f, 0.62f, 0.24f});
    TriggerCameraShake(0.035f + strength * 0.065f,
                       0.12f + strength * 0.12f);
  }

  for (int k = 0; k < count; ++k) {
    auto &p = m_envParticles[m_envWriteIndex];
    m_envWriteIndex = (m_envWriteIndex + 1) % kEnvParticleCount;

    p.lifetime = 0.5f + Rand01() * 0.5f;
    p.maxLifetime = p.lifetime;
    p.groundHeight = position.y;

    auto *t = ctx.world.Get<Transform>(p.entity);
    auto *mr = ctx.world.Get<MeshRenderer>(p.entity);

    if (t && mr) {
      t->position = position;
      t->position.y += isBunker ? 0.012f : 0.1f;

      mr->isVisible = true;
      mr->customFlags = {0.0f, 0.0f, 0.0f, 0.0f};

      const float speed = strength * 5.0f * (0.8f + Rand01() * 0.4f);
      const float angle = Rand01() * XM_2PI;
      const float upBias = 0.5f + Rand01() * 0.5f;

      p.velocity.x = std::cos(angle) * speed * 0.5f;
      p.velocity.y = speed * upBias;
      p.velocity.z = std::sin(angle) * speed * 0.5f;

      // 個別パラメータ
      switch (material) {
      case TerrainMaterial::Bunker: {
        const bool makeDust = (k % 4) == 0;
        const bool makeClump = !makeDust && (k % 5) == 0;
        const float radialSpeed =
            (1.2f + strength * 4.8f) * (0.55f + Rand01() * 0.75f);

        if (makeDust) {
          p.kind = EnvironmentParticleKind::SandDust;
          mr->mesh = ctx.resource.LoadMesh("builtin/sphere");
          mr->shader = particleShader;
          mr->isTransparent = true;
          mr->blendMode = BlendMode::Alpha;
          mr->customFlags = {1.0f, k == 0 ? 1.0f : 0.0f, 0.0f, 0.0f};
          const float warmth = Rand01() * 0.10f;
          p.baseColor = {1.05f + warmth, 0.82f + warmth * 0.7f,
                         0.45f + warmth * 0.35f, 0.93f};
          p.baseScale = 0.18f + strength * 0.22f + Rand01() * 0.13f;
          p.lifetime = 1.0f + strength * 0.55f + Rand01() * 0.55f;
          p.velocity = {std::cos(angle) * radialSpeed * 0.28f,
                        0.45f + strength * 1.05f + Rand01() * 0.65f,
                        std::sin(angle) * radialSpeed * 0.28f};
          p.angularVelocity = {0, 0, 0};
          t->scale = {p.baseScale, p.baseScale * 0.72f, p.baseScale};
        } else if (makeClump) {
          p.kind = EnvironmentParticleKind::SandClump;
          mr->mesh = ctx.resource.LoadMesh("builtin/rock");
          mr->shader = basicShader;
          mr->isTransparent = false;
          mr->blendMode = BlendMode::Opaque;
          const float shade = Rand01() * 0.16f;
          p.baseColor = {0.84f + shade, 0.63f + shade * 0.75f,
                         0.34f + shade * 0.35f, 1.0f};
          p.baseScale = 0.040f + strength * 0.050f + Rand01() * 0.035f;
          p.lifetime = 0.72f + Rand01() * 0.48f;
          p.velocity = {std::cos(angle) * radialSpeed * 0.72f,
                        1.4f + strength * 3.0f + Rand01() * 1.2f,
                        std::sin(angle) * radialSpeed * 0.72f};
          p.angularVelocity = {RandCentered() * 24.0f,
                               RandCentered() * 30.0f,
                               RandCentered() * 24.0f};
          t->scale = {p.baseScale * 1.35f, p.baseScale * 0.75f,
                      p.baseScale};
        } else {
          p.kind = EnvironmentParticleKind::SandGrain;
          mr->mesh = ctx.resource.LoadMesh("builtin/cube");
          mr->shader = basicShader;
          mr->isTransparent = false;
          mr->blendMode = BlendMode::Opaque;
          const float brightness = Rand01() * 0.18f;
          p.baseColor = {0.94f + brightness, 0.72f + brightness * 0.72f,
                         0.39f + brightness * 0.35f, 1.0f};
          p.baseScale = 0.014f + strength * 0.018f + Rand01() * 0.018f;
          p.lifetime = 0.58f + Rand01() * 0.44f;
          p.velocity = {std::cos(angle) * radialSpeed,
                        1.0f + strength * 3.8f + Rand01() * 1.8f,
                        std::sin(angle) * radialSpeed};
          p.angularVelocity = {RandCentered() * 40.0f,
                               RandCentered() * 48.0f,
                               RandCentered() * 40.0f};
          t->scale = {p.baseScale * (0.65f + Rand01()),
                      p.baseScale * (0.45f + Rand01() * 0.8f),
                      p.baseScale * (0.65f + Rand01())};
        }
        p.maxLifetime = p.lifetime;
        mr->color = p.baseColor;
        break;
      }

      case TerrainMaterial::Rough:
      case TerrainMaterial::Fairway:
      case TerrainMaterial::Green: {
        // 芝・葉っぱ
        p.kind = EnvironmentParticleKind::GrassClip;
        mr->mesh = ctx.resource.LoadMesh("builtin/cube");
        mr->shader = particleShader;
        mr->isTransparent = true;
        mr->blendMode = BlendMode::Alpha;
        mr->customFlags.x = 0.0f;
        mr->customFlags.y = 0.0f;

        if (material == game::components::TerrainMaterial::Rough) {
          p.baseColor = {0.12f, 0.38f, 0.10f, 1.0f}; // 濃い緑
          float sizeJitter = 0.7f + ((float)(rand() % 100) / 100.0f) * 0.6f;
          p.baseScale = (0.1f + strength * 0.15f) * sizeJitter;
        } else if (material == game::components::TerrainMaterial::Green) {
          p.baseColor = {0.18f, 0.78f, 0.28f, 1.0f}; // 鮮やか
          p.baseScale = 0.05f + strength * 0.05f; // 小さい
        } else {
          p.baseColor = {0.25f, 0.58f, 0.18f, 1.0f}; // 普通
          p.baseScale = 0.08f + strength * 0.1f;
        }
        mr->color = p.baseColor;

        t->scale = {p.baseScale * 1.5f, p.baseScale * 0.1f, p.baseScale * 1.5f};

        float spinScale = 45.0f;
        p.angularVelocity.x =
            ((float)(rand() % 100) / 100.0f - 0.5f) * spinScale;
        p.angularVelocity.y =
            ((float)(rand() % 100) / 100.0f - 0.5f) * spinScale;
        p.angularVelocity.z =
            ((float)(rand() % 100) / 100.0f - 0.5f) * spinScale;
        break;
      }

      default:
        // 岩など（汎用拡散）
        p.kind = EnvironmentParticleKind::GenericDebris;
        mr->mesh = ctx.resource.LoadMesh("builtin/cube"); // 岩片
        mr->shader = basicShader;
        mr->isTransparent = false;
        mr->blendMode = BlendMode::Opaque;
        p.baseColor = {0.5f, 0.5f, 0.5f, 1.0f};
        mr->color = p.baseColor;
        mr->customFlags.x = 0.0f;
        mr->customFlags.y = 0.0f;
        p.baseScale = 0.05f + strength * 0.1f;
        t->scale = {p.baseScale, p.baseScale, p.baseScale};
        break;
      }
    }
  }
}

void GameJuiceSystem::CreateEnvironmentParticleEntities(
    core::GameContext &ctx) {
  m_envParticles.clear();
  m_envParticles.resize(kEnvParticleCount);

  for (int i = 0; i < kEnvParticleCount; ++i) {
    auto e = ctx.world.CreateEntity();

    auto &t = ctx.world.Add<Transform>(e);
    t.position = {0, -100, 0};
    t.scale = {0.1f, 0.1f, 0.1f};

    auto &mr = ctx.world.Add<MeshRenderer>(e);
    // 初期は全てcube、Emit時にmeshを切り替える（リソースロード済み前提）
    mr.mesh = ctx.resource.LoadMesh("builtin/cube");
    mr.shader = ctx.resource.LoadShader("Particle", L"shaders/ParticleVS.hlsl",
                                        L"shaders/ParticlePS.hlsl");
    mr.isVisible = false;
    mr.isTransparent = true;

    m_envParticles[i].entity = e;
    m_envParticles[i].lifetime = 0.0f;
  }
  m_envWriteIndex = 0;
}

void GameJuiceSystem::CreateSandSurfaceEntities(core::GameContext &ctx) {
  const auto craterMesh = ctx.resource.LoadMesh("builtin/sand_crater");
  const auto basicShader = ctx.resource.LoadShader(
      "Basic", L"Assets/shaders/BasicVS.hlsl",
      L"Assets/shaders/BasicPS.hlsl");

  m_sandImprints.clear();
  m_sandImprints.resize(kSandImprintCount);
  for (SandImprint &imprint : m_sandImprints) {
    imprint.entity = ctx.world.CreateEntity();
    auto &transform = ctx.world.Add<Transform>(imprint.entity);
    transform.position = {0.0f, -100.0f, 0.0f};
    transform.scale = {0.1f, 0.1f, 0.1f};

    auto &renderer = ctx.world.Add<MeshRenderer>(imprint.entity);
    renderer.mesh = craterMesh;
    renderer.shader = basicShader;
    renderer.color = {1.0f, 0.9f, 0.66f, 0.0f};
    renderer.isVisible = false;
    renderer.isTransparent = true;
    renderer.blendMode = BlendMode::Alpha;
    renderer.maxDrawDistance = 65.0f;
    renderer.boundsScale = 1.4f;
  }
  m_sandImprintWriteIndex = 0;

  m_sandCollarEntity = ctx.world.CreateEntity();
  auto &collarTransform = ctx.world.Add<Transform>(m_sandCollarEntity);
  collarTransform.position = {0.0f, -100.0f, 0.0f};
  collarTransform.scale = {0.085f, 0.42f, 0.085f};

  auto &collarRenderer = ctx.world.Add<MeshRenderer>(m_sandCollarEntity);
  collarRenderer.mesh = craterMesh;
  collarRenderer.shader = basicShader;
  collarRenderer.color = {1.05f, 0.91f, 0.62f, 0.42f};
  collarRenderer.isVisible = false;
  collarRenderer.isTransparent = true;
  collarRenderer.blendMode = BlendMode::Alpha;
  collarRenderer.maxDrawDistance = 55.0f;
  collarRenderer.boundsScale = 1.8f;
}

void GameJuiceSystem::SpawnSandImprint(core::GameContext &ctx,
                                       const XMFLOAT3 &position, float scale,
                                       float lifetime,
                                       const XMFLOAT4 &color) {
  if (m_sandImprints.empty()) {
    return;
  }

  SandImprint &imprint = m_sandImprints[m_sandImprintWriteIndex];
  m_sandImprintWriteIndex =
      (m_sandImprintWriteIndex + 1) % kSandImprintCount;
  imprint.lifetime = std::max(lifetime, 0.1f);
  imprint.maxLifetime = imprint.lifetime;
  imprint.startScale = std::max(scale, 0.02f);
  imprint.baseColor = color;

  if (auto *transform = ctx.world.Get<Transform>(imprint.entity)) {
    transform->position = position;
    transform->position.y += 0.0025f;
    transform->scale = {imprint.startScale, 0.55f,
                        imprint.startScale * (0.82f + Rand01() * 0.24f)};
    XMStoreFloat4(&transform->rotation,
                  XMQuaternionRotationRollPitchYaw(0.0f, Rand01() * XM_2PI,
                                                   0.0f));
  }
  if (auto *renderer = ctx.world.Get<MeshRenderer>(imprint.entity)) {
    renderer->color = color;
    renderer->isVisible = true;
  }
}

void GameJuiceSystem::UpdateSandSurfaceEffects(core::GameContext &ctx,
                                               ecs::Entity targetEntity) {
  for (SandImprint &imprint : m_sandImprints) {
    if (imprint.lifetime <= 0.0f) {
      continue;
    }
    imprint.lifetime = std::max(0.0f, imprint.lifetime - ctx.dt);
    const float lifeRatio = imprint.lifetime / imprint.maxLifetime;
    if (auto *renderer = ctx.world.Get<MeshRenderer>(imprint.entity)) {
      const float fade = std::pow(std::clamp(lifeRatio, 0.0f, 1.0f), 1.8f);
      renderer->color = imprint.baseColor;
      renderer->color.w = imprint.baseColor.w * fade;
      if (imprint.lifetime <= 0.0f) {
        renderer->isVisible = false;
      }
    }
  }

  auto *state = ctx.world.GetGlobal<GolfGameState>();
  auto *ballTransform = ctx.world.Get<Transform>(targetEntity);
  auto *ballBody = ctx.world.Get<RigidBody>(targetEntity);
  auto *ballCollider = ctx.world.Get<Collider>(targetEntity);
  auto *collarRenderer = ctx.world.Get<MeshRenderer>(m_sandCollarEntity);
  auto *collarTransform = ctx.world.Get<Transform>(m_sandCollarEntity);
  const bool isInSand = state && ballTransform && ballCollider &&
                        state->isBallGrounded &&
                        state->currentMaterial == TerrainMaterial::Bunker;

  if (!isInSand) {
    if (collarRenderer) {
      collarRenderer->isVisible = false;
    }
    m_sandTrackTimer = 0.0f;
    return;
  }

  const float speed = ballBody
                          ? std::sqrt(ballBody->velocity.x * ballBody->velocity.x +
                                      ballBody->velocity.y * ballBody->velocity.y +
                                      ballBody->velocity.z * ballBody->velocity.z)
                          : 0.0f;
  const float verticalImpact =
      ballBody ? std::max(0.0f, -ballBody->velocity.y) : 0.0f;
  const float sink = ComputeSurfaceSinkDepth(
      TerrainMaterial::Bunker, verticalImpact, speed, ballCollider->radius);
  const float surfaceHeight =
      ballTransform->position.y - ballCollider->radius + sink;

  if (collarTransform && collarRenderer) {
    collarTransform->position = {ballTransform->position.x,
                                 surfaceHeight + 0.003f,
                                 ballTransform->position.z};
    const float collarScale = 0.075f + sink * 1.15f +
                              std::clamp(speed / 30.0f, 0.0f, 0.025f);
    collarTransform->scale = {collarScale, 0.46f, collarScale};
    collarRenderer->color = {1.06f, 0.91f, 0.61f,
                             std::clamp(0.34f + sink * 5.0f, 0.34f, 0.50f)};
    collarRenderer->isVisible = true;
  }

  if (speed > 0.35f) {
    m_sandTrackTimer += ctx.dt;
    const float interval = std::clamp(0.11f - speed * 0.002f, 0.045f, 0.11f);
    if (m_sandTrackTimer >= interval) {
      m_sandTrackTimer = 0.0f;
      SpawnSandImprint(
          ctx,
          {ballTransform->position.x, surfaceHeight, ballTransform->position.z},
          0.050f + std::clamp(speed / 300.0f, 0.0f, 0.035f),
          0.75f + Rand01() * 0.55f,
          {0.86f, 0.70f, 0.43f, 0.16f});
    }
  }
}

void GameJuiceSystem::CreateRippleEntities(core::GameContext &ctx) {
  m_ripples.clear();
  m_ripples.resize(kRippleCount);

  for (int i = 0; i < kRippleCount; ++i) {
    auto e = ctx.world.CreateEntity();
    auto &t = ctx.world.Add<Transform>(e);
    t.position = {0, -100, 0};
    t.scale = {0.5f, 0.02f, 0.5f};

    auto &mr = ctx.world.Add<MeshRenderer>(e);
    mr.mesh = ctx.resource.LoadMesh("builtin/cylinder");
    mr.shader = ctx.resource.LoadShader("Particle", L"shaders/ParticleVS.hlsl",
                                        L"shaders/ParticlePS.hlsl");
    mr.isVisible = false;
    mr.isTransparent = true;
    mr.color = {0.6f, 0.8f, 1.2f, 0.0f};

    m_ripples[i].entity = e;
    m_ripples[i].lifetime = 0.0f;
  }
  m_rippleWriteIndex = 0;
}

void GameJuiceSystem::EmitEnvironmentParticles(core::GameContext &ctx,
                                               ecs::Entity targetEntity) {
  auto *state = ctx.world.GetGlobal<GolfGameState>();
  if (!state || !state->isBallGrounded || state->currentBallSpeed < 0.5f) {
    return;
  }

  m_envEmitTimer += ctx.dt;
  // 速度が速いほどたくさん出す
  const float speed01 =
      std::clamp(state->currentBallSpeed / 28.0f, 0.0f, 1.0f);
  float interval =
      0.07f / (std::max<float>(1.0f, state->currentBallSpeed * 0.2f));

  if (m_envEmitTimer >= interval) {
    m_envEmitTimer = 0.0f;

    auto *targetT = ctx.world.Get<Transform>(targetEntity);
    if (!targetT)
      return;

    // 放出量
    int count = 1;
    if (state->currentMaterial == TerrainMaterial::Bunker) {
      count = 6 + static_cast<int>(speed01 * 8.0f);
    } else if (state->currentMaterial == TerrainMaterial::Rough ||
               state->currentMaterial == TerrainMaterial::Fairway) {
      count = 1 + static_cast<int>(speed01 * 1.8f);
    } else if (state->currentMaterial == TerrainMaterial::Green) {
      count = state->currentBallSpeed > 2.5f ? 1 : 0;
    }

    for (int k = 0; k < count; ++k) {
      auto &p = m_envParticles[m_envWriteIndex];
      m_envWriteIndex = (m_envWriteIndex + 1) % kEnvParticleCount;

      p.lifetime = 0.75f + Rand01() * 0.45f;
      p.maxLifetime = p.lifetime;

      auto *t = ctx.world.Get<Transform>(p.entity);
      auto *mr = ctx.world.Get<MeshRenderer>(p.entity);

      if (t && mr) {
        t->position = targetT->position;
        p.groundHeight = targetT->position.y - 0.02f;
        if (state->currentMaterial == TerrainMaterial::Bunker) {
          auto *collider = ctx.world.Get<Collider>(targetEntity);
          auto *body = ctx.world.Get<RigidBody>(targetEntity);
          if (collider) {
            const float verticalImpact =
                body ? std::max(0.0f, -body->velocity.y) : 0.0f;
            const float sink = ComputeSurfaceSinkDepth(
                TerrainMaterial::Bunker, verticalImpact,
                state->currentBallSpeed, collider->radius);
            p.groundHeight = targetT->position.y - collider->radius + sink;
          }
          t->position.y = p.groundHeight + 0.012f;
        } else {
          t->position.y += 0.05f;
        }

        mr->isVisible = true;
        mr->isTransparent = true;
        mr->blendMode = BlendMode::Alpha;
        mr->customFlags = {0.0f, 0.0f, 0.0f, 0.0f};

        // マテリアル別の設定
        switch (state->currentMaterial) {
        case TerrainMaterial::Bunker: {
          auto *rb = ctx.world.Get<RigidBody>(targetEntity);
          XMVECTOR v = rb ? XMLoadFloat3(&rb->velocity) : XMVectorZero();
          const bool makeDust = (k % 3) == 0;
          const bool makeClump = !makeDust && speed01 > 0.35f && (k % 5) == 0;
          if (makeDust) {
            p.kind = EnvironmentParticleKind::SandDust;
            mr->mesh = ctx.resource.LoadMesh("builtin/sphere");
            mr->shader = ctx.resource.LoadShader(
                "Particle", L"shaders/ParticleVS.hlsl",
                L"shaders/ParticlePS.hlsl");
            mr->isTransparent = true;
            mr->blendMode = BlendMode::Alpha;
            mr->customFlags = {1.0f, 1.0f, 0.0f, 0.0f};
            const float warmth = Rand01() * 0.08f;
            p.baseColor = {1.02f + warmth, 0.81f + warmth * 0.7f,
                           0.46f + warmth * 0.4f, 0.92f};
            p.baseScale = 0.14f + Rand01() * 0.12f + speed01 * 0.10f;
            t->scale = {p.baseScale, p.baseScale * 0.7f, p.baseScale};
            p.lifetime = 0.72f + Rand01() * 0.45f + speed01 * 0.45f;
            v = XMVectorScale(v, -0.10f - speed01 * 0.10f);
            XMStoreFloat3(&p.velocity, v);
            p.velocity.x += RandCentered() * (0.5f + speed01 * 0.5f);
            p.velocity.y = 0.28f + Rand01() * 0.55f + speed01 * 0.35f;
            p.velocity.z += RandCentered() * (0.5f + speed01 * 0.5f);
            p.angularVelocity = {0, 0, 0};
          } else {
            p.kind = makeClump ? EnvironmentParticleKind::SandClump
                               : EnvironmentParticleKind::SandGrain;
            mr->mesh = ctx.resource.LoadMesh(makeClump ? "builtin/rock"
                                                       : "builtin/cube");
            mr->shader = ctx.resource.LoadShader(
                "Basic", L"Assets/shaders/BasicVS.hlsl",
                L"Assets/shaders/BasicPS.hlsl");
            mr->isTransparent = false;
            mr->blendMode = BlendMode::Opaque;
            mr->customFlags = {0.0f, 0.0f, 0.0f, 0.0f};
            p.baseColor = makeClump
                              ? XMFLOAT4{0.86f, 0.64f, 0.35f, 1.0f}
                              : XMFLOAT4{1.02f, 0.79f, 0.44f, 1.0f};
            p.baseScale = makeClump ? 0.040f + speed01 * 0.035f
                                    : 0.014f + speed01 * 0.016f;
            t->scale = {p.baseScale * (makeClump ? 1.4f : 0.7f),
                        p.baseScale * (makeClump ? 0.8f : 0.5f),
                        p.baseScale};
            const float angle = Rand01() * XM_2PI;
            const float scatter = 0.8f + speed01 * 3.4f;
            p.velocity = {std::cos(angle) * scatter,
                          0.55f + Rand01() * 1.25f + speed01 * 1.4f,
                          std::sin(angle) * scatter};
            p.angularVelocity = {RandCentered() * 32.0f,
                                 RandCentered() * 40.0f,
                                 RandCentered() * 32.0f};
            p.lifetime = 0.42f + Rand01() * 0.38f;
          }
          p.maxLifetime = p.lifetime;
          mr->color = p.baseColor;
          break;
        }
        case TerrainMaterial::Rough:
        case TerrainMaterial::Fairway: {
          // 芝片: 立方体(薄く)、緑、弾ける、回転
          p.kind = EnvironmentParticleKind::GrassClip;
          mr->mesh = ctx.resource.LoadMesh("builtin/cube");
          mr->shader = ctx.resource.LoadShader(
              "Particle", L"shaders/ParticleVS.hlsl",
              L"shaders/ParticlePS.hlsl");
          mr->customFlags.x = 0.0f; // 芝は通常の四角
          mr->customFlags.y = 0.0f;
          if (state->currentMaterial == TerrainMaterial::Rough) {
            p.baseColor = {0.08f, 0.34f + Rand01() * 0.08f, 0.08f, 1.0f};
          } else {
            p.baseColor = {0.22f, 0.56f + Rand01() * 0.14f, 0.14f, 1.0f};
          }
          mr->color = p.baseColor;
          p.baseScale = 0.055f + Rand01() * 0.055f + speed01 * 0.025f;
          float sizeJitter = 0.65f + Rand01() * 0.75f;
          p.baseScale *= sizeJitter;
          // 板状にする
          t->scale = {p.baseScale * (2.2f + Rand01() * 0.7f),
                      p.baseScale * 0.16f,
                      p.baseScale * (1.3f + Rand01() * 0.5f)};

          // 四方に弾ける
          float angle = Rand01() * XM_2PI;
          float speed = 2.2f + Rand01() * 3.0f + speed01 * 2.0f;
          p.velocity.x = std::cos(angle) * speed;
          p.velocity.y = 0.8f + Rand01() * 1.6f + speed01 * 0.6f;
          p.velocity.z = std::sin(angle) * speed;

          float angScale = 38.0f + speed01 * 18.0f;
          p.angularVelocity.x = RandCentered() * angScale;
          p.angularVelocity.y = RandCentered() * angScale;
          p.angularVelocity.z = RandCentered() * angScale;
          break;
        }
        case TerrainMaterial::Green: {
          // グリーン: 細かい削れ粉だけを控えめに出す
          p.kind = EnvironmentParticleKind::GrassClip;
          mr->mesh = ctx.resource.LoadMesh("builtin/cube");
          mr->shader = ctx.resource.LoadShader(
              "Particle", L"shaders/ParticleVS.hlsl",
              L"shaders/ParticlePS.hlsl");
          p.baseColor = {0.28f, 0.72f, 0.26f, 0.95f};
          mr->color = p.baseColor;
          mr->customFlags.x = 0.0f;
          mr->customFlags.y = 0.0f;
          p.baseScale = 0.035f + Rand01() * 0.025f;
          t->scale = {p.baseScale * 2.0f, p.baseScale * 0.12f,
                      p.baseScale * 1.4f};

          float angle = Rand01() * XM_2PI;
          float speed = 1.2f + Rand01() * 1.5f + speed01;
          p.velocity.x = std::cos(angle) * speed;
          p.velocity.y = 0.35f + Rand01() * 0.55f;
          p.velocity.z = std::sin(angle) * speed;

          float angScale = 24.0f;
          p.angularVelocity.x = RandCentered() * angScale;
          p.angularVelocity.y = RandCentered() * angScale;
          p.angularVelocity.z = RandCentered() * angScale;
          break;
        }
        default:
          mr->isVisible = false;
          p.lifetime = 0;
          break;
        }
      }
    }
  }
}

void GameJuiceSystem::UpdateEnvironmentParticles(core::GameContext &ctx,
                                                 ecs::Entity targetEntity) {
  const float gravity = 9.8f;

  for (auto &p : m_envParticles) {
    if (p.lifetime <= 0.0f)
      continue;

    p.lifetime -= ctx.dt;

    auto *t = ctx.world.Get<Transform>(p.entity);
    auto *mr = ctx.world.Get<MeshRenderer>(p.entity);

    if (t) {
      if (p.kind == EnvironmentParticleKind::SandDust) {
        const float drag = std::exp(-1.9f * ctx.dt);
        p.velocity.x *= drag;
        p.velocity.y += (0.16f - p.velocity.y) * 1.35f * ctx.dt;
        p.velocity.z *= drag;

        t->position.x += p.velocity.x * ctx.dt;
        t->position.y += p.velocity.y * ctx.dt;
        t->position.z += p.velocity.z * ctx.dt;

        float progress = 1.0f - (p.lifetime / p.maxLifetime);
        float scale = p.baseScale * (1.0f + progress * 2.8f);
        t->scale = {scale * (1.0f + progress * 0.45f),
                    scale * (0.62f + progress * 0.32f), scale};
      } else {
        float particleGravity = gravity;
        float drag = 0.18f;
        if (p.kind == EnvironmentParticleKind::SandGrain) {
          particleGravity = 13.5f;
          drag = 0.35f;
        } else if (p.kind == EnvironmentParticleKind::SandClump) {
          particleGravity = 11.5f;
          drag = 0.55f;
        }
        const float dragRatio = std::exp(-drag * ctx.dt);
        p.velocity.x *= dragRatio;
        p.velocity.y -= particleGravity * ctx.dt;
        p.velocity.z *= dragRatio;

        t->position.x += p.velocity.x * ctx.dt;
        t->position.y += p.velocity.y * ctx.dt;
        t->position.z += p.velocity.z * ctx.dt;

        // 回転更新
        XMVECTOR rot = XMLoadFloat4(&t->rotation);
        XMVECTOR angVel = XMLoadFloat3(&p.angularVelocity);
        XMVECTOR deltaRot = XMQuaternionRotationRollPitchYaw(
            angVel.m128_f32[0] * ctx.dt, angVel.m128_f32[1] * ctx.dt,
            angVel.m128_f32[2] * ctx.dt);
        rot = XMQuaternionMultiply(rot, deltaRot);
        XMStoreFloat4(&t->rotation, rot);

        if (t->position.y < p.groundHeight) {
          t->position.y = p.groundHeight + 0.002f;
          if ((p.kind == EnvironmentParticleKind::SandGrain ||
               p.kind == EnvironmentParticleKind::SandClump) &&
              std::abs(p.velocity.y) > 0.16f) {
            p.velocity.y = -p.velocity.y * 0.16f;
            p.velocity.x *= 0.48f;
            p.velocity.z *= 0.48f;
          } else {
            p.velocity = {0, 0, 0};
            p.lifetime = std::min(p.lifetime, 0.22f);
          }
        }
      }
    }

    if (mr) {
      float lifeRatio = std::max(0.0f, p.lifetime / p.maxLifetime);
      mr->color = p.baseColor;
      if (p.kind == EnvironmentParticleKind::SandDust) {
        float progress = 1.0f - lifeRatio;
        float alphaHold = 1.0f - SmoothFade(progress, 0.46f, 1.0f);
        mr->color.w = p.baseColor.w * std::clamp(alphaHold, 0.0f, 1.0f);
      } else if (p.kind == EnvironmentParticleKind::SandGrain ||
                 p.kind == EnvironmentParticleKind::SandClump) {
        mr->color.w = p.baseColor.w * SmoothFade(lifeRatio, 0.0f, 0.20f);
      } else {
        mr->color.w = p.baseColor.w * std::pow(lifeRatio, 0.55f);
      }

      if (p.lifetime <= 0.0f) {
        mr->isVisible = false;
      }
    }
  }

}

void GameJuiceSystem::TriggerConfetti(core::GameContext &ctx,
                                      const DirectX::XMFLOAT3 &position,
                                      float burstPower) {
  const int startIndex = kImpactBurstCount;
  const int count = kImpactParticleCount - startIndex;

  for (int i = 0; i < count; ++i) {
    auto &p = m_impactParticles[startIndex + i];
    auto *t = ctx.world.Get<Transform>(p.entity);
    auto *mr = ctx.world.Get<MeshRenderer>(p.entity);
    if (!t || !mr)
      continue;

    float angle = Rand01() * XM_2PI;
    float ring = 0.18f + Rand01() * 0.42f;
    float radialSpeed = (2.0f + burstPower * 4.2f) * (0.7f + Rand01() * 0.65f);
    float up = 1.8f + burstPower * 3.2f;

    t->position = position;
    t->position.x += std::cos(angle) * ring;
    t->position.y += 0.25f + Rand01() * 0.45f;
    t->position.z += std::sin(angle) * ring;

    XMVECTOR rot = XMQuaternionRotationRollPitchYaw(Rand01() * XM_2PI,
                                                    Rand01() * XM_2PI,
                                                    Rand01() * XM_2PI);
    XMStoreFloat4(&t->rotation, rot);

    p.velocity.x = std::cos(angle) * radialSpeed;
    p.velocity.z = std::sin(angle) * radialSpeed;
    p.velocity.y = up * (0.55f + Rand01() * 0.8f);
    p.angularVelocity = {RandCentered() * 12.0f, RandCentered() * 16.0f,
                         RandCentered() * 14.0f};

    const int layer = i % 6;
    if (layer == 0) {
      p.kind = ImpactParticleKind::Glint;
      mr->mesh = ctx.resource.LoadMesh("builtin/quad");
      mr->blendMode = BlendMode::Add;
      mr->customFlags = {0.0f, 1.0f, 0.0f, 0.0f};
      p.maxLifetime = 0.45f + Rand01() * 0.18f;
      p.baseScale = 0.55f + burstPower * 0.28f + Rand01() * 0.18f;
      p.baseColor = {2.8f, 2.35f, 1.05f, 0.95f};
      p.velocity.x *= 0.35f;
      p.velocity.z *= 0.35f;
      p.velocity.y *= 0.25f;
    } else if (layer == 1 || layer == 2) {
      p.kind = ImpactParticleKind::Star;
      mr->mesh = ctx.resource.LoadMesh("builtin/quad");
      mr->blendMode = BlendMode::Add;
      mr->customFlags = {0.0f, 1.0f, 0.0f, 0.0f};
      p.maxLifetime = 1.05f + Rand01() * 0.75f;
      p.baseScale = 0.18f + burstPower * 0.08f + Rand01() * 0.12f;
      p.baseColor = ScaleColor(CupInSparkleColor(i), 1.65f + Rand01() * 0.75f,
                               1.0f);
      p.velocity.x *= 0.72f;
      p.velocity.z *= 0.72f;
      p.velocity.y *= 0.78f;
    } else if (layer == 3 || layer == 4) {
      p.kind = ImpactParticleKind::Sparkle;
      mr->mesh = ctx.resource.LoadMesh("builtin/quad");
      mr->blendMode = BlendMode::Add;
      mr->customFlags = {0.0f, 1.0f, 0.0f, 0.0f};
      p.maxLifetime = 0.65f + Rand01() * 0.55f;
      p.baseScale = 0.08f + burstPower * 0.035f + Rand01() * 0.055f;
      p.baseColor = {2.4f, 2.1f, 0.85f, 0.9f};
      p.velocity.x *= 1.12f;
      p.velocity.z *= 1.12f;
      p.velocity.y *= 1.05f;
    } else {
      p.kind = ImpactParticleKind::Confetti;
      mr->mesh = ctx.resource.LoadMesh("builtin/cube");
      mr->blendMode = BlendMode::Alpha;
      mr->customFlags = {0.0f, 0.0f, 0.0f, 0.0f};
      p.maxLifetime = 1.1f + Rand01() * 0.55f;
      p.baseScale = 0.08f + burstPower * 0.05f + Rand01() * 0.04f;
      p.baseColor = ScaleColor(CupInSparkleColor(i + 3), 1.05f, 1.0f);
    }

    p.lifetime = p.maxLifetime;
    t->scale = {p.baseScale, p.baseScale, p.baseScale};
    mr->color = p.baseColor;
    mr->isTransparent = true;
    mr->isVisible = true;
  }
}

void GameJuiceSystem::TriggerRippleEffect(core::GameContext &ctx,
                                          const DirectX::XMFLOAT3 &position,
                                          float baseRadius, float strength) {
  if (m_ripples.empty())
    return;

  auto &r = m_ripples[m_rippleWriteIndex];
  m_rippleWriteIndex = (m_rippleWriteIndex + 1) % kRippleCount;

  r.maxLifetime = 0.45f + strength * 0.4f;
  r.lifetime = r.maxLifetime;
  r.startScale = std::max(baseRadius, 0.1f);

  if (auto *t = ctx.world.Get<Transform>(r.entity)) {
    t->position = position;
    t->position.y += 0.02f; // 地面より少し上
    t->scale = {r.startScale, 0.02f, r.startScale};
  }

  if (auto *mr = ctx.world.Get<MeshRenderer>(r.entity)) {
    mr->isVisible = true;
    mr->color.w = 0.6f;
  }
}

void GameJuiceSystem::UpdateRipples(core::GameContext &ctx) {
  for (auto &r : m_ripples) {
    if (r.lifetime <= 0.0f)
      continue;

    r.lifetime -= ctx.dt;
    float progress = 1.0f - (r.lifetime / r.maxLifetime);
    float ease = std::pow(progress, 0.85f);

    auto *t = ctx.world.Get<Transform>(r.entity);
    auto *mr = ctx.world.Get<MeshRenderer>(r.entity);
    if (t && mr) {
      float scaleMul = 1.0f + ease * 3.0f;
      t->scale = {r.startScale * scaleMul, 0.02f,
                  r.startScale * scaleMul};
      mr->color.w = (1.0f - ease) * 0.6f;
      if (r.lifetime <= 0.0f) {
        mr->isVisible = false;
      }
    }
  }
}

} // namespace game::systems
