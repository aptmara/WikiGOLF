/**
 * @file GhostBallView.cpp
 * @brief 道すじを走る光の玉の描画部品
*/

#include "ShotGuideViews.h"
#include "../../../core/GameContext.h"
#include "../../../ecs/World.h"
#include "../../components/MeshRenderer.h"
#include "../../components/Transform.h"

namespace game::guide {

void GhostBallView::Initialize(core::GameContext &ctx) {
  m_orb = CreateGuideQuad(ctx, m_owner, shape::kOrb);
}

void GhostBallView::Update(core::GameContext &ctx,
                           const ShotGuideFrame &frame) {
  auto *transform = ctx.world.Get<game::components::Transform>(m_orb);
  auto *renderer = ctx.world.Get<game::components::MeshRenderer>(m_orb);
  if (!transform || !renderer) {
    return;
  }
  const float alpha = frame.ghostAlpha * frame.fade;
  if (alpha <= 0.01f) {
    renderer->isVisible = false;
    return;
  }
  const ShotGuideSample sample = SamplePathAtArc(*frame.path, frame.ghostArc);
  transform->position = sample.position;
  const float size = frame.style->ghostSize * (0.5f + 0.5f * frame.scale);
  transform->scale = {size, size, size};
  renderer->color = frame.palette.accent.WithAlpha(alpha).ToFloat4();
  renderer->isVisible = true;
}

void GhostBallView::Hide(core::GameContext &ctx) { HideGuideQuad(ctx, m_orb); }

void GhostBallView::Shutdown(core::GameContext &ctx) {
  m_owner.DestroyAll(ctx.world);
  m_orb = UINT32_MAX;
}

} // namespace game::guide
