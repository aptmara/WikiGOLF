/**
 * @file CourseInfoPanel.cpp
 * @brief コース情報パネルの実装
*/

#include "CourseInfoPanel.h"
#include "HudStyles.h"
#include "../../../core/GameContext.h"
#include "../../../core/StringUtils.h"
#include "../../../ecs/World.h"
#include "../../components/UIText.h"
#include "../../components/WikiComponents.h"
#include "../../utils/UIConstants.h"
#include <string>

namespace game::controllers::hud {
namespace {

void SetTextIfChanged(game::components::UIText &text,
                      const std::wstring &value) {
  if (text.text == value) {
    return;
  }
  text.text = value;
}

void SetTextVisible(ecs::World &world, ecs::Entity entity, bool visible) {
  if (!ecs::IsValidEntity(entity)) {
    return;
  }

  auto *text = world.Get<game::components::UIText>(entity);
  if (!text) {
    return;
  }
  text->visible = visible;
}

} // namespace

void CourseInfoPanel::Initialize(core::GameContext &ctx) {
  const float x = game::ui::kBrowserHudX;
  const float y = game::ui::kBrowserHudY;
  constexpr float kPanelWidth = 372.0f;
  constexpr float kPanelHeight = 150.0f;

  m_entities.background = m_entityOwner.Create(ctx.world);
  auto &background =
      ctx.world.Add<game::components::UIText>(m_entities.background);
  background.x = x;
  background.y = y;
  background.width = kPanelWidth;
  background.height = kPanelHeight;
  ApplySurfaceStyle(background.style);
  background.visible = true;
  background.layer = game::ui::kLayerBrowser - 1;

  m_entities.wikiBadge = m_entityOwner.Create(ctx.world);
  auto &wikiBadge =
      ctx.world.Add<game::components::UIText>(m_entities.wikiBadge);
  wikiBadge.text = L"WIKI";
  wikiBadge.x = x + 14.0f;
  wikiBadge.y = y + 12.0f;
  wikiBadge.width = 48.0f;
  wikiBadge.height = 20.0f;
  wikiBadge.style = graphics::TextStyle::CardLabel();
  wikiBadge.style.align = graphics::TextAlign::Center;
  wikiBadge.style.bgColor = game::ui::kColorSurfaceRaised;
  wikiBadge.style.cornerRadius = game::ui::kRadiusChip * 0.75f;
  wikiBadge.style.borderWidth = game::ui::kBorderWidthThin;
  wikiBadge.style.borderColor = game::ui::kColorBorder;
  wikiBadge.visible = true;
  wikiBadge.layer = game::ui::kLayerBrowser;

  m_entities.currentLabel = m_entityOwner.Create(ctx.world);
  auto &currentLabel =
      ctx.world.Add<game::components::UIText>(m_entities.currentLabel);
  currentLabel.text = L"CURRENT";
  currentLabel.x = x + 14.0f;
  currentLabel.y = y + 44.0f;
  currentLabel.width = kPanelWidth - 28.0f;
  currentLabel.height = 16.0f;
  currentLabel.style = graphics::TextStyle::CardLabel();
  currentLabel.visible = true;
  currentLabel.layer = game::ui::kLayerBrowser + 1;

  m_entities.currentPage = m_entityOwner.Create(ctx.world);
  auto &currentPage =
      ctx.world.Add<game::components::UIText>(m_entities.currentPage);
  currentPage.text = L"Loading...";
  currentPage.x = x + 14.0f;
  currentPage.y = y + 59.0f;
  currentPage.width = kPanelWidth - 28.0f;
  currentPage.height = 26.0f;
  currentPage.style = graphics::TextStyle::BrowserURL();
  currentPage.style.fontSize = game::ui::kBrowserFontSize;
  currentPage.visible = true;
  currentPage.layer = game::ui::kLayerBrowser + 1;

  m_entities.targetLabel = m_entityOwner.Create(ctx.world);
  auto &targetLabel =
      ctx.world.Add<game::components::UIText>(m_entities.targetLabel);
  targetLabel.text = L"TARGET";
  targetLabel.x = x + 14.0f;
  targetLabel.y = y + 90.0f;
  targetLabel.width = kPanelWidth - 28.0f;
  targetLabel.height = 16.0f;
  targetLabel.style = graphics::TextStyle::CardLabel();
  targetLabel.visible = true;
  targetLabel.layer = game::ui::kLayerBrowser + 1;

  m_entities.targetPage = m_entityOwner.Create(ctx.world);
  auto &targetPage =
      ctx.world.Add<game::components::UIText>(m_entities.targetPage);
  targetPage.text = L"Target page...";
  targetPage.x = x + 14.0f;
  targetPage.y = y + 105.0f;
  targetPage.width = kPanelWidth - 28.0f;
  targetPage.height = 24.0f;
  targetPage.style = graphics::TextStyle::GoalHighlight();
  targetPage.style.fontSize = game::ui::kBrowserGoalFontSize;
  targetPage.visible = true;
  targetPage.layer = game::ui::kLayerBrowser + 1;

  m_entities.scoreBackground = m_entityOwner.Create(ctx.world);
  auto &scoreBackground =
      ctx.world.Add<game::components::UIText>(m_entities.scoreBackground);
  scoreBackground.x = x;
  scoreBackground.y = y + kPanelHeight + 4.0f;
  scoreBackground.width = kPanelWidth;
  scoreBackground.height = 26.0f;
  ApplySurfaceStyle(scoreBackground.style, game::ui::kRadiusChip);
  scoreBackground.visible = true;
  scoreBackground.layer = game::ui::kLayerBrowser - 1;

  m_entities.scoreText = m_entityOwner.Create(ctx.world);
  auto &scoreText =
      ctx.world.Add<game::components::UIText>(m_entities.scoreText);
  scoreText.text = L"打数 0　目標まで目安5リンク　移動 0";
  scoreText.x = x + 14.0f;
  scoreText.y = y + kPanelHeight + 8.0f;
  scoreText.width = kPanelWidth - 28.0f;
  scoreText.height = 18.0f;
  scoreText.style = graphics::TextStyle::BrowserSub();
  scoreText.style.fontFamily = "Meiryo";
  scoreText.style.fontSize = game::ui::kBrowserSubFontSize;
  scoreText.visible = true;
  scoreText.layer = game::ui::kLayerBrowser;
}

void CourseInfoPanel::Update(
    core::GameContext &ctx,
    const game::components::GolfGameState &state) {
  auto *currentPage =
      ctx.world.Get<game::components::UIText>(m_entities.currentPage);
  if (currentPage) {
    SetTextIfChanged(*currentPage, core::ToWString(state.currentPage));
  }

  auto *targetPage =
      ctx.world.Get<game::components::UIText>(m_entities.targetPage);
  if (targetPage) {
    SetTextIfChanged(*targetPage, core::ToWString(state.targetPage));
  }

  auto *scoreText =
      ctx.world.Get<game::components::UIText>(m_entities.scoreText);
  if (scoreText) {
    const std::wstring value =
        L"打数 " + std::to_wstring(state.shotCount) + L"　目標まで目安" +
        std::to_wstring(state.par) + L"リンク　移動 " +
        std::to_wstring(state.moveCount);
    SetTextIfChanged(*scoreText, value);
  }
}

void CourseInfoPanel::SetVisible(core::GameContext &ctx, bool visible) {
  SetTextVisible(ctx.world, m_entities.background, visible);
  SetTextVisible(ctx.world, m_entities.wikiBadge, visible);
  SetTextVisible(ctx.world, m_entities.currentLabel, visible);
  SetTextVisible(ctx.world, m_entities.currentPage, visible);
  SetTextVisible(ctx.world, m_entities.targetLabel, visible);
  SetTextVisible(ctx.world, m_entities.targetPage, visible);
  SetTextVisible(ctx.world, m_entities.scoreBackground, visible);
  SetTextVisible(ctx.world, m_entities.scoreText, visible);
}

void CourseInfoPanel::Shutdown(core::GameContext &ctx) {
  m_entityOwner.DestroyAll(ctx.world);
  m_entities = Entities{};
}

} // namespace game::controllers::hud
