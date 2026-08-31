#pragma once
/**
 * @file UIRenderSystem.h
 * @brief UIチ（��スト描画システム���（�リファクタリング版）
 */

#include "../../core/GameContext.h"
#include "../../core/Profiler.h"
#include "../../graphics/TextRenderer.h"
#include "../components/UIText.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
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

    // 可視状態のUIテキストを収集
    std::vector<std::pair<ecs::Entity, const components::UIText *>> uiTexts;
    ctx.world.Query<components::UIText>().Each(
        [&](ecs::Entity e, const components::UIText &ui) {
          if (ui.visible) {
            uiTexts.push_back({e, &ui});
          }
        });

    // レイヤーごとにソート（背面から順に描画するため）
    std::sort(uiTexts.begin(), uiTexts.end(), [](const auto &a, const auto &b) {
      return a.second->layer < b.second->layer;
    });


    m_renderer.BeginDraw();

    std::unordered_set<ecs::Entity> seen;
    seen.reserve(uiTexts.size());
    size_t rasterHits = 0;
    size_t directDraws = 0;

    for (const auto &[entity, ui] : uiTexts) {
      if (ui->fullScreenCover) {
        // 仮想解像度のレターボックスを無視し、物理画面全体を塗りつぶす
        // （フェード/暗転オーバーレイ）。テキストやキャッシュ追跡は行わない。
        m_renderer.FillFullScreenRect(ui->style.bgColor);
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
      const bool stable = UpdateStability(entity, ui->text, ui->style, w, h);

      if (stable) {
        m_renderer.RenderTextCached(ui->text, rect, ui->style);
        ++rasterHits;
      } else {
        m_renderer.RenderText(ui->text, rect, ui->style);
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
  /// @brief 内容/スタイル/レイアウトサイズが連続して安定しているエンティティかどうかを追跡する
  struct EntityTextState {
    std::wstring text;
    graphics::TextStyle style;
    float width = -1.0f;
    float height = -1.0f;
    int stableFrames = 0;
  };

  static constexpr int kStableFrameThreshold = 3;

  /// @return ラスタキャッシュ描画を使ってよいほど安定しているか
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
