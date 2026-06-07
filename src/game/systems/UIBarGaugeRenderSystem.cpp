#include "UIBarGaugeRenderSystem.h"
#include "../../ecs/World.h"
#include "../../graphics/TextRenderer.h"
#include "../components/WikiComponents.h"
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

void FillBorder(graphics::TextRenderer &renderer, const D2D1_RECT_F &rect,
                float width, const DirectX::XMFLOAT4 &color) {
  if (width <= 0.0f) {
    return;
  }

  renderer.FillRect(D2D1::RectF(rect.left, rect.top, rect.right,
                                rect.top + width),
                    color);
  renderer.FillRect(D2D1::RectF(rect.left, rect.bottom - width, rect.right,
                                rect.bottom),
                    color);
  renderer.FillRect(D2D1::RectF(rect.left, rect.top, rect.left + width,
                                rect.bottom),
                    color);
  renderer.FillRect(D2D1::RectF(rect.right - width, rect.top, rect.right,
                                rect.bottom),
                    color);
}

void FillMarker(graphics::TextRenderer &renderer,
                const game::components::UIBarGauge &gauge, float value,
                float width, float topOverhang, float bottomOverhang,
                const DirectX::XMFLOAT4 &color) {
  const float markerX = gauge.x + gauge.width * NormalizeGaugeValue(gauge, value);
  D2D1_RECT_F markerRect =
      D2D1::RectF(markerX - width * 0.5f, gauge.y - topOverhang,
                  markerX + width * 0.5f, gauge.y + gauge.height + bottomOverhang);

  renderer.FillRect(markerRect, {0.0f, 0.0f, 0.0f, 0.88f});
  markerRect.left += 1.0f;
  markerRect.right -= 1.0f;
  markerRect.top += 1.0f;
  markerRect.bottom -= 1.0f;
  renderer.FillRect(markerRect, color);
}

void FillPowerBands(graphics::TextRenderer &renderer,
                    const game::components::UIBarGauge &gauge) {
  const float y = gauge.y;
  const float h = gauge.height;
  const float x = gauge.x;
  const float w = gauge.width;

  renderer.FillRect(D2D1::RectF(x, y, x + w * 0.35f, y + h),
                    {0.12f, 0.54f, 0.90f, 0.26f});
  renderer.FillRect(D2D1::RectF(x + w * 0.35f, y, x + w * 0.72f, y + h),
                    {0.13f, 0.80f, 0.42f, 0.28f});
  renderer.FillRect(D2D1::RectF(x + w * 0.72f, y, x + w, y + h),
                    {1.00f, 0.76f, 0.16f, 0.30f});
}

void FillImpactZones(graphics::TextRenderer &renderer,
                     const game::components::UIBarGauge &gauge) {
  if (!gauge.showImpactZones) {
    return;
  }

  const float centerX = gauge.x + gauge.width * gauge.impactCenter;
  const float niceW = gauge.width * gauge.impactWidthNice;
  const float greatW = gauge.width * gauge.impactWidthGreat;
  const float specialW = gauge.width * gauge.impactWidthSpecial;

  renderer.FillRect(D2D1::RectF(centerX - niceW * 0.5f, gauge.y,
                                centerX + niceW * 0.5f,
                                gauge.y + gauge.height),
                    {0.18f, 0.60f, 1.00f, 0.22f});
  renderer.FillRect(D2D1::RectF(centerX - greatW * 0.5f, gauge.y,
                                centerX + greatW * 0.5f,
                                gauge.y + gauge.height),
                    {0.12f, 0.80f, 0.42f, 0.45f});
  renderer.FillRect(D2D1::RectF(centerX - specialW * 0.5f, gauge.y - 2.0f,
                                centerX + specialW * 0.5f,
                                gauge.y + gauge.height + 2.0f),
                    {1.00f, 0.86f, 0.24f, 0.78f});
}

void FillImpactDeviation(graphics::TextRenderer &renderer,
                         const game::components::UIBarGauge &gauge) {
  const float marker = NormalizeGaugeValue(gauge, gauge.markerValue);
  const float center = std::clamp(gauge.impactCenter, 0.0f, 1.0f);
  const float x1 = gauge.x + gauge.width * std::min(center, marker);
  const float x2 = gauge.x + gauge.width * std::max(center, marker);
  DirectX::XMFLOAT4 color = DirectX::XMFLOAT4{1.00f, 0.76f, 0.16f, 0.34f};
  if (std::abs(marker - center) <= 0.04f) {
    color = DirectX::XMFLOAT4{0.12f, 0.80f, 0.42f, 0.42f};
  }
  renderer.FillRect(D2D1::RectF(x1, gauge.y + 4.0f, x2,
                                gauge.y + gauge.height - 4.0f),
                    color);
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

        D2D1_RECT_F bgRect = D2D1::RectF(
            gauge.x, gauge.y, gauge.x + gauge.width, gauge.y + gauge.height);
        ctx.textRenderer->FillRect(bgRect, gauge.bgColor);

        if (gauge.mode == game::components::UIBarGaugeMode::Power) {
          FillPowerBands(*ctx.textRenderer, gauge);
          const float fillRatio = NormalizeGaugeValue(gauge, gauge.value);
          if (fillRatio > 0.0f) {
            ctx.textRenderer->FillRect(
                D2D1::RectF(gauge.x, gauge.y, gauge.x + gauge.width * fillRatio,
                            gauge.y + gauge.height),
                gauge.color);
          }
        } else {
          FillImpactZones(*ctx.textRenderer, gauge);
          FillImpactDeviation(*ctx.textRenderer, gauge);
          if (gauge.showConfirmedMarker) {
            FillMarker(*ctx.textRenderer, gauge, gauge.confirmedValue, 5.0f,
                       7.0f, 7.0f, gauge.confirmedMarkerColor);
          }
        }

        if (gauge.showMarker) {
          FillMarker(*ctx.textRenderer, gauge, gauge.markerValue, 6.0f, 8.0f,
                     8.0f, gauge.markerColor);
        }

        FillBorder(*ctx.textRenderer, bgRect, gauge.borderWidth,
                   gauge.borderColor);
      });

  ctx.textRenderer->EndDraw();
}

} // namespace game::systems
