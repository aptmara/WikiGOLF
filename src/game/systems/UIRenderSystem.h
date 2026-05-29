#pragma once
/**
 * @file UIRenderSystem.h
 * @brief UI繝・く繧ｹ繝域緒逕ｻ繧ｷ繧ｹ繝・Β・医Μ繝輔ぃ繧ｯ繧ｿ繝ｪ繝ｳ繧ｰ迚茨ｼ・
 */

#include "../../core/GameContext.h"
#include "../../graphics/TextRenderer.h"
#include "../components/UIText.h"
#include <algorithm>
#include <vector>


namespace game::systems {

/// @brief UI繝・く繧ｹ繝域緒逕ｻ繧ｷ繧ｹ繝・Β
/// @details TextRenderer 繧剃ｽｿ逕ｨ縺励※ UIText 繧ｳ繝ｳ繝昴・繝阪Φ繝医ｒ謠冗判
class UIRenderSystem {
public:
  /// @brief 繧ｳ繝ｳ繧ｹ繝医Λ繧ｯ繧ｿ
  /// @param renderer 蜈ｱ譛・TextRenderer 縺ｸ縺ｮ蜿ら・
  explicit UIRenderSystem(graphics::TextRenderer &renderer)
      : m_renderer(renderer) {}

  /// @brief 繧ｷ繧ｹ繝・Β螳溯｡鯉ｼ・CS 繝代う繝励Λ繧､繝ｳ縺九ｉ蜻ｼ縺ｳ蜃ｺ縺輔ｌ繧具ｼ・  void operator()(core::GameContext &ctx) {
    if (!m_renderer.IsValid())
      return;

    // 蜿ｯ隕也憾諷九・UI繝・く繧ｹ繝医ｒ蜿朱寔
    std::vector<std::pair<ecs::Entity, const components::UIText *>> uiTexts;
    ctx.world.Query<components::UIText>().Each(
        [&](ecs::Entity e, const components::UIText &ui) {
          if (ui.visible) {
            uiTexts.push_back({e, &ui});
          }
        });

    // 繝ｬ繧､繝､繝ｼ縺斐→縺ｫ繧ｽ繝ｼ繝茨ｼ郁レ髱｢縺九ｉ鬆・↓謠冗判縺吶ｋ縺溘ａ・・    std::sort(uiTexts.begin(), uiTexts.end(), [](const auto &a, const auto &b) {
      return a.second->layer < b.second->layer;
    });


    m_renderer.BeginDraw();

    for (const auto &[entity, ui] : uiTexts) {
      // 謠冗判鬆伜沺繧定ｨ育ｮ・
      float w = m_renderer.GetWidth() - ui->x;
      if (ui->width > 0) {
        w = ui->width;
      }
      float h = m_renderer.GetHeight() - ui->y;
      if (ui->height > 0) {
        h = ui->height;
      }
      D2D1_RECT_F rect = D2D1::RectF(ui->x, ui->y, ui->x + w, ui->y + h);

      // 繝・く繧ｹ繝域緒逕ｻ
      m_renderer.RenderText(ui->text, rect, ui->style);
    }


    m_renderer.EndDraw();
  }

private:
  graphics::TextRenderer &m_renderer;
};

} // namespace game::systems
