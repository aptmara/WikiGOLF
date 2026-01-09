#pragma once
/**
 * @file UIImageRenderSystem.h
 * @brief UI画像描画システム
 */

#include "../../core/GameContext.h"
#include "../../graphics/TextRenderer.h"
#include "../components/UIImage.h"
#include <algorithm>
#include <string>
#include <vector>

namespace game::systems {

/// @brief UI画像描画システム
class UIImageRenderSystem {
public:
  explicit UIImageRenderSystem(graphics::TextRenderer &renderer)
      : m_renderer(renderer) {}

  void operator()(core::GameContext &ctx) {
    if (!m_renderer.IsValid())
      return;

    // 1. 可視 UIImage を収集
    std::vector<const components::UIImage *> images;
    ctx.world.Query<components::UIImage>().Each(
        [&](ecs::Entity e, const components::UIImage &ui) {
          if (ui.visible) {
            images.push_back(&ui);
          }
        });

    // 2. レイヤーでソート（小さい順 = 奥から描画）
    std::sort(images.begin(), images.end(),
              [](const auto *a, const auto *b) { return a->layer < b->layer; });

    // 3. 描画
    m_renderer.BeginDraw();

    for (const auto *ui : images) {
      // 描画領域を計算
      float w = (ui->width > 0) ? ui->width : 100.0f; // デフォルトサイズ
      float h = (ui->height > 0) ? ui->height : 100.0f;
      D2D1_RECT_F rect = D2D1::RectF(ui->x, ui->y, ui->x + w, ui->y + h);

      if (ui->textureSRV) {
        // 動的テクスチャ描画
        m_renderer.RenderImage(ui->textureSRV, rect, ui->alpha, ui->rotation);
      } else {
        // ファイルテクスチャ描画
        std::string path = "Assets/textures/" + ui->texturePath;
        m_renderer.RenderImage(path, rect, ui->alpha, ui->rotation);
      }
    }

    m_renderer.EndDraw();
  }

private:
  graphics::TextRenderer &m_renderer;
};

} // namespace game::systems
