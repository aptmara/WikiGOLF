/**
 * @file ShotGaugePanel.cpp
 * @brief ショットゲージパネルの実装
*/

#include "ShotGaugePanel.h"
#include "ShotGaugePanelInternals.h"
#include "../../../core/GameContext.h"
#include "../../../ecs/World.h"
#include "../../components/UIText.h"
#include "../../utils/ShotGaugeRules.h"
#include "../../utils/UIConstants.h"


namespace game::controllers::hud {

void ShotGaugePanel::Initialize(core::GameContext &ctx) {
  m_entities.gauge = m_entityOwner.Create(ctx.world);
  auto &gauge =
      ctx.world.Add<game::components::UIBarGauge>(m_entities.gauge);
  gauge.x = game::ui::kShotRulerX;
  gauge.y = game::ui::kShotRulerY;
  gauge.width = game::ui::kShotRulerWidth;
  gauge.height = game::ui::kShotRulerHeight;
  gauge.isVisible = false;
  gauge.hintLabel = L"右クリックで取消";
  gauge.impactWidthSpecial =
      game::utils::GetImpactZoneVisualWidth(game::ui::kThresholdSpecial);
  gauge.impactWidthGreat =
      game::utils::GetImpactZoneVisualWidth(game::ui::kThresholdGreat);
  gauge.impactWidthNice =
      game::utils::GetImpactZoneVisualWidth(game::ui::kThresholdNice);

  m_entities.judge = m_entityOwner.Create(ctx.world);
  auto &judge = ctx.world.Add<game::components::UIText>(m_entities.judge);
  judge.x = game::ui::kJudgeTextX;
  judge.y = game::ui::kJudgeTextY;
  judge.width = 200.0f;
  judge.height = 80.0f;
  judge.style = graphics::TextStyle::Guide();
  judge.style.fontFamily = "Barlow Condensed Black";
  judge.style.fontSize = 34.0f;
  judge.style.align = graphics::TextAlign::Center;
  judge.visible = true;
  judge.layer = game::ui::kLayerJudge;
}

} // namespace game::controllers::hud

