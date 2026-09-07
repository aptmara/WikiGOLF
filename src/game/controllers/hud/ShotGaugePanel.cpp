/**
 * @file ShotGaugePanel.cpp
 * @brief ショットゲージパネルの実装
*/

#include "ShotGaugePanel.h"
#include "ShotGaugePanelInternals.h"
#include "HudStyles.h"
#include "../../../core/GameContext.h"
#include "../../../core/StringUtils.h"
#include "../../../ecs/World.h"
#include "../../components/UIText.h"
#include "../../utils/ShotGaugeRules.h"
#include "../../utils/UIConstants.h"
#include <algorithm>
#include <cmath>


namespace game::controllers::hud {

void ShotGaugePanel::Initialize(core::GameContext &ctx) {
  auto createText = [&](ecs::Entity &destination) -> game::components::UIText & {
    destination = m_entityOwner.Create(ctx.world);
    return ctx.world.Add<game::components::UIText>(destination);
  };

  auto &background = createText(m_entities.background);
  background.x = game::ui::kShotPanelBgX;
  background.y = game::ui::kShotPanelBgY;
  background.width = game::ui::kShotPanelBgWidth;
  background.height = game::ui::kShotPanelBgHeight;
  ApplySurfaceStyle(background.style);
  background.visible = false;
  background.layer = game::ui::kLayerShotPanel - 2;

  auto &step = createText(m_entities.step);
  step.text = L"01 / 02";
  step.x = game::ui::kShotPanelX;
  step.y = game::ui::kShotPanelY - 22.0f;
  step.width = game::ui::kShotStepWidth;
  step.height = 24.0f;
  step.style = graphics::TextStyle::ShotPanelLabel();
  step.style.align = graphics::TextAlign::Center;
  step.style.fontSize = game::ui::kShotLabelFontSize;
  step.style.color = game::ui::kColorTextPrimary;
  step.style.bgColor = game::ui::kColorSurfaceRaised;
  step.style.borderColor = game::ui::kColorAccent;
  step.style.borderColor.w = 0.75f;
  step.style.borderWidth = game::ui::kBorderWidthThin;
  step.style.cornerRadius = game::ui::kRadiusChip;
  step.visible = false;
  step.layer = game::ui::kLayerShotPanel + 1;

  auto &title = createText(m_entities.title);
  title.text = L"パワー調整";
  title.x = game::ui::kShotPanelX + game::ui::kShotStepWidth + 12.0f;
  title.y = game::ui::kShotPanelY - 25.0f;
  title.width = 300.0f;
  title.height = 30.0f;
  title.style = graphics::TextStyle::ClubName();
  title.style.align = graphics::TextAlign::Left;
  title.style.fontSize = game::ui::kShotTitleFontSize;
  title.visible = false;
  title.layer = game::ui::kLayerShotPanel + 1;

  auto &hint = createText(m_entities.hint);
  hint.text = L"左クリックで確定　　右クリックでキャンセル";
  hint.x = game::ui::kShotPanelX + game::ui::kShotPanelWidth - 300.0f;
  hint.y = game::ui::kShotPanelY - 19.0f;
  hint.width = 300.0f;
  hint.height = 22.0f;
  hint.style = graphics::TextStyle::ShotPanelLabel();
  hint.style.align = graphics::TextAlign::Right;
  hint.style.fontSize = game::ui::kShotHintFontSize;
  hint.style.color = game::ui::kColorTextSub;
  hint.visible = false;
  hint.layer = game::ui::kLayerShotPanel + 1;

  auto initializeValue = [&](ecs::Entity &labelEntity,
                             ecs::Entity &valueEntity,
                             const std::wstring &labelText,
                             const std::wstring &valueText, float y,
                             float valueWidth) {
    auto &label = createText(labelEntity);
    label.text = labelText;
    label.x = game::ui::kShotPanelX;
    label.y = y;
    label.width = 120.0f;
    label.height = 22.0f;
    label.style = graphics::TextStyle::ShotPanelLabel();
    label.style.fontSize = game::ui::kShotLabelFontSize;
    label.visible = false;
    label.layer = game::ui::kLayerShotPanel;

    auto &value = createText(valueEntity);
    value.text = valueText;
    value.x = game::ui::kShotPanelX + game::ui::kShotPanelWidth - valueWidth;
    value.y = y;
    value.width = valueWidth;
    value.height = 22.0f;
    value.style = graphics::TextStyle::ShotPanelValue();
    value.style.fontSize = game::ui::kShotValueFontSize;
    value.visible = false;
    value.layer = game::ui::kLayerShotPanel + 1;
  };
  initializeValue(m_entities.powerLabel, m_entities.powerValue, L"パワー",
                  L"0%", game::ui::kShotPanelY + 38.0f, 180.0f);
  initializeValue(m_entities.accuracyLabel, m_entities.accuracyValue,
                  L"正確性", L"待機", game::ui::kShotPanelY + 64.0f,
                  220.0f);

  auto &club = createText(m_entities.club);
  club.text = L"ドライバー";
  club.x = game::ui::kShotPanelX;
  club.y = game::ui::kShotPanelY + 88.0f;
  club.width = game::ui::kShotPanelWidth;
  club.height = 18.0f;
  club.style = graphics::TextStyle::ClubName();
  club.style.align = graphics::TextAlign::Left;
  club.style.fontSize = game::ui::kBrowserSubFontSize;
  club.style.color = game::ui::kColorTextSub;
  club.visible = false;
  club.layer = game::ui::kLayerShotPanel;

  m_entities.gauge = m_entityOwner.Create(ctx.world);
  auto &gauge =
      ctx.world.Add<game::components::UIBarGauge>(m_entities.gauge);
  gauge.value = 0.0f;
  gauge.maxValue = 1.0f;
  gauge.color = game::ui::kColorWarning;
  gauge.bgColor = game::ui::kColorBgDark;
  gauge.borderColor = game::ui::kColorBorder;
  gauge.borderWidth = game::ui::kGaugeBorderWidth;
  gauge.x = game::ui::kShotPanelX;
  gauge.y = game::ui::kShotPanelY + 12.0f;
  gauge.width = game::ui::kShotPanelWidth;
  gauge.height = game::ui::kGaugeHeight;
  gauge.isVisible = false;
  gauge.mode = game::components::UIBarGaugeMode::Power;
  gauge.showMarker = true;
  gauge.markerColor = game::ui::kColorWhite;
  gauge.showConfirmedMarker = false;
  gauge.showImpactZones = false;
  gauge.impactCenter = 0.5f;
  gauge.impactWidthSpecial =
      game::utils::GetImpactZoneVisualWidth(game::ui::kThresholdSpecial);
  gauge.impactWidthGreat =
      game::utils::GetImpactZoneVisualWidth(game::ui::kThresholdGreat);
  gauge.impactWidthNice =
      game::utils::GetImpactZoneVisualWidth(game::ui::kThresholdNice);

  auto &judge = createText(m_entities.judge);
  judge.x = game::ui::kJudgeTextX;
  judge.y = game::ui::kJudgeTextY;
  judge.width = 200.0f;
  judge.height = 80.0f;
  judge.style = graphics::TextStyle::Guide();
  judge.style.fontSize = 34.0f;
  judge.style.align = graphics::TextAlign::Center;
  judge.visible = true;
  judge.layer = game::ui::kLayerJudge;
}

} // namespace game::controllers::hud

