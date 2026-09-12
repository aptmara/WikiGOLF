/**
 * @file CourseInfoPanel.cpp
 * @brief コース情報パネルの実装
*/

#include "CourseInfoPanel.h"
#include "../../../core/GameContext.h"
#include "../../../core/StringUtils.h"
#include "../../../ecs/World.h"
#include "../../components/UIText.h"
#include "../../components/WikiComponents.h"
#include "../../utils/UIConstants.h"

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
  constexpr float kPanelWidth = 440.0f;
  constexpr float kLabelWidth = 92.0f;
  constexpr float kRowHeight = 34.0f;

  m_entities.currentRule = m_entityOwner.Create(ctx.world);
  auto &currentRule =
      ctx.world.Add<game::components::UIText>(m_entities.currentRule);
  currentRule.x = x;
  currentRule.y = y + kRowHeight;
  currentRule.width = kPanelWidth;
  currentRule.height = 2.0f;
  currentRule.style.bgColor = {1.0f, 1.0f, 1.0f, 0.9f};
  currentRule.visible = true;
  currentRule.layer = game::ui::kLayerBrowser;

  m_entities.currentLabel = m_entityOwner.Create(ctx.world);
  auto &currentLabel =
      ctx.world.Add<game::components::UIText>(m_entities.currentLabel);
  currentLabel.text = L"CURRENT";
  currentLabel.x = x;
  currentLabel.y = y + 4.0f;
  currentLabel.width = kLabelWidth;
  currentLabel.height = 26.0f;
  currentLabel.style = graphics::TextStyle::Guide();
  currentLabel.style.fontFamily = "Barlow Condensed Black";
  currentLabel.style.fontSize = 17.0f;
  currentLabel.style.align = graphics::TextAlign::Left;
  currentLabel.visible = true;
  currentLabel.layer = game::ui::kLayerBrowser + 1;

  m_entities.currentPage = m_entityOwner.Create(ctx.world);
  auto &currentPage =
      ctx.world.Add<game::components::UIText>(m_entities.currentPage);
  currentPage.text = L"Loading...";
  currentPage.x = x + kLabelWidth;
  currentPage.y = y;
  currentPage.width = kPanelWidth - kLabelWidth;
  currentPage.height = 30.0f;
  currentPage.style = graphics::TextStyle::Guide();
  currentPage.style.fontFamily = "Kiwi Maru Medium";
  currentPage.style.fontSize = 22.0f;
  currentPage.style.align = graphics::TextAlign::Right;
  currentPage.visible = true;
  currentPage.layer = game::ui::kLayerBrowser + 1;

  m_entities.targetLabel = m_entityOwner.Create(ctx.world);
  auto &targetLabel =
      ctx.world.Add<game::components::UIText>(m_entities.targetLabel);
  targetLabel.text = L"TARGET";
  targetLabel.x = x;
  targetLabel.y = y + kRowHeight + 8.0f;
  targetLabel.width = kLabelWidth;
  targetLabel.height = 26.0f;
  targetLabel.style = graphics::TextStyle::Guide();
  targetLabel.style.fontFamily = "Barlow Condensed Black";
  targetLabel.style.fontSize = 17.0f;
  targetLabel.style.align = graphics::TextAlign::Left;
  targetLabel.visible = true;
  targetLabel.layer = game::ui::kLayerBrowser + 1;

  m_entities.targetPage = m_entityOwner.Create(ctx.world);
  auto &targetPage =
      ctx.world.Add<game::components::UIText>(m_entities.targetPage);
  targetPage.text = L"Target page...";
  targetPage.x = x + kLabelWidth;
  targetPage.y = y + kRowHeight + 4.0f;
  targetPage.width = kPanelWidth - kLabelWidth;
  targetPage.height = 30.0f;
  targetPage.style = graphics::TextStyle::Guide();
  targetPage.style.fontFamily = "Kiwi Maru Medium";
  targetPage.style.fontSize = 22.0f;
  targetPage.style.align = graphics::TextAlign::Right;
  targetPage.visible = true;
  targetPage.layer = game::ui::kLayerBrowser + 1;

  m_entities.targetRule = m_entityOwner.Create(ctx.world);
  auto &targetRule =
      ctx.world.Add<game::components::UIText>(m_entities.targetRule);
  targetRule.x = x;
  targetRule.y = y + kRowHeight * 2.0f + 4.0f;
  targetRule.width = kPanelWidth;
  targetRule.height = 2.0f;
  targetRule.style.bgColor = {1.0f, 1.0f, 1.0f, 0.9f};
  targetRule.visible = true;
  targetRule.layer = game::ui::kLayerBrowser;
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

}

void CourseInfoPanel::SetVisible(core::GameContext &ctx, bool visible) {
  SetTextVisible(ctx.world, m_entities.currentRule, visible);
  SetTextVisible(ctx.world, m_entities.currentLabel, visible);
  SetTextVisible(ctx.world, m_entities.currentPage, visible);
  SetTextVisible(ctx.world, m_entities.targetLabel, visible);
  SetTextVisible(ctx.world, m_entities.targetPage, visible);
  SetTextVisible(ctx.world, m_entities.targetRule, visible);
}

void CourseInfoPanel::SetOpacity(core::GameContext &ctx, float opacity) {
  ctx.world.Query<game::components::UIText>().Each(
      [&](ecs::Entity entity, game::components::UIText &text) {
        if (m_entityOwner.Owns(entity)) text.opacity = opacity;
      });
}

void CourseInfoPanel::Shutdown(core::GameContext &ctx) {
  m_entityOwner.DestroyAll(ctx.world);
  m_entities = Entities{};
}

} // namespace game::controllers::hud
