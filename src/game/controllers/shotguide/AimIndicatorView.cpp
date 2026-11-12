/**
 * @file AimIndicatorView.cpp
 * @brief ボールの足元の向き表示の描画部品
*/

#include "ShotGuideViews.h"
#include "../../../core/GameContext.h"

namespace game::guide {

void AimIndicatorView::Initialize(core::GameContext &ctx) {
  m_bezel = CreateGuideQuad(ctx, m_owner, shape::kBezel);
  m_arrow = CreateGuideQuad(ctx, m_owner, shape::kAimArrow);
}

void AimIndicatorView::Update(core::GameContext &ctx,
                              const ShotGuideFrame &frame) {
  const ShotGuidePath &path = *frame.path;
  const ShotGuideStyle &style = *frame.style;

  // ボールの真下、地表すれすれに置く
  const DirectX::XMFLOAT3 &ball = path.Start().position;
  const float offset = style.markerLift - path.ballRadius;
  const DirectX::XMFLOAT3 ground{ball.x + path.startNormal.x * offset,
                                 ball.y + path.startNormal.y * offset,
                                 ball.z + path.startNormal.z * offset};

  // 目盛りは地面（北向き）に固定する。向きを回すと矢羽根だけが回り、
  // 目盛りとのずれで「どれだけ回したか」が見える。
  const DirectX::XMFLOAT3 north{0.0f, 0.0f, 1.0f};
  const float size = style.aimSize * frame.scale;
  PlaceGroundQuad(ctx, m_bezel, ground, path.startNormal, north, 0.0f, size, frame.palette.nearColor.WithAlpha(frame.fade * 0.7f));
  PlaceGroundQuad(ctx, m_arrow, ground, path.startNormal, frame.aimDirection,
                  0.0f, size,
                  frame.palette.accent.WithAlpha(frame.fade * 0.95f),
                  frame.aimPhase);
}

void AimIndicatorView::Hide(core::GameContext &ctx) {
  HideGuideQuad(ctx, m_bezel);
  HideGuideQuad(ctx, m_arrow);
}

void AimIndicatorView::Shutdown(core::GameContext &ctx) {
  m_owner.DestroyAll(ctx.world);
  m_bezel = UINT32_MAX;
  m_arrow = UINT32_MAX;
}

} // namespace game::guide
