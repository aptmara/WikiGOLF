#pragma once
/**
 * @file UIButtonRenderSystem.h
 * @brief UIボタンの描画処理（背景色＋テキスト）
 */

#include "../../core/GameContext.h"
#include "../../ecs/World.h"
#include "../../graphics/TextRenderer.h"
#include "../components/UIButton.h"

namespace game::systems {

/// @brief UIボタン描画システム
class UIButtonRenderSystem {
public:
  explicit UIButtonRenderSystem(graphics::TextRenderer &renderer)
      : m_renderer(renderer) {}

  void operator()(core::GameContext &ctx) {
    if (!m_renderer.IsValid())
      return;

    m_renderer.BeginDraw();

    ctx.world.Query<components::UIButton>().Each(
        [&](ecs::Entity, const components::UIButton &btn) {
          if (!btn.visible)
            return;

          D2D1_RECT_F rect =
              D2D1::RectF(btn.x, btn.y, btn.x + btn.width, btn.y + btn.height);
          DirectX::XMFLOAT4 bgColor = btn.GetCurrentColor();

          // 背景描画（状態に応じた色）
          m_renderer.FillRect(rect, bgColor);

          // ラベル描画（中央揃え）
          graphics::TextStyle style = btn.textStyle;
          style.align = graphics::TextAlign::Center;

          // テキストを垂直中央に配置（簡易的に上下パディング）
          float textHeight = style.fontSize * 1.2f;
          float verticalOffset = (btn.height - textHeight) / 2.0f;
          D2D1_RECT_F textRect =
              D2D1::RectF(btn.x, btn.y + verticalOffset, btn.x + btn.width,
                          btn.y + btn.height - verticalOffset);
          m_renderer.RenderText(btn.label, textRect, style);
        });

    m_renderer.EndDraw();
  }

private:
  graphics::TextRenderer &m_renderer;
};

} // namespace game::systems
