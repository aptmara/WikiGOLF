/**
 * @file AimDistancePanel.cpp
 * @brief AimDistancePanel の実装
*/

#include "AimDistancePanel.h"
#include "HudStyles.h"
#include "../../../core/GameContext.h"
#include "../../../ecs/World.h"
#include "../../components/UIImage.h"
#include "../../components/UIText.h"
#include "../../utils/UIConstants.h"
#include <algorithm>
#include <cmath>
#include <format>

namespace game::controllers::hud {

namespace {

/** @brief バーの塗り色をパワー割合に応じて選びます（ShotGaugePanelと同じ配色規則）。*/
DirectX::XMFLOAT4 FillColorForRatio(float ratio) {
  if (ratio >= 0.75f) {
    return game::ui::kColorWarning;
  }
  if (ratio >= 0.40f) {
    return game::ui::kColorSuccess;
  }
  return game::ui::kColorAccent;
}

void SetTextVisible(ecs::World &world, ecs::Entity entity, bool visible) {
  if (auto *text = world.Get<game::components::UIText>(entity)) {
    text->visible = visible;
  }
}

void SetImageVisible(ecs::World &world, ecs::Entity entity, bool visible) {
  if (auto *image = world.Get<game::components::UIImage>(entity)) {
    image->visible = visible;
  }
}

} // namespace

void AimDistancePanel::Initialize(core::GameContext &ctx) {
  const float x = game::ui::kAimGraphX;
  const float y = game::ui::kAimGraphY;
  const float w = game::ui::kAimGraphWidth;
  const float h = game::ui::kAimGraphHeight;

  m_entities.track = m_entityOwner.Create(ctx.world);
  auto &track = ctx.world.Add<game::components::UIText>(m_entities.track);
  track.x = x;
  track.y = y;
  track.width = w;
  track.height = h;
  ApplySurfaceStyle(track.style, game::ui::kRadiusBar);
  track.visible = false;
  track.layer = game::ui::kLayerShotPanel - 1;

  m_entities.fill = m_entityOwner.Create(ctx.world);
  auto &fill = ctx.world.Add<game::components::UIText>(m_entities.fill);
  fill.x = x + 2.0f;
  fill.y = y + h - 2.0f;
  fill.width = w - 4.0f;
  fill.height = 0.0f;
  fill.style.bgColor = game::ui::kColorAccent;
  fill.style.cornerRadius = game::ui::kRadiusBar * 0.7f;
  fill.visible = false;
  fill.layer = game::ui::kLayerShotPanel;

  m_entities.maxLabel = m_entityOwner.Create(ctx.world);
  auto &maxLabel = ctx.world.Add<game::components::UIText>(m_entities.maxLabel);
  maxLabel.x = x - 30.0f;
  maxLabel.y = y - 20.0f;
  maxLabel.width = w + 60.0f;
  maxLabel.height = 16.0f;
  maxLabel.style = graphics::TextStyle::CardLabel();
  maxLabel.style.align = graphics::TextAlign::Center;
  maxLabel.visible = false;
  maxLabel.layer = game::ui::kLayerShotPanel + 1;

  m_entities.valueLabel = m_entityOwner.Create(ctx.world);
  auto &valueLabel =
      ctx.world.Add<game::components::UIText>(m_entities.valueLabel);
  valueLabel.x = x - 30.0f;
  valueLabel.width = w + 60.0f;
  valueLabel.height = 16.0f;
  valueLabel.style = graphics::TextStyle::ShotPanelValue();
  valueLabel.style.align = graphics::TextAlign::Center;
  valueLabel.style.fontSize = game::ui::kAimGraphValueFont;
  valueLabel.style.hasShadow = true;
  valueLabel.style.shadowColor = {0.0f, 0.0f, 0.0f, 0.5f};
  valueLabel.visible = false;
  valueLabel.layer = game::ui::kLayerShotPanel + 2;

  // ピン距離の高さを示す横線（バーいっぱいの幅、細い帯）
  m_entities.pinLine = m_entityOwner.Create(ctx.world);
  auto &pinLine = ctx.world.Add<game::components::UIText>(m_entities.pinLine);
  pinLine.x = x - 4.0f;
  pinLine.width = w + 8.0f;
  pinLine.height = 3.0f;
  pinLine.style.bgColor = game::ui::kColorSpecial;
  pinLine.visible = false;
  pinLine.layer = game::ui::kLayerShotPanel + 1;

  // ピンマーカー: 既存の風向き矢印アイコンを横向きに倒して転用し、
  // バーの高さ位置を指す「刺さったピン」の代わりとする。
  m_entities.pinMarker = m_entityOwner.Create(ctx.world);
  auto &pinMarker =
      ctx.world.Add<game::components::UIImage>(m_entities.pinMarker);
  pinMarker = game::components::UIImage::Create("Assets/textures/ui_wind_arrow.png", 0, 0);
  pinMarker.width = game::ui::kAimGraphMarkerSize;
  pinMarker.height = game::ui::kAimGraphMarkerSize;
  pinMarker.rotation = -DirectX::XM_PIDIV2; // 左向き = バーを指す
  pinMarker.visible = false;
  pinMarker.layer = game::ui::kLayerShotPanel + 2;

  m_entities.pinLabel = m_entityOwner.Create(ctx.world);
  auto &pinLabel = ctx.world.Add<game::components::UIText>(m_entities.pinLabel);
  pinLabel.width = 80.0f;
  pinLabel.height = 16.0f;
  pinLabel.style = graphics::TextStyle::CardLabel();
  pinLabel.style.color = game::ui::kColorSpecial;
  pinLabel.style.align = graphics::TextAlign::Left;
  pinLabel.visible = false;
  pinLabel.layer = game::ui::kLayerShotPanel + 2;
}

void AimDistancePanel::Update(core::GameContext &ctx,
                              game::components::ShotState::Phase phase,
                              float currentPower, float confirmedPower,
                              const game::components::AimPinState *aimPin,
                              const ClubUIData &currentClub) {
  const bool powerPhase =
      phase == game::components::ShotState::Phase::PowerCharging;
  const bool impactPhase =
      phase == game::components::ShotState::Phase::ImpactTiming;
  const bool hasPin = aimPin && aimPin->active;
  const bool shouldShow = !m_manuallyHidden && hasPin &&
                          (powerPhase || impactPhase) &&
                          currentClub.baseCarryDistance > 0.0f;

  auto setAllVisible = [&](bool visible) {
    SetTextVisible(ctx.world, m_entities.track, visible);
    SetTextVisible(ctx.world, m_entities.fill, visible);
    SetTextVisible(ctx.world, m_entities.maxLabel, visible);
    SetTextVisible(ctx.world, m_entities.valueLabel, visible);
    SetTextVisible(ctx.world, m_entities.pinLine, visible);
    SetImageVisible(ctx.world, m_entities.pinMarker, visible);
    SetTextVisible(ctx.world, m_entities.pinLabel, visible);
  };

  if (!shouldShow) {
    setAllVisible(false);
    return;
  }
  setAllVisible(true);

  // ImpactTiming中は「強さ決定時のバー」＝確定パワーの高さで静止表示する。
  float ratio = powerPhase ? currentPower : confirmedPower;
  ratio = std::clamp(ratio, 0.0f, 1.0f);

  const float x = game::ui::kAimGraphX;
  const float y = game::ui::kAimGraphY;
  const float w = game::ui::kAimGraphWidth;
  const float h = game::ui::kAimGraphHeight;
  const float innerH = h - 4.0f;

  auto *fill = ctx.world.Get<game::components::UIText>(m_entities.fill);
  auto *maxLabel = ctx.world.Get<game::components::UIText>(m_entities.maxLabel);
  auto *valueLabel =
      ctx.world.Get<game::components::UIText>(m_entities.valueLabel);
  auto *pinLine = ctx.world.Get<game::components::UIText>(m_entities.pinLine);
  auto *pinMarker =
      ctx.world.Get<game::components::UIImage>(m_entities.pinMarker);
  auto *pinLabel = ctx.world.Get<game::components::UIText>(m_entities.pinLabel);
  if (!fill || !maxLabel || !valueLabel || !pinLine || !pinMarker ||
      !pinLabel) {
    return;
  }

  const float fillHeight = innerH * ratio;
  fill->height = fillHeight;
  fill->y = y + h - 2.0f - fillHeight;
  fill->style.bgColor = FillColorForRatio(ratio);

  const int maxYards =
      std::clamp(static_cast<int>(std::round(currentClub.baseCarryDistance)),
                0, 9999);
  maxLabel->text = std::format(L"最大 {}y", maxYards);

  const int currentYards = std::clamp(
      static_cast<int>(std::round(currentClub.baseCarryDistance * ratio)), 0,
      9999);
  valueLabel->text = std::format(L"{}y", currentYards);
  valueLabel->y = fill->y - 20.0f;
  valueLabel->style.color = FillColorForRatio(ratio);

  const float pinRatio =
      std::clamp(aimPin->distanceFromBall / currentClub.baseCarryDistance,
                0.0f, 1.0f);
  const float pinY = y + h - 2.0f - innerH * pinRatio;

  pinLine->y = pinY - pinLine->height * 0.5f;

  pinMarker->x = x + w + 6.0f;
  pinMarker->y = pinY - game::ui::kAimGraphMarkerSize * 0.5f;

  const int pinYards = std::clamp(
      static_cast<int>(std::round(aimPin->distanceFromBall)), 0, 9999);
  pinLabel->text = std::format(L"ピン {}y", pinYards);
  pinLabel->x = x + w + 6.0f + game::ui::kAimGraphMarkerSize + 4.0f;
  pinLabel->y = pinY - 8.0f;
}

void AimDistancePanel::SetVisible(core::GameContext &ctx, bool visible) {
  m_manuallyHidden = !visible;
  if (visible) {
    return; // 実際の表示可否は次回Updateの条件判定に委ねる
  }
  SetTextVisible(ctx.world, m_entities.track, false);
  SetTextVisible(ctx.world, m_entities.fill, false);
  SetTextVisible(ctx.world, m_entities.maxLabel, false);
  SetTextVisible(ctx.world, m_entities.valueLabel, false);
  SetTextVisible(ctx.world, m_entities.pinLine, false);
  SetImageVisible(ctx.world, m_entities.pinMarker, false);
  SetTextVisible(ctx.world, m_entities.pinLabel, false);
}

void AimDistancePanel::Shutdown(core::GameContext &ctx) {
  m_entityOwner.DestroyAll(ctx.world);
  m_entities = Entities{};
  m_manuallyHidden = false;
}

} // namespace game::controllers::hud
