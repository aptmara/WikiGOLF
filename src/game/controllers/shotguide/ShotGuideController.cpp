/**
 * @file ShotGuideController.cpp
 * @brief ショット予測表示の窓口の実装
*/

#include "ShotGuideController.h"
#include "../../../core/GameContext.h"
#include "../../systems/WikiTerrainSystem.h"
#include "ShotGuideViews.h"
#include <cmath>

namespace game::guide {

ShotGuideController::ShotGuideController() = default;
ShotGuideController::~ShotGuideController() = default;

void ShotGuideController::Initialize(core::GameContext &ctx) {
  if (m_initialized) {
    return;
  }
  // 描画部品の登録。足す・外すはここの1行だけ。
  // 手前に重ねたいものほど後ろに並べる。
  m_views.push_back(std::make_unique<AimIndicatorView>());
  m_views.push_back(std::make_unique<LandingZoneView>());
  m_views.push_back(std::make_unique<TrajectoryRibbonView>());
  m_views.push_back(std::make_unique<GhostBallView>());
  for (auto &view : m_views) {
    view->Initialize(ctx);
  }
  m_initialized = true;
}

void ShotGuideController::Update(core::GameContext &ctx,
                                 const ShotGuideInput &input) {
  if (!m_initialized) {
    return;
  }
  if (!input.visible) {
    Hide(ctx);
    return;
  }

  // 道すじは条件が変わったときだけ計算し直す
  const bool requestChanged = !m_hasCache ||
                              !(m_cachedRequest == input.request) ||
                              m_cachedTerrain != input.terrain ||
                              m_cachedObstacles != input.obstacles ||
                              m_cachedObstacleRevision != input.obstacleRevision;
  bool restart = !m_visible || input.contextKey != m_contextKey;
  if (requestChanged) {
    if (m_hasCache) {
      const float dx = input.request.start.x - m_cachedRequest.start.x;
      const float dy = input.request.start.y - m_cachedRequest.start.y;
      const float dz = input.request.start.z - m_cachedRequest.start.z;
      restart = restart || (dx * dx + dy * dy + dz * dz) > 0.3f * 0.3f;
    }
    m_path = BuildShotGuidePath(input.request, input.terrain, input.obstacles);
    m_zone = BuildShotGuideZone(m_path, input.spread, input.terrain,
                                input.request.flatGround);
    m_cachedRequest = input.request;
    m_cachedTerrain = input.terrain;
    m_cachedObstacles = input.obstacles;
    m_cachedObstacleRevision = input.obstacleRevision;
    m_hasCache = true;
  }
  m_contextKey = input.contextKey;

  if (!m_path.valid) {
    for (auto &view : m_views) {
      view->Hide(ctx);
    }
    m_visible = false;
    m_animation.Reset();
    return;
  }

  if (restart) {
    m_animation.Restart();
  }
  m_visible = true;
  // 動きの速さはゲージ操作中だけパワーに合わせる。待機中は見せるパワーが
  // 大きくても（フルスイングの予測でも）落ち着いた速さのままにする。
  m_animation.Advance(m_path, m_style, ctx.dt,
                      input.charging ? input.energy : 0.0f);

  ShotGuideFrame frame;
  frame.path = &m_path;
  frame.zone = &m_zone;
  frame.style = &m_style;
  frame.palette = PaletteFor(input.energy, input.charging);
  frame.aimDirection = input.request.direction;
  frame.time = m_animation.time;
  frame.fade = m_animation.fade;
  frame.energy = input.energy;
  frame.flowPhase = m_animation.flowPhase;
  frame.aimPhase = m_animation.aimPhase;
  frame.spin = m_animation.spin;
  frame.reveal = m_animation.reveal;
  frame.pop = m_animation.pop;
  frame.ghostArc = m_animation.GhostArc(m_path);
  frame.ghostAlpha = m_animation.GhostAlpha(m_path, m_style);
  frame.landingRipple = m_animation.landingRipple;
  frame.restRipple = m_animation.restRipple;
  frame.scale = GuideScaleFor(m_style, m_path.totalDistance);

  for (auto &view : m_views) {
    view->Update(ctx, frame);
  }
}

void ShotGuideController::Hide(core::GameContext &ctx) {
  if (!m_visible && !m_hasCache) {
    return;
  }
  for (auto &view : m_views) {
    view->Hide(ctx);
  }
  // 隠れている間に地形が作り直されることがあるので、道すじは持ち越さない
  m_visible = false;
  m_hasCache = false;
  m_animation.Reset();
}

void ShotGuideController::Shutdown(core::GameContext &ctx) {
  for (auto &view : m_views) {
    view->Shutdown(ctx);
  }
  m_views.clear();
  m_visible = false;
  m_hasCache = false;
  m_initialized = false;
}

} // namespace game::guide
