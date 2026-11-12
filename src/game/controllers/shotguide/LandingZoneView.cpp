/**
 * @file LandingZoneView.cpp
 * @brief 「だいたいこのあたりに止まる」範囲の描画部品
*/

#include "ShotGuideViews.h"
#include "../../../core/GameContext.h"
#include <algorithm>
#include <cmath>

namespace game::guide {

namespace {

/** @brief ふちの点線の本数（BuildShotGuideZone のふちの点の数と合わせる）*/
constexpr size_t kRimSegments = 32;
/** @brief 点線の模様の周期[m]（ShotGuidePS.hlsl の RibbonRoll と揃える）*/
constexpr float kDotPeriod = 0.34f;

DirectX::XMFLOAT3 Lifted(const DirectX::XMFLOAT3 &ground, float lift) {
  return {ground.x, ground.y + lift, ground.z};
}

/** @brief ボール中心の位置から、地表すれすれの位置を求める*/
DirectX::XMFLOAT3 GroundPoint(const DirectX::XMFLOAT3 &ballCenter,
                              const DirectX::XMFLOAT3 &normal, float ballRadius,
                              float lift) {
  const float offset = lift - ballRadius;
  return {ballCenter.x + normal.x * offset, ballCenter.y + normal.y * offset,
          ballCenter.z + normal.z * offset};
}

} // namespace

void LandingZoneView::Initialize(core::GameContext &ctx) {
  m_fill = CreateGuideQuad(ctx, m_owner, shape::kDisc);
  m_restRipple = CreateGuideQuad(ctx, m_owner, shape::kRipple);
  m_landingRipple = CreateGuideQuad(ctx, m_owner, shape::kRipple);
  for (size_t i = 0; i < kRimSegments; ++i) {
    m_rim.push_back(CreateGuideQuad(ctx, m_owner, shape::kRibbonRoll));
  }
  for (size_t i = 0; i < kRimSegments; ++i) {
    m_rimXray.push_back(CreateGuideQuad(ctx, m_owner, shape::kRibbonRoll, true));
  }
  m_beam = CreateGuideQuad(ctx, m_owner, shape::kBeam);
}

void LandingZoneView::Update(core::GameContext &ctx,
                             const ShotGuideFrame &frame) {
  const ShotGuidePath &path = *frame.path;
  const ShotGuideZone &zone = *frame.zone;
  const ShotGuideStyle &style = *frame.style;

  // 範囲は線がだいたい届いてから、少し行き過ぎて戻る動きで広がる
  if (frame.pop <= 0.0f || !zone.valid) {
    Hide(ctx);
    return;
  }
  const float grow = 0.55f + 0.45f * EaseOutBack(frame.pop);
  const float appear = Saturate(frame.pop * 3.0f);

  const SurfaceMood mood = MoodFor(path.restMaterial);
  const float pulse = MarkerPulse(mood, frame.time);
  const GuideColor color = MarkerColorFor(path.restMaterial, frame.palette);
  // 上限時間までに止まらなかったときは、さらに不確かなので薄くする
  const float certainty = path.settled ? 1.0f : 0.6f;
  const float alpha = frame.fade * appear * pulse * certainty;
  const DirectX::XMFLOAT3 center = Lifted(zone.center, style.markerLift);

  // --- ふち: 地面に沿ってゆっくり回る点線 ---
  auto rimPoint = [&](size_t index) {
    const DirectX::XMFLOAT3 &p = zone.rim[index % zone.rim.size()];
    return DirectX::XMFLOAT3{
        zone.center.x + (p.x - zone.center.x) * grow,
        p.y + style.markerLift,
        zone.center.z + (p.z - zone.center.z) * grow};
  };
  const float rimWidth =
      style.zoneRimWidth * std::clamp(frame.scale * 1.6f, 0.6f, 1.0f);
  const float drift =
      std::fmod(frame.time * style.zoneRimFlow * style.flowSpeedIdle,
                kDotPeriod * 10.0f);
  const size_t rimCount = std::min(zone.rim.size(), m_rim.size());
  float perimeter = 0.0f;
  for (size_t i = 0; i < m_rim.size(); ++i) {
    if (i >= rimCount) {
      HideGuideQuad(ctx, m_rim[i]);
      HideGuideQuad(ctx, m_rimXray[i]);
      continue;
    }
    const DirectX::XMFLOAT3 from = rimPoint(i);
    const DirectX::XMFLOAT3 to = rimPoint(i + 1);
    const float pattern = perimeter - drift;
    PlaceRibbonQuad(ctx, m_rim[i], from, to, rimWidth, color.WithAlpha(alpha),
                    shape::kRibbonRoll, pattern);
    PlaceRibbonQuad(ctx, m_rimXray[i], from, to, rimWidth,
                    color.WithAlpha(alpha * style.xrayAlpha * 1.5f),
                    shape::kRibbonRoll, pattern);
    perimeter += path_detail::Distance(from, to);
  }

  // --- 内側: 小さい範囲だけ淡く塗る（大きいと平らな板が起伏に刺さる）---
  const float sizeAcross = zone.halfWidth * 2.0f * grow;
  const float sizeAlong = zone.halfLength * 2.0f * grow;
  if (std::max(sizeAcross, sizeAlong) <= style.zoneFillMaxSize) {
    PlaceGroundQuad(ctx, m_fill, center, zone.normal, zone.forward, 0.0f,
                    sizeAcross, color.WithAlpha(alpha), 0.0f, sizeAlong);
  } else {
    HideGuideQuad(ctx, m_fill);
  }

  // --- 光の柱: 芝や起伏にふちが隠れても、遠くからおおよその場所が分かる ---
  {
    const float height = style.beamHeight * (0.45f + 0.55f * frame.scale) *
                         EaseOutCubic(frame.pop);
    const DirectX::XMFLOAT3 top{center.x, center.y + height, center.z};
    const float phase = frame.time * 0.55f - std::floor(frame.time * 0.55f);
    const float width = style.beamWidth * std::max(frame.scale, 0.5f);
    PlaceRibbonQuad(ctx, m_beam, center, top, width,
                    color.WithAlpha(alpha * Lerp(0.30f, 0.70f, frame.scale)),
                    shape::kBeam, phase);
  }

  // --- 波紋: 光の玉が着地した瞬間と、止まった瞬間 ---
  if (frame.restRipple >= 0.0f) {
    const DirectX::XMFLOAT3 rest = GroundPoint(
        path.Rest().position, path.restNormal, path.ballRadius,
        style.markerLift);
    const float size = std::max(zone.halfWidth * 2.0f, 0.9f) * 1.5f;
    PlaceGroundQuad(ctx, m_restRipple, rest, path.restNormal, zone.forward,
                    0.0f, size, color.WithAlpha(frame.fade), frame.restRipple);
  } else {
    HideGuideQuad(ctx, m_restRipple);
  }
  if (frame.landingRipple >= 0.0f && path.hasLanding) {
    const DirectX::XMFLOAT3 landing =
        GroundPoint(path.Landing().position, path.landingNormal,
                    path.ballRadius, style.markerLift);
    const GuideColor landingColor =
        MarkerColorFor(path.landingMaterial, frame.palette);
    PlaceGroundQuad(ctx, m_landingRipple, landing, path.landingNormal,
                    zone.forward, 0.0f,
                    style.landingRippleSize * std::max(frame.scale, 0.45f),
                    landingColor.WithAlpha(frame.fade), frame.landingRipple);
  } else {
    HideGuideQuad(ctx, m_landingRipple);
  }
}

void LandingZoneView::Hide(core::GameContext &ctx) {
  for (auto entity : m_rim) HideGuideQuad(ctx, entity);
  for (auto entity : m_rimXray) HideGuideQuad(ctx, entity);
  HideGuideQuad(ctx, m_fill);
  HideGuideQuad(ctx, m_restRipple);
  HideGuideQuad(ctx, m_landingRipple);
  HideGuideQuad(ctx, m_beam);
}

void LandingZoneView::Shutdown(core::GameContext &ctx) {
  m_owner.DestroyAll(ctx.world);
  m_rim.clear();
  m_rimXray.clear();
}

} // namespace game::guide
