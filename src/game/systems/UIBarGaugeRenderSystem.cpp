#include "UIBarGaugeRenderSystem.h"
#include "../../ecs/World.h"
#include "../../graphics/TextRenderer.h"
#include "../components/WikiComponents.h"
#include "../utils/UIConstants.h"
#include <algorithm>
#include <d2d1_1.h>

namespace game::systems {

namespace {

float NormalizeGaugeValue(const game::components::UIBarGauge &gauge,
                          float value) {
  if (gauge.maxValue <= 0.0f) {
    return 0.0f;
  }
  return std::clamp(value / gauge.maxValue, 0.0f, 1.0f);
}

DirectX::XMFLOAT4 WithOpacity(const DirectX::XMFLOAT4 &c, float opacity) {
  return DirectX::XMFLOAT4{c.x, c.y, c.z, c.w * opacity};
}

void FillBorder(graphics::TextRenderer &renderer, const D2D1_RECT_F &rect,
                float width, float pulse, float opacity,
                const DirectX::XMFLOAT4 &color) {
  if (width <= 0.0f) {
    return;
  }
  // 確定直後だけ枠線を一瞬太くしてスナップ感を出す（発光は使わない）。
  const float pulseWidth = width + width * std::clamp(pulse, 0.0f, 1.0f) * 1.5f;
  renderer.DrawRoundedRect(rect, game::ui::kRadiusBar, WithOpacity(color, opacity),
                           pulseWidth);
}

// マーカー: 丸ヘッド+細いステムの「ピン」形状。暗い縁取り+単色の2層のみ。グローは持たせない。
void FillMarker(graphics::TextRenderer &renderer,
                const game::components::UIBarGauge &gauge, float value,
                float stemWidth, float topOverhang, float bottomOverhang,
                const DirectX::XMFLOAT4 &color, float opacity) {
  const float markerX = gauge.x + gauge.width * NormalizeGaugeValue(gauge, value);
  const float pulse = std::clamp(gauge.confirmPulse, 0.0f, 1.0f);
  const float headSize =
      stemWidth * 2.2f * (1.0f + game::ui::kGaugeMarkerPulseScale * pulse);
  const float headRadius = headSize * 0.5f;
  const float headTop = gauge.y - topOverhang;
  const float headCenterY = headTop + headRadius;
  const float stemBottom = gauge.y + gauge.height + bottomOverhang;
  const DirectX::XMFLOAT4 outline = WithOpacity({0.0f, 0.0f, 0.0f, 0.92f}, opacity);
  const DirectX::XMFLOAT4 body = WithOpacity(color, opacity);

  // 縁取り（暗色）: ステム→ヘッドの順に、本体より一回り大きく塗ってから重ねる。
  renderer.FillRoundedRect(
      D2D1::RectF(markerX - stemWidth * 0.5f - 1.5f, headCenterY,
                  markerX + stemWidth * 0.5f + 1.5f, stemBottom),
      3.0f, outline);
  renderer.FillRoundedRect(
      D2D1::RectF(markerX - headRadius - 1.5f, headTop - 1.5f,
                  markerX + headRadius + 1.5f, headTop + headSize + 1.5f),
      headRadius + 1.5f, outline);

  // 本体
  renderer.FillRoundedRect(
      D2D1::RectF(markerX - stemWidth * 0.5f, headCenterY,
                  markerX + stemWidth * 0.5f, stemBottom),
      2.0f, body);
  renderer.FillRoundedRect(
      D2D1::RectF(markerX - headRadius, headTop, markerX + headRadius,
                  headTop + headSize),
      headRadius, body);
}

// パワー閾値の目盛り線: 40%/75%の境界だけを細い1pxラインで示す。
// 半透明の帯のような装飾は持たせない。
void FillPowerTicks(graphics::TextRenderer &renderer,
                    const game::components::UIBarGauge &gauge, float opacity) {
  const float y = gauge.y;
  const float h = gauge.height;
  const float x = gauge.x;
  const float w = gauge.width;
  const float inset = 3.0f;
  const float tickWidth = 1.0f;
  const DirectX::XMFLOAT4 tickColor = WithOpacity(game::ui::kColorGaugeTick, opacity);

  auto drawTick = [&](float ratio) {
    const float tx = x + w * ratio;
    renderer.FillRect(D2D1::RectF(tx - tickWidth * 0.5f, y + inset,
                                  tx + tickWidth * 0.5f, y + h - inset),
                      tickColor);
  };
  drawTick(0.40f);
  drawTick(0.75f);
}

// インパクトゾーン: 高さを統一した帯で、共有パレット色のみを使う。
void FillImpactZones(graphics::TextRenderer &renderer,
                     const game::components::UIBarGauge &gauge, float opacity) {
  if (!gauge.showImpactZones) {
    return;
  }

  const float centerX = gauge.x + gauge.width * gauge.impactCenter;
  const float niceW = gauge.width * gauge.impactWidthNice;
  const float greatW = gauge.width * gauge.impactWidthGreat;
  const float specialW = gauge.width * gauge.impactWidthSpecial;
  const float top = gauge.y + 3.0f;
  const float bottom = gauge.y + gauge.height - 3.0f;

  auto zoneColor = [&](const DirectX::XMFLOAT4 &c, float alpha) {
    return DirectX::XMFLOAT4{c.x, c.y, c.z, alpha * opacity};
  };

  renderer.FillRoundedRect(
      D2D1::RectF(centerX - niceW * 0.5f, top, centerX + niceW * 0.5f, bottom),
      game::ui::kRadiusBar,
      zoneColor(game::ui::kColorAccent, game::ui::kGaugeZoneAlphaNice));
  renderer.FillRoundedRect(
      D2D1::RectF(centerX - greatW * 0.5f, top, centerX + greatW * 0.5f, bottom),
      game::ui::kRadiusBar,
      zoneColor(game::ui::kColorSuccess, game::ui::kGaugeZoneAlphaGreat));
  renderer.FillRoundedRect(
      D2D1::RectF(centerX - specialW * 0.5f, top, centerX + specialW * 0.5f,
                  bottom),
      game::ui::kRadiusBar,
      zoneColor(game::ui::kColorSpecial, game::ui::kGaugeZoneAlphaSpecial));
}

} // namespace

void UIBarGaugeRenderSystem::operator()(core::GameContext &ctx) {
  if (!ctx.textRenderer) {
    return;
  }

  ctx.textRenderer->BeginDraw();

  ctx.world.Query<game::components::UIBarGauge>().Each(
      [&](ecs::Entity, game::components::UIBarGauge &gauge) {
        if (!gauge.isVisible) {
          return;
        }

        const float opacity = std::clamp(gauge.opacity, 0.0f, 1.0f);

        D2D1_RECT_F bgRect = D2D1::RectF(
            gauge.x, gauge.y, gauge.x + gauge.width, gauge.y + gauge.height);
        ctx.textRenderer->FillRoundedRect(bgRect, game::ui::kRadiusBar,
                                          WithOpacity(gauge.bgColor, opacity));

        if (gauge.mode == game::components::UIBarGaugeMode::Power) {
          FillPowerTicks(*ctx.textRenderer, gauge, opacity);
          const float fillRatio = NormalizeGaugeValue(gauge, gauge.value);
          if (fillRatio > 0.0f) {
            const float fillRight = std::max(
                gauge.x + 5.0f, gauge.x + gauge.width * fillRatio);
            const float radius = std::min(
                game::ui::kRadiusBar, std::max(1.0f, (fillRight - gauge.x) * 0.5f));
            ctx.textRenderer->FillRoundedRect(
                D2D1::RectF(gauge.x + 2.0f, gauge.y + 2.0f, fillRight,
                            gauge.y + gauge.height - 2.0f),
                radius,
                WithOpacity({gauge.color.x, gauge.color.y, gauge.color.z, 0.88f},
                           opacity));
          }
        } else {
          FillImpactZones(*ctx.textRenderer, gauge, opacity);
          if (gauge.showConfirmedMarker) {
            FillMarker(*ctx.textRenderer, gauge, gauge.confirmedValue, 5.0f,
                       7.0f, 7.0f, gauge.confirmedMarkerColor, opacity);
          }
        }

        if (gauge.showMarker) {
          FillMarker(*ctx.textRenderer, gauge, gauge.markerValue, 6.0f, 8.0f,
                     8.0f, gauge.markerColor, opacity);
        }

        FillBorder(*ctx.textRenderer, bgRect, gauge.borderWidth,
                   gauge.confirmPulse, opacity, gauge.borderColor);
      });

  ctx.textRenderer->EndDraw();
}

} // namespace game::systems
