#pragma once
/**
 * @file UIBarGaugeRenderSystem.h
 * @brief UIBarGaugeコンポーネントを描画するシステム
 */

#include "../../core/GameContext.h"
#include "../../ecs/World.h"
#include "../../graphics/TextRenderer.h"
#include "../components/WikiComponents.h"
#include <d2d1_1.h>

namespace game::systems {

class UIBarGaugeRenderSystem {
public:
  void operator()(core::GameContext &ctx) {
    if (!ctx.textRenderer)
      return;

    auto &world = ctx.world;
    world.Query<game::components::UIBarGauge>().Each(
        [&](ecs::Entity entity, game::components::UIBarGauge &gauge) {
          if (!gauge.isVisible)
            return;

          // 1. 背景
          D2D1_RECT_F bgRect =
              D2D1::RectF(gauge.x, gauge.y, gauge.x + gauge.width,
                          gauge.y + gauge.height);
          ctx.textRenderer->FillRect(bgRect, gauge.bgColor);

          // 2. インパクトゾーン (ゴルフ特化)
          if (gauge.showImpactZones) {
            float centerX = gauge.x + gauge.width * gauge.impactCenter;

            // Niceゾーン (黄色)
            float niceW = gauge.width * gauge.impactWidthNice;
            D2D1_RECT_F niceRect =
                D2D1::RectF(centerX - niceW * 0.5f, gauge.y,
                            centerX + niceW * 0.5f, gauge.y + gauge.height);
            ctx.textRenderer->FillRect(niceRect, {1.0f, 1.0f, 0.0f, 0.5f});

            // Greatゾーン (赤色)
            float greatW = gauge.width * gauge.impactWidthGreat;
            D2D1_RECT_F greatRect =
                D2D1::RectF(centerX - greatW * 0.5f, gauge.y,
                            centerX + greatW * 0.5f, gauge.y + gauge.height);
            ctx.textRenderer->FillRect(greatRect, {1.0f, 0.2f, 0.2f, 0.8f});

            // Specialゾーン (さらに狭い - 白)
            // 仮: Greatの40%
            float specialW = greatW * 0.4f;
            D2D1_RECT_F specialRect =
                D2D1::RectF(centerX - specialW * 0.5f, gauge.y,
                            centerX + specialW * 0.5f, gauge.y + gauge.height);
            ctx.textRenderer->FillRect(specialRect, {1.0f, 1.0f, 1.0f, 0.9f});
          }

          // 3. バー本体 (値)
          float fillRatio = gauge.value / gauge.maxValue;
          if (fillRatio < 0.0f)
            fillRatio = 0.0f;
          if (fillRatio > 1.0f)
            fillRatio = 1.0f;

          if (fillRatio > 0.0f) {
            D2D1_RECT_F fillRect = D2D1::RectF(
                gauge.x, gauge.y, gauge.x + gauge.width * fillRatio,
                gauge.y + gauge.height);
            ctx.textRenderer->FillRect(fillRect, gauge.color);
          }

          // 4. マーカー
          if (gauge.showMarker) {
            float markerX =
                gauge.x + gauge.width * (gauge.markerValue / gauge.maxValue);
            float w = 4.0f;
            D2D1_RECT_F markerRect = D2D1::RectF(
                markerX - w * 0.5f, gauge.y - 5.0f, markerX + w * 0.5f,
                gauge.y + gauge.height + 5.0f);

            // 白枠黒中身など目立つように
            ctx.textRenderer->FillRect(markerRect,
                                       {0.0f, 0.0f, 0.0f, 1.0f}); // 黒背景

            // 少し縮めて中身
            markerRect.left += 1.0f;
            markerRect.right -= 1.0f;
            markerRect.top += 1.0f;
            markerRect.bottom -= 1.0f;
            ctx.textRenderer->FillRect(markerRect, gauge.markerColor);
          }

          // 5. 枠線 (簡易:
          // アウトライン用のFillRect実装がないため、四角形を4回描くか、諦めるか)
          // TextRenderer::FillRectしかないので、四辺を描く
          if (gauge.borderWidth > 0.0f) {
            float b = gauge.borderWidth;
            // 上
            ctx.textRenderer->FillRect(
                D2D1::RectF(bgRect.left, bgRect.top, bgRect.right,
                            bgRect.top + b),
                gauge.borderColor);
            // 下
            ctx.textRenderer->FillRect(
                D2D1::RectF(bgRect.left, bgRect.bottom - b, bgRect.right,
                            bgRect.bottom),
                gauge.borderColor);
            // 左
            ctx.textRenderer->FillRect(
                D2D1::RectF(bgRect.left, bgRect.top, bgRect.left + b,
                            bgRect.bottom),
                gauge.borderColor);
            // 右
            ctx.textRenderer->FillRect(
                D2D1::RectF(bgRect.right - b, bgRect.top, bgRect.right,
                            bgRect.bottom),
                gauge.borderColor);
          }
        });
  }
};

} // namespace game::systems
