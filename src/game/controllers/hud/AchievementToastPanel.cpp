/**
 * @file AchievementToastPanel.cpp
 * @brief 実績解除トーストパネルの実装
*/

#include "AchievementToastPanel.h"
#include "../../../core/GameContext.h"
#include "../../../ecs/World.h"
#include "../../../graphics/TextRenderer.h"
#include "../../components/UIText.h"
#include <algorithm>
#include <cmath>

namespace game::controllers::hud {
namespace {

constexpr float kPanelWidth = 300.0f;
constexpr float kPanelHeight = 64.0f;
constexpr float kMarginRight = 20.0f;
constexpr float kMarginBottom = 20.0f;
constexpr float kBaseX = 1280.0f - kPanelWidth - kMarginRight; // 右端から一定マージン
constexpr float kBaseY =
    720.0f - kMarginBottom - kPanelHeight; // 画面右下端に最新トーストを配置
constexpr float kSlotSpacing = 74.0f;
constexpr std::size_t kMaxSpacingDepth = 4; // これ以上は重ねて表示（画面外へはみ出させない）
constexpr float kVisibleDuration = 3.2f; // 先頭に立ってからフェードアウトを始めるまでの秒数
constexpr float kFadeDuration = 0.5f;
constexpr float kPositionLerpSpeed = 8.0f;
constexpr float kBrightnessLerpSpeed = 6.0f;
constexpr float kMinBrightness = 0.38f; // 奥のトーストが真っ黒にならない下限
constexpr float kDarkenPerDepth = 0.80f;
// UIRenderSystem等の通常パスでは描画しない（visible=falseで除外し、Render()で
// フレーム最後に手動描画することで他の全パス・全シーンオーバーレイより前面に出す）。
constexpr int kLayerAchievementToast = 950;

DirectX::XMFLOAT4 ScaleRgb(const DirectX::XMFLOAT4 &color, float brightness) {
  return {color.x * brightness, color.y * brightness, color.z * brightness,
          color.w};
}

float LerpTo(float current, float target, float speed, float dt) {
  const float t = std::clamp(speed * dt, 0.0f, 1.0f);
  return current + (target - current) * t;
}

} // namespace

void AchievementToastPanel::PushToast(core::GameContext &ctx,
                                      const std::wstring &name,
                                      const std::wstring &description) {
  ToastEntry entry;
  entry.name = name;
  entry.descriptionText = description;
  entry.currentY = kBaseY + 24.0f; // 画面下から滑り上がるように初期配置
  entry.currentBrightness = 1.0f;
  SpawnEntities(ctx, entry);
  m_active.insert(m_active.begin(), std::move(entry));
}

void AchievementToastPanel::SpawnEntities(core::GameContext &ctx,
                                          ToastEntry &entry) {
  entry.background = m_entityOwner.Create(ctx.world);
  auto &background = ctx.world.Add<game::components::UIText>(entry.background);
  background.x = kBaseX;
  background.y = entry.currentY;
  background.width = kPanelWidth;
  background.height = kPanelHeight;
  background.style.bgColor = {0.03f, 0.07f, 0.13f, 0.95f};
  background.style.borderColor = {0.82f, 0.68f, 0.28f, 1.0f};
  background.style.borderWidth = 2.0f;
  background.style.cornerRadius = 10.0f;
  background.visible = false; // Render()で個別に描画するため通常パスからは隠す
  background.layer = kLayerAchievementToast;

  entry.title = m_entityOwner.Create(ctx.world);
  auto &title = ctx.world.Add<game::components::UIText>(entry.title);
  title.text = L"実績解除: " + entry.name;
  title.x = kBaseX + 14.0f;
  title.y = entry.currentY + 8.0f;
  title.width = kPanelWidth - 28.0f;
  title.height = 24.0f;
  title.style.fontFamily = "Barlow Condensed SemiBold";
  title.style.fontSize = 19.0f;
  title.style.color = {1.0f, 0.9f, 0.55f, 1.0f};
  title.visible = false; // Render()で個別に描画するため通常パスからは隠す
  title.layer = kLayerAchievementToast + 1;

  entry.description = m_entityOwner.Create(ctx.world);
  auto &description = ctx.world.Add<game::components::UIText>(entry.description);
  description.text = entry.descriptionText;
  description.x = kBaseX + 14.0f;
  description.y = entry.currentY + 34.0f;
  description.width = kPanelWidth - 28.0f;
  description.height = 26.0f;
  description.style.fontFamily = "Kiwi Maru Medium";
  description.style.fontSize = 15.0f;
  description.style.color = {0.88f, 0.9f, 0.95f, 1.0f};
  description.visible = false; // Render()で個別に描画するため通常パスからは隠す
  description.layer = kLayerAchievementToast + 1;
}

void AchievementToastPanel::ApplyVisualState(core::GameContext &ctx,
                                              ToastEntry &entry,
                                              float targetY,
                                              float targetBrightness,
                                              float dt) {
  entry.currentY = LerpTo(entry.currentY, targetY, kPositionLerpSpeed, dt);
  entry.currentBrightness =
      LerpTo(entry.currentBrightness, targetBrightness, kBrightnessLerpSpeed, dt);

  const float alpha = entry.fadingOut ? (1.0f - entry.fadeProgress) : 1.0f;

  if (auto *background =
          ctx.world.Get<game::components::UIText>(entry.background)) {
    background->y = entry.currentY;
    const DirectX::XMFLOAT4 baseBg = {0.03f, 0.07f, 0.13f, 0.95f};
    background->style.bgColor = ScaleRgb(baseBg, entry.currentBrightness);
    background->style.bgColor.w = baseBg.w * alpha;
    const DirectX::XMFLOAT4 baseBorder = {0.82f, 0.68f, 0.28f, 1.0f};
    background->style.borderColor =
        ScaleRgb(baseBorder, entry.currentBrightness);
    background->style.borderColor.w = alpha;
  }
  if (auto *title = ctx.world.Get<game::components::UIText>(entry.title)) {
    title->y = entry.currentY + 8.0f;
    const DirectX::XMFLOAT4 baseColor = {1.0f, 0.9f, 0.55f, 1.0f};
    title->style.color = ScaleRgb(baseColor, entry.currentBrightness);
    title->style.color.w = alpha;
  }
  if (auto *description =
          ctx.world.Get<game::components::UIText>(entry.description)) {
    description->y = entry.currentY + 34.0f;
    const DirectX::XMFLOAT4 baseColor = {0.88f, 0.9f, 0.95f, 1.0f};
    description->style.color = ScaleRgb(baseColor, entry.currentBrightness);
    description->style.color.w = alpha;
  }
}

void AchievementToastPanel::Update(core::GameContext &ctx, float dt) {
  if (m_active.empty()) {
    return;
  }

  ToastEntry &front = m_active.front();
  front.age += dt;
  if (!front.fadingOut && front.age >= kVisibleDuration) {
    front.fadingOut = true;
  }
  if (front.fadingOut) {
    front.fadeProgress =
        std::min(1.0f, front.fadeProgress + dt / kFadeDuration);
  }

  for (std::size_t i = 0; i < m_active.size(); ++i) {
    const std::size_t clampedDepth = std::min(i, kMaxSpacingDepth);
    // 最新（depth=0）を画面下端に置き、古いトーストほど上へ積み上げる
    const float targetY = kBaseY - static_cast<float>(clampedDepth) * kSlotSpacing;
    const float targetBrightness =
        (i == 0) ? 1.0f
                 : std::max(kMinBrightness,
                            std::pow(kDarkenPerDepth, static_cast<float>(i)));
    ApplyVisualState(ctx, m_active[i], targetY, targetBrightness, dt);
  }

  if (front.fadeProgress >= 1.0f) {
    DestroyEntry(ctx, front);
    m_active.erase(m_active.begin());
  }
}

void AchievementToastPanel::Render(core::GameContext &ctx,
                                   graphics::TextRenderer &renderer) const {
  // UIText/UIImage/UIButton等の通常描画パス、さらにシーンごとのオーバーレイ
  // （RankingSceneの暗転矩形等）が全て描き終わった後に呼ばれることを前提に、
  // どの要素よりも手前へ直接描画する。
  for (const ToastEntry &entry : m_active) {
    const ecs::Entity parts[] = {entry.background, entry.title,
                                 entry.description};
    for (const ecs::Entity part : parts) {
      const auto *ui = ctx.world.Get<game::components::UIText>(part);
      if (!ui) {
        continue;
      }
      const D2D1_RECT_F rect = D2D1::RectF(ui->x, ui->y, ui->x + ui->width,
                                           ui->y + ui->height);
      renderer.RenderText(ui->text, rect, ui->style);
    }
  }
}

void AchievementToastPanel::DestroyEntry(core::GameContext &ctx,
                                          ToastEntry &entry) {
  ctx.world.DestroyEntity(entry.background);
  ctx.world.DestroyEntity(entry.title);
  ctx.world.DestroyEntity(entry.description);
}

void AchievementToastPanel::Shutdown(core::GameContext &ctx) {
  m_entityOwner.DestroyAll(ctx.world);
  m_active.clear();
}

} // namespace game::controllers::hud
