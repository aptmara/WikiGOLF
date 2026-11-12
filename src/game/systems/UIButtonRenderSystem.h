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

/** @brief UIボタン描画システム*/
class UIButtonRenderSystem {
public:
  explicit UIButtonRenderSystem(graphics::TextRenderer &renderer)
      : m_renderer(renderer) {}

  /**
   * @brief ボタン描画を実行します。
   * @param ctx ゲームコンテキスト
   */
  void operator()(core::GameContext &ctx) {
    if (!m_renderer.IsValid())
      return;

    m_renderer.BeginDraw();

    ctx.world.Query<components::UIButton>().Each(
        [&](ecs::Entity e, const components::UIButton &btn) {
          if (!btn.visible)
            return;

          D2D1_RECT_F rect =
              D2D1::RectF(btn.x, btn.y, btn.x + btn.width, btn.y + btn.height);
          DirectX::XMFLOAT4 bgColor = btn.GetCurrentColor();

          // 背景描画（状態に応じた色）
          m_renderer.FillRect(rect, bgColor);
          if (btn.noHintToggle) {
            if (btn.toggleOn) m_renderer.RenderButtonFire(rect, ctx.time);
            const DirectX::XMFLOAT4 accent = btn.toggleOn
                ? DirectX::XMFLOAT4{0.9f, 0.67f, 0.47f, 1.0f}
                : DirectX::XMFLOAT4{0.45f, 0.5f, 0.55f, 1.0f};
            m_renderer.DrawRoundedRect(rect, 3.0f, accent, 1.0f);
            auto track = D2D1::RectF(rect.right - 48, rect.top + 15,
                                    rect.right - 14, rect.top + 33);
            m_renderer.DrawRoundedRect(track, 9.0f, accent, 1.0f);
            float knobX = track.left + 4.0f;
            if (btn.toggleOn) knobX += 16.0f;
            m_renderer.FillRoundedRect(D2D1::RectF(knobX, track.top + 4,
                knobX + 10, track.top + 14), 5.0f, accent);
            auto labelStyle = btn.textStyle;
            labelStyle.align = graphics::TextAlign::Left;
            m_renderer.RenderText(btn.label, D2D1::RectF(rect.left + 14,
                rect.top + 9, rect.right - 96, rect.bottom), labelStyle);
            labelStyle.fontSize = 13;
            labelStyle.color = accent;
            m_renderer.RenderText(btn.toggleOn ? L"ON" : L"OFF",
                D2D1::RectF(rect.right - 84, rect.top + 16,
                            rect.right - 48, rect.bottom), labelStyle);
            if (btn.state == components::ButtonState::Hovered ||
                btn.state == components::ButtonState::Pressed) {
              const auto helpRect = D2D1::RectF(850, 24, 1240, 150);
              m_renderer.FillRoundedRect(helpRect, 6.0f,
                  DirectX::XMFLOAT4{0.03f, 0.07f, 0.12f, 0.96f});
              m_renderer.DrawRoundedRect(helpRect, 6.0f,
                  DirectX::XMFLOAT4{0.65f, 0.48f, 0.3f, 1.0f}, 1.0f);
              auto helpStyle = btn.textStyle;
              helpStyle.align = graphics::TextAlign::Left;
              helpStyle.fontSize = 17.0f;
              helpStyle.color = {0.95f, 0.92f, 0.85f, 1.0f};
              m_renderer.RenderText(
                  L"距離のヒントなしで挑戦。\n"
                  L"最短記事数のみ表示します。\n"
                  L"デイリーは常にヒントありで遊びます。",
                  D2D1::RectF(864, 38, 1226, 140), helpStyle);
            }
            return;
          }

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
