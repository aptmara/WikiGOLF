#pragma once
/**
 * @file UIImageRenderSystem.h
 * @brief UI逕ｻ蜒乗緒逕ｻ繧ｷ繧ｹ繝・Β
 */

#include "../../core/GameContext.h"
#include "../../graphics/TextRenderer.h"
#include "../components/UIImage.h"
#include <algorithm>
#include <string>
#include <vector>

namespace game::systems {

/// @brief UI逕ｻ蜒乗緒逕ｻ繧ｷ繧ｹ繝・Β
class UIImageRenderSystem {
public:
  explicit UIImageRenderSystem(graphics::TextRenderer &renderer)
      : m_renderer(renderer) {}

  void operator()(core::GameContext &ctx) {
    if (!m_renderer.IsValid())
      return;

    // 陦ｨ遉ｺ荳ｭ縺ｮUI逕ｻ蜒上ｒ蜿朱寔
    std::vector<const components::UIImage *> images;
    ctx.world.Query<components::UIImage>().Each(
        [&](ecs::Entity e, const components::UIImage &ui) {
          if (ui.visible) {
            images.push_back(&ui);
          }
        });

    // 繝ｬ繧､繝､繝ｼ縺斐→縺ｫ繧ｽ繝ｼ繝茨ｼ郁レ髱｢縺九ｉ鬆・↓謠冗判縺吶ｋ縺溘ａ・・    std::sort(images.begin(), images.end(),
              [](const auto *a, const auto *b) { return a->layer < b->layer; });


    m_renderer.BeginDraw();

    for (const auto *ui : images) {
      if (!ui->HasTexture()) {
        continue;
      }

      // 謠冗判鬆伜沺縺ｮ險育ｮ・
      float w = 100.0f; // 繝・ヵ繧ｩ繝ｫ繝医し繧､繧ｺ
      if (ui->width > 0) {
        w = ui->width;
      }
      float h = 100.0f;
      if (ui->height > 0) {
        h = ui->height;
      }
      D2D1_RECT_F rect = D2D1::RectF(ui->x, ui->y, ui->x + w, ui->y + h);

      if (ui->textureSRV) {
        // 譌｢蟄倥ユ繧ｯ繧ｹ繝√Ε縺ｧ謠冗判
        m_renderer.RenderImage(ui->textureSRV, rect, ui->alpha, ui->rotation);
      } else {
        // 繝輔ぃ繧､繝ｫ繝・け繧ｹ繝√Ε縺ｧ謠冗判
        std::string path;
        if (ui->texturePath.find("Assets/") == 0) {
          path = ui->texturePath;
        } else {
          path = "Assets/textures/" + ui->texturePath;
        }
        m_renderer.RenderImage(path, rect, ui->alpha, ui->rotation);
      }
    }

    m_renderer.EndDraw();
  }

private:
  graphics::TextRenderer &m_renderer;
};

} // namespace game::systems
