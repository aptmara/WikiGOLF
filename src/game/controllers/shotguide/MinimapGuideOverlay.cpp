/**
 * @file MinimapGuideOverlay.cpp
 * @brief ミニマップに重ねるショット予測の実装
*/

#include "MinimapGuideOverlay.h"
#include "../../../core/GameContext.h"
#include "../../../ecs/World.h"
#include "../../components/UIImage.h"
#include <algorithm>
#include <cmath>

namespace game::guide {

using game::components::UIImage;

namespace {

constexpr int kDashCount = 40;
constexpr float kDashLength = 7.0f;    ///< 破線1本の長さ[px]
constexpr float kDashGap = 4.5f;       ///< 破線の間[px]
constexpr float kDashThickness = 4.5f; ///< 破線の太さ[px]（縁取り込み）
constexpr float kFlowSpeed = 15.0f;    ///< 破線が流れる速さ[px/秒]
constexpr float kStartClear = 7.0f;    ///< ボールのアイコンと重ねない距離[px]
constexpr float kZoneMinSize = 11.0f;  ///< 範囲のだ円の最小の大きさ[px]
constexpr float kRadToDeg = 57.29578f; ///< UIImage の回転は度数

ecs::Entity CreateImage(core::GameContext &ctx, ecs::EntityOwner &owner,
                        const char *texture, int layer) {
  const ecs::Entity entity = owner.Create(ctx.world);
  auto &image = ctx.world.Add<UIImage>(entity);
  image = UIImage::Create(texture, 0.0f, 0.0f);
  image.layer = layer;
  image.visible = false;
  return entity;
}

void HideImage(core::GameContext &ctx, ecs::Entity entity) {
  if (auto *image = ctx.world.Get<UIImage>(entity)) {
    image->visible = false;
  }
}

} // namespace

void MinimapGuideOverlay::Initialize(core::GameContext &ctx, int layer) {
  if (!m_dashes.empty()) {
    Shutdown(ctx); // 作り直しのときに二重に持たない
  }
  m_dashes.reserve(kDashCount);
  for (int i = 0; i < kDashCount; ++i) {
    m_dashes.push_back(CreateImage(ctx, m_owner, "ui_guide_dash.png", layer));
  }
  m_zone = CreateImage(ctx, m_owner, "ui_guide_zone.png", layer + 1);
  m_polyline.reserve(256);
}

void MinimapGuideOverlay::Update(core::GameContext &ctx,
                                 const ShotGuidePath *path,
                                 const ShotGuideZone *zone,
                                 const Projector &projector) {
  if (!path || !path->valid || !projector) {
    Hide(ctx);
    return;
  }

  // 道すじを画面上の折れ線へ。地図の外へ出たら、そこで打ち切る
  m_polyline.clear();
  for (size_t i = 0; i < path->points.size(); ++i) {
    ScreenPoint point;
    if (!projector(path->points[i].position, point.x, point.y)) {
      break;
    }
    if (!m_polyline.empty()) {
      const ScreenPoint &prev = m_polyline.back();
      const float step = std::hypot(point.x - prev.x, point.y - prev.y);
      if (step < 0.05f && i + 1 < path->points.size()) {
        continue; // 画面上でほぼ同じ位置の点は省く
      }
      point.length = prev.length + step;
    }
    m_polyline.push_back(point);
  }
  if (m_polyline.size() < 2) {
    Hide(ctx);
    return;
  }
  const float total = m_polyline.back().length;

  // 画面上の長さ s の位置と向きを返す
  size_t cursor = 1;
  auto sample = [&](float s, float &x, float &y, float &angle) {
    while (cursor + 1 < m_polyline.size() && m_polyline[cursor].length < s) {
      ++cursor;
    }
    const ScreenPoint &a = m_polyline[cursor - 1];
    const ScreenPoint &b = m_polyline[cursor];
    const float span = b.length - a.length;
    const float t = span > 1.0e-4f ? std::clamp((s - a.length) / span, 0.0f, 1.0f) : 0.0f;
    x = a.x + (b.x - a.x) * t;
    y = a.y + (b.y - a.y) * t;
    angle = std::atan2(b.y - a.y, b.x - a.x);
  };

  // 破線。長い線は間隔を広げて、用意した本数に収める
  float period = kDashLength + kDashGap;
  const float usable = std::max(0.0f, total - kStartClear);
  if (usable / period > static_cast<float>(kDashCount - 1)) {
    period = usable / static_cast<float>(kDashCount - 1);
  }
  const float dashLength = period * (kDashLength / (kDashLength + kDashGap));
  const float offset = std::fmod(ctx.time * kFlowSpeed, period);

  size_t used = 0;
  for (float start = kStartClear + offset - period;
       start < total && used < m_dashes.size(); start += period) {
    const float from = std::max(start, kStartClear);
    const float to = std::min(start + dashLength, total);
    if (to - from < 1.0f) {
      continue;
    }
    auto *image = ctx.world.Get<UIImage>(m_dashes[used]);
    if (!image) {
      ++used;
      continue;
    }
    float x = 0.0f, y = 0.0f, angle = 0.0f;
    sample((from + to) * 0.5f, x, y, angle);
    // 画像は両端が丸い棒。丸みのぶんだけ長めに置く
    const float width = (to - from) + kDashThickness * 0.5f;
    image->width = width;
    image->height = kDashThickness;
    image->x = x - width * 0.5f;
    image->y = y - kDashThickness * 0.5f;
    image->rotation = angle * kRadToDeg;
    // 手前は控えめに、先へ行くほどはっきり
    image->alpha = 0.55f + 0.45f * std::clamp(from / std::max(total, 1.0f), 0.0f, 1.0f);
    image->visible = true;
    ++used;
  }
  for (size_t i = used; i < m_dashes.size(); ++i) {
    HideImage(ctx, m_dashes[i]);
  }

  // 止まりそうな範囲。だ円の向きと大きさを画面上へ写す
  bool zoneShown = false;
  if (zone && zone->valid) {
    const DirectX::XMFLOAT3 &c = zone->center;
    const DirectX::XMFLOAT3 ahead{c.x + zone->forward.x * zone->halfLength, c.y,
                                  c.z + zone->forward.z * zone->halfLength};
    const DirectX::XMFLOAT3 side{c.x + zone->forward.z * zone->halfWidth, c.y,
                                 c.z - zone->forward.x * zone->halfWidth};
    float cx = 0.0f, cy = 0.0f, ax = 0.0f, ay = 0.0f, sx = 0.0f, sy = 0.0f;
    auto *image = ctx.world.Get<UIImage>(m_zone);
    if (image && projector(c, cx, cy) && projector(ahead, ax, ay) &&
        projector(side, sx, sy)) {
      const float pulse = 1.0f + 0.06f * std::sin(ctx.time * 3.2f);
      const float along =
          std::max(kZoneMinSize, 2.0f * std::hypot(ax - cx, ay - cy)) * pulse;
      const float across =
          std::max(kZoneMinSize, 2.0f * std::hypot(sx - cx, sy - cy)) * pulse;
      image->width = along;
      image->height = across;
      image->x = cx - along * 0.5f;
      image->y = cy - across * 0.5f;
      image->rotation = std::atan2(ay - cy, ax - cx) * kRadToDeg;
      image->alpha = 0.95f;
      image->visible = true;
      zoneShown = true;
    }
  }
  if (!zoneShown) {
    HideImage(ctx, m_zone);
  }
}

void MinimapGuideOverlay::Hide(core::GameContext &ctx) {
  for (auto entity : m_dashes) HideImage(ctx, entity);
  HideImage(ctx, m_zone);
}

void MinimapGuideOverlay::Shutdown(core::GameContext &ctx) {
  m_owner.DestroyAll(ctx.world);
  m_dashes.clear();
  m_zone = UINT32_MAX;
}

} // namespace game::guide
