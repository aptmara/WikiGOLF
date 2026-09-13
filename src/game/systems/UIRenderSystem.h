#pragma once
/**
 * @file UIRenderSystem.h
 * @brief UIテキスト・画像統合描画システム
 */

#include "../../core/GameContext.h"
#include "../../core/Profiler.h"
#include "../../graphics/TextRenderer.h"
#include "../components/UIImage.h"
#include "../components/UIText.h"
#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>


namespace game::systems {

/**
 * @brief UIテキスト・画像統合描画システム
 * @details コンポーネント種別をまたいでlayer順に描画します。
*/
class UIRenderSystem {
public:
  /**
   * @brief コンストラクタ
   * @param renderer 共有 TextRenderer への参照
*/
  explicit UIRenderSystem(graphics::TextRenderer &renderer)
      : m_renderer(renderer) {}

  /** @brief システム実行（ECS パイプラインから呼び出される）*/
  void operator()(core::GameContext &ctx) {
    if (!m_renderer.IsValid())
      return;

    struct RenderItem {
      int layer = 0;
      int typeOrder = 0;
      ecs::Entity entity = 0;
      const components::UIText *text = nullptr;
      const components::UIImage *image = nullptr;
    };
    std::vector<RenderItem> items;
    ctx.world.Query<components::UIText>().Each(
        [&](ecs::Entity e, const components::UIText &ui) {
          if (ui.visible) {
            items.push_back({ui.layer, 1, e, &ui, nullptr});
          }
        });
    ctx.world.Query<components::UIImage>().Each(
        [&](ecs::Entity e, const components::UIImage &ui) {
          if (ui.visible && ui.HasTexture()) {
            items.push_back({ui.layer, 0, e, nullptr, &ui});
          }
        });

    // 同じレイヤーでは画像を先に描き、その上へ文字を重ねる。
    std::sort(items.begin(), items.end(), [](const auto &a, const auto &b) {
      if (a.layer != b.layer) {
        return a.layer < b.layer;
      }
      return a.typeOrder < b.typeOrder;
    });


    m_renderer.BeginDraw();

    std::unordered_set<ecs::Entity> seen;
    seen.reserve(items.size());
    size_t rasterHits = 0;
    size_t directDraws = 0;

    for (const auto &item : items) {
      if (item.image) {
        const auto *image = item.image;
        const float width = image->width > 0.0f ? image->width : 100.0f;
        const float height = image->height > 0.0f ? image->height : 100.0f;
        const auto rect = D2D1::RectF(image->x, image->y,
                                      image->x + width, image->y + height);
        const float opacity = image->alpha * image->opacity;
        if (image->textureSRV) {
          m_renderer.RenderImage(image->textureSRV, rect, opacity,
                                 image->rotation, image->grayscaleTint,
                                 image->tintColor);
        } else {
          std::string path = image->texturePath;
          if (path.find("Assets/") != 0) {
            path = "Assets/textures/" + path;
          }
          m_renderer.RenderImage(path, rect, opacity, image->rotation,
                                 image->grayscaleTint, image->tintColor);
        }
        continue;
      }

      const auto entity = item.entity;
      const auto *ui = item.text;
      graphics::TextStyle effectiveStyle = ui->style;
      const float opacity = std::clamp(ui->opacity, 0.0f, 1.0f);
      effectiveStyle.color.w *= opacity;
      effectiveStyle.shadowColor.w *= opacity;
      effectiveStyle.bgColor.w *= opacity;
      effectiveStyle.bgGradientEnd.w *= opacity;
      effectiveStyle.borderColor.w *= opacity;
      effectiveStyle.outlineColor.w *= opacity;

      if (ui->fullScreenCover) {
        // 仮想解像度のレターボックスを無視し、物理画面全体を塗りつぶす
        // （フェード/暗転オーバーレイ）。テキストやキャッシュ追跡は行わない。
        m_renderer.FillFullScreenRect(effectiveStyle.bgColor);
        seen.insert(entity);
        continue;
      }

      // 描画領域を計算
      float w = m_renderer.GetWidth() - ui->x;
      if (ui->width > 0) {
        w = ui->width;
      }
      float h = m_renderer.GetHeight() - ui->y;
      if (ui->height > 0) {
        h = ui->height;
      }
      D2D1_RECT_F rect = D2D1::RectF(ui->x, ui->y, ui->x + w, ui->y + h);

      seen.insert(entity);
      const bool stable =
          UpdateStability(entity, ui->text, effectiveStyle, w, h);

      if (stable) {
        m_renderer.RenderTextCached(ui->text, rect, effectiveStyle);
        ++rasterHits;
      } else {
        m_renderer.RenderText(ui->text, rect, effectiveStyle);
        ++directDraws;
      }
    }

    // 破棄・非表示になったエンティティの安定度追跡データを間引く
    for (auto it = m_entityStates.begin(); it != m_entityStates.end();) {
      if (seen.find(it->first) == seen.end()) {
        it = m_entityStates.erase(it);
      } else {
        ++it;
      }
    }

    core::Profiler::Instance().SetCounter("Text.RasterCacheDraws",
                                          static_cast<double>(rasterHits));
    core::Profiler::Instance().SetCounter("Text.DirectDraws",
                                          static_cast<double>(directDraws));

    m_renderer.EndDraw();
  }

private:
  /** @brief 内容/スタイル/レイアウトサイズが連続して安定しているエンティティかどうかを追跡する*/
  struct EntityTextState {
    std::wstring text;
    graphics::TextStyle style;
    float width = -1.0f;
    float height = -1.0f;
    int stableFrames = 0;
  };

  static constexpr int kStableFrameThreshold = 3;

  /**
   * @return ラスタキャッシュ描画を使ってよいほど安定しているか
*/
  bool UpdateStability(ecs::Entity entity, const std::wstring &text,
                       const graphics::TextStyle &style, float width,
                       float height) {
    auto it = m_entityStates.find(entity);
    if (it == m_entityStates.end()) {
      EntityTextState state;
      state.text = text;
      state.style = style;
      state.width = width;
      state.height = height;
      state.stableFrames = 0;
      m_entityStates.emplace(entity, std::move(state));
      return false;
    }

    auto &state = it->second;
    const bool unchanged = state.text == text && state.style == style &&
                           state.width == width && state.height == height;
    if (unchanged) {
      ++state.stableFrames;
    } else {
      state.text = text;
      state.style = style;
      state.width = width;
      state.height = height;
      state.stableFrames = 0;
    }

    return state.stableFrames >= kStableFrameThreshold;
  }

  graphics::TextRenderer &m_renderer;
  std::unordered_map<ecs::Entity, EntityTextState> m_entityStates;
};

} // namespace game::systems
