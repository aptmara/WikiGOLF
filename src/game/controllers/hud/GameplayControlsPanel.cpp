/**
 * @file GameplayControlsPanel.cpp
 * @brief 固定操作UIパネルの実装
*/

#include "GameplayControlsPanel.h"
#include "HudStyles.h"
#include "../../../core/GameContext.h"
#include "../../../ecs/World.h"
#include "../../components/UIText.h"
#include "../../utils/UIConstants.h"

namespace game::controllers::hud {
namespace {

void SetTextVisible(ecs::World &world, ecs::Entity entity, bool visible) {
  auto *text = world.Get<game::components::UIText>(entity);
  if (!text) {
    return;
  }
  text->visible = visible;
}

} // namespace

void GameplayControlsPanel::Initialize(core::GameContext &ctx) {
  m_shotButtonBackground = m_entityOwner.Create(ctx.world);
  auto &background =
      ctx.world.Add<game::components::UIText>(m_shotButtonBackground);
  background.x = game::ui::kShotBtnX;
  background.y = game::ui::kShotBtnY;
  background.width = game::ui::kShotBtnW;
  background.height = game::ui::kShotBtnH;
  background.style.bgColor = game::ui::kColorShotBtn;
  background.style.useGradient = false;
  background.style.cornerRadius = game::ui::kRadiusChip;
  background.style.borderWidth = game::ui::kBorderWidthThin;
  background.style.borderColor = game::ui::kColorShotBtnBorder;
  background.style.hasShadow = false;
  background.visible = true;
  background.layer = game::ui::kLayerShotButton;

  m_shotButtonText = m_entityOwner.Create(ctx.world);
  auto &buttonText =
      ctx.world.Add<game::components::UIText>(m_shotButtonText);
  buttonText.text = L"SHOT   SPACE / CLICK";
  buttonText.x = game::ui::kShotBtnX;
  buttonText.y = game::ui::kShotBtnY + 22.0f;
  buttonText.width = game::ui::kShotBtnW;
  buttonText.height = 30.0f;
  buttonText.style.fontFamily = "Meiryo";
  buttonText.style.fontSize = game::ui::kShotBtnFontSize;
  buttonText.style.align = graphics::TextAlign::Center;
  buttonText.style.color = game::ui::kColorTextPrimary;
  buttonText.style.hasShadow = false;
  buttonText.style.hasOutline = false;
  buttonText.style.bgColor = {0.0f, 0.0f, 0.0f, 0.0f};
  buttonText.style.borderWidth = 0.0f;
  buttonText.visible = true;
  buttonText.layer = game::ui::kLayerShotButton + 1;

  m_controlHint = m_entityOwner.Create(ctx.world);
  auto &hint = ctx.world.Add<game::components::UIText>(m_controlHint);
  hint.text = L"Q / E  CLUB     RMB  CAMERA     M  MAP";
  hint.x = game::ui::kControlHintX;
  hint.y = game::ui::kControlHintY;
  hint.width = game::ui::kControlHintW;
  hint.height = game::ui::kControlHintH;
  hint.style = graphics::TextStyle::BrowserSub();
  hint.style.fontSize = game::ui::kControlHintFont;
  hint.style.color = game::ui::kColorTextSub;
  ApplySurfaceStyle(hint.style, game::ui::kRadiusChip);
  hint.style.bgColor.w = 0.72f;
  hint.visible = true;
  hint.layer = game::ui::kLayerControlHint;
}

void GameplayControlsPanel::SetShotPhaseVisible(core::GameContext &ctx,
                                                bool shotPhase) {
  const bool visible = !shotPhase;
  SetVisible(ctx, visible);
}

void GameplayControlsPanel::SetVisible(core::GameContext &ctx, bool visible) {
  SetTextVisible(ctx.world, m_shotButtonBackground, visible);
  SetTextVisible(ctx.world, m_shotButtonText, visible);
  SetTextVisible(ctx.world, m_controlHint, visible);
}

void GameplayControlsPanel::Shutdown(core::GameContext &ctx) {
  m_entityOwner.DestroyAll(ctx.world);
  m_shotButtonBackground = UINT32_MAX;
  m_shotButtonText = UINT32_MAX;
  m_controlHint = UINT32_MAX;
}

} // namespace game::controllers::hud
