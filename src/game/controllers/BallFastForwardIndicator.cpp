/**
 * @file BallFastForwardIndicator.cpp
 * @brief BallFastForwardIndicatorの実装
*/

#include "BallFastForwardIndicator.h"
#include "../../ecs/World.h"
#include "../components/UIImage.h"
#include "../utils/UIConstants.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>

namespace game::controllers {

using game::components::UIImage;
using game::utils::FastForwardTier;

namespace {

/**
 * @brief 倍速段階に対応するテクスチャファイル名（Assets/textures/配下）を返します。
 * @details ui_speedup_1_5x.png / ui_speedup_2_0x.png（Assets/textures/）を使用する。
 *          画像素材が未配置でもクラッシュしないよう、実ファイルの存在を
 *          確認したうえで見つからなければ空文字（=非表示）を返す。
*/
std::string ResolveIndicatorTexturePath(FastForwardTier tier) {
  const char *preferred = nullptr;
  switch (tier) {
  case FastForwardTier::Speed1_5x:
    preferred = "ui_speedup_1_5x.png";
    break;
  case FastForwardTier::Speed2_0x:
    preferred = "ui_speedup_2_0x.png";
    break;
  case FastForwardTier::Normal:
  default:
    return "";
  }

  if (std::filesystem::exists(std::string("Assets/textures/") + preferred)) {
    return preferred;
  }
  return "";
}

} // namespace

void BallFastForwardIndicator::Initialize(core::GameContext &ctx, ecs::Entity entity) {
  m_entity = entity;
  m_displayedTier = FastForwardTier::Normal;
  m_visibleSeconds = 0.0f;
  m_currentAlpha = 0.0f;

  auto *ui = ctx.world.Get<UIImage>(m_entity);
  if (!ui) return;

  ui->texturePath = "";
  ui->visible = false;
  ui->alpha = 0.0f;
  ui->width = game::ui::kFastForwardIndicatorW;
  ui->height = game::ui::kFastForwardIndicatorH;
  ui->x = game::ui::kFastForwardIndicatorX - ui->width * 0.5f;
  ui->y = game::ui::kFastForwardIndicatorY;
  ui->rotation = 0.0f;
  ui->layer = game::ui::kLayerFastForward;
}

void BallFastForwardIndicator::Shutdown() {
  m_entity = UINT32_MAX;
  m_displayedTier = FastForwardTier::Normal;
  m_visibleSeconds = 0.0f;
  m_currentAlpha = 0.0f;
}

void BallFastForwardIndicator::Update(core::GameContext &ctx, float dt, FastForwardTier tier) {
  if (m_entity == UINT32_MAX) return;
  auto *ui = ctx.world.Get<UIImage>(m_entity);
  if (!ui) return;

  if (tier != m_displayedTier) {
    // 段階が切り替わった瞬間: テクスチャを差し替えて登場アニメーションをやり直す。
    m_displayedTier = tier;
    m_visibleSeconds = 0.0f;

    if (tier == FastForwardTier::Normal) {
      ui->texturePath = "";
    } else {
      const std::string tex = ResolveIndicatorTexturePath(tier);
      if (!tex.empty()) {
        ui->texturePath = tex;
      }
      // 画像素材が見つからない場合はtexturePathを更新しない
      // （下のshouldShow判定でtexturePathが空なら自動的に非表示のままになる）
    }
  }

  const bool shouldShow = (tier != FastForwardTier::Normal) && !ui->texturePath.empty();
  m_visibleSeconds += dt;

  const float fadeStep = game::ui::kFastForwardFadeSpeed * dt;
  const float targetAlpha = shouldShow ? 1.0f : 0.0f;
  m_currentAlpha += std::clamp(targetAlpha - m_currentAlpha, -fadeStep, fadeStep);
  m_currentAlpha = std::clamp(m_currentAlpha, 0.0f, 1.0f);

  ui->alpha = m_currentAlpha;
  ui->visible = m_currentAlpha > 0.001f;

  if (ui->visible) {
    // 表示中はゆっくり脈動させ、待機中であることに気付きやすくする
    const float pulse = 1.0f + std::sin(m_visibleSeconds * 3.0f) * 0.03f;
    ui->width = game::ui::kFastForwardIndicatorW * pulse;
    ui->height = game::ui::kFastForwardIndicatorH * pulse;
    ui->x = game::ui::kFastForwardIndicatorX - ui->width * 0.5f;
    ui->y = game::ui::kFastForwardIndicatorY;
  }
}

} // namespace game::controllers
