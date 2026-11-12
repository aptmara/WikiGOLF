/**
 * @file TrajectoryRibbonView.cpp
 * @brief 予測線の描画部品
*/

#include "ShotGuideViews.h"
#include "../../../core/GameContext.h"
#include <algorithm>
#include <cmath>

namespace game::guide {

namespace {
/** @brief 帯の本数の上限（ShotGuidePathOptions::maxPoints と合わせる）*/
constexpr size_t kMaxSegments = 200;
} // namespace

void TrajectoryRibbonView::Initialize(core::GameContext &ctx) {
  m_segments.reserve(kMaxSegments);
  m_xraySegments.reserve(kMaxSegments);
  for (size_t i = 0; i < kMaxSegments; ++i) {
    m_segments.push_back(CreateGuideQuad(ctx, m_owner, shape::kRibbonFlight));
  }
  for (size_t i = 0; i < kMaxSegments; ++i) {
    m_xraySegments.push_back(
        CreateGuideQuad(ctx, m_owner, shape::kRibbonFlight, true));
  }
}

void TrajectoryRibbonView::Update(core::GameContext &ctx,
                                  const ShotGuideFrame &frame) {
  const ShotGuidePath &path = *frame.path;
  const ShotGuideStyle &style = *frame.style;
  const auto &points = path.points;
  const size_t segmentCount =
      std::min(points.size() - 1, m_segments.size());

  // 線は手前から伸びる
  const float revealArc = EaseOutCubic(frame.reveal) * path.totalArc;
  // 色は「着地までのどのあたりか」で決める（転がり区間は別の色）
  const float flightArc =
      path.hasLanding ? std::max(0.01f, path.Landing().arc)
                      : std::max(0.01f, path.totalArc);

  const float dissolveLength =
      std::max(frame.zone->halfLength * 2.0f, path.totalArc * 0.06f);

  for (size_t i = 0; i < m_segments.size(); ++i) {
    if (i >= segmentCount || points[i].arc >= revealArc) {
      HideGuideQuad(ctx, m_segments[i]);
      HideGuideQuad(ctx, m_xraySegments[i]);
      continue;
    }
    const ShotGuidePoint &a = points[i];
    const ShotGuidePoint &b = points[i + 1];
    DirectX::XMFLOAT3 end = b.position;
    float endArc = b.arc;
    if (b.arc > revealArc && b.arc > a.arc) {
      const float t = (revealArc - a.arc) / (b.arc - a.arc);
      end = {a.position.x + (b.position.x - a.position.x) * t,
             a.position.y + (b.position.y - a.position.y) * t,
             a.position.z + (b.position.z - a.position.z) * t};
      endArc = revealArc;
    }
    const float midArc = (a.arc + endArc) * 0.5f;
    const bool rolling = a.grounded && b.grounded;

    GuideColor color =
        rolling ? frame.palette.rollColor
                : Mix(frame.palette.nearColor, frame.palette.farColor,
                      midArc / flightArc);
    float width = (rolling ? style.rollWidth : style.ribbonWidth) * frame.scale;

    // 光の玉が通るあたりは、線も明るく太くなる
    const float ghostDistance = (midArc - frame.ghostArc) / style.ghostGlowReach;
    const float glow =
        frame.ghostAlpha * std::exp(-ghostDistance * ghostDistance);
    color = Mix(color, GuideColor{1.0f, 1.0f, 1.0f, 1.0f}, glow * 0.6f);
    width *= 1.0f + glow * 0.45f;

    // ボールのすぐ先は薄くして、ボールと足元の表示を隠さない
    color.a = frame.fade *
              SmoothStep(0.12f * frame.scale, style.ribbonStartFade * frame.scale, midArc);
    // 先端は1点で終わらせず、止まりそうな範囲へ溶け込ませる
    color.a *= Lerp(1.0f, style.zoneEndFade,
                    SmoothStep(path.totalArc - dissolveLength, path.totalArc, midArc));

    const float shapeId = rolling ? shape::kRibbonRoll : shape::kRibbonFlight;
    const float pattern = a.arc - frame.flowPhase;
    PlaceRibbonQuad(ctx, m_segments[i], a.position, end, width, color, shapeId,
                    pattern);

    if (a.arc >= style.xrayStartArc * frame.scale) {
      PlaceRibbonQuad(ctx, m_xraySegments[i], a.position, end, width,
                      color.WithAlpha(color.a * style.xrayAlpha), shapeId,
                      pattern);
    } else {
      HideGuideQuad(ctx, m_xraySegments[i]);
    }
  }
}

void TrajectoryRibbonView::Hide(core::GameContext &ctx) {
  for (auto entity : m_segments) HideGuideQuad(ctx, entity);
  for (auto entity : m_xraySegments) HideGuideQuad(ctx, entity);
}

void TrajectoryRibbonView::Shutdown(core::GameContext &ctx) {
  m_owner.DestroyAll(ctx.world);
  m_segments.clear();
  m_xraySegments.clear();
}

} // namespace game::guide
