#pragma once
/**
 * @file UIRenderSystem.h
 * @brief UIテキスト描画システム（リファクタリング版）
 */

#include "../../core/GameContext.h"
#include "../../graphics/TextRenderer.h"
#include "../components/UIText.h"
#include <algorithm>
#include <vector>


namespace game::systems {

/// @brief UIテキスト描画システム
/// @details TextRenderer を使用して UIText コンポーネントを描画
class UIRenderSystem {
public:
  /// @brief コンストラクタ
  /// @param renderer 共有 TextRenderer への参照
  explicit UIRenderSystem(graphics::TextRenderer &renderer)
      : m_renderer(renderer) {}

  /// @brief システム実行（ECS パイプラインから呼び出される）
  void operator()(core::GameContext &ctx) {
    if (!m_renderer.IsValid())
      return;

    // 1. 可視 UIText を収集
    std::vector<std::pair<ecs::Entity, const components::UIText *>> uiTexts;
    ctx.world.Query<components::UIText>().Each(
        [&](ecs::Entity e, const components::UIText &ui) {
          if (ui.visible) {
            uiTexts.push_back({e, &ui});
          }
        });

    // 2. レイヤーでソート（小さい順 = 奥から描画）
    std::sort(uiTexts.begin(), uiTexts.end(), [](const auto &a, const auto &b) {
      return a.second->layer < b.second->layer;
    });

    // 3. 描画開始
    m_renderer.BeginDraw();

    for (const auto &[entity, ui] : uiTexts) {
      // 描画領域を計算
      float w = (ui->width > 0) ? ui->width : m_renderer.GetWidth() - ui->x;
      float h = (ui->height > 0) ? ui->height : m_renderer.GetHeight() - ui->y;
      D2D1_RECT_F rect = D2D1::RectF(ui->x, ui->y, ui->x + w, ui->y + h);

      // テキスト描画
      m_renderer.RenderText(ui->text, rect, ui->style);
    }

    // 4. 描画終了
    m_renderer.EndDraw();
  }

private:
  graphics::TextRenderer &m_renderer;
};

} // namespace game::systems
