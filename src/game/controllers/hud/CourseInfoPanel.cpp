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
  constexpr float kPanelWidth = game::ui::kBrowserHudWidth;
  constexpr float kLabelWidth = game::ui::kBrowserLabelWidth;
  constexpr float kRowHeight = game::ui::kBrowserRowHeight;

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
  currentLabel.text = L"現在";
  currentLabel.x = x;
  currentLabel.y = y + 4.0f;
  currentLabel.width = kLabelWidth;
  currentLabel.height = 26.0f;
  ApplyCourseInfoLabelStyle(currentLabel.style);
  currentLabel.visible = true;
  currentLabel.layer = game::ui::kLayerBrowser + 1;

  m_entities.currentPage = m_entityOwner.Create(ctx.world);
  auto &currentPage =
      ctx.world.Add<game::components::UIText>(m_entities.currentPage);
  currentPage.text = L"読み込み中…";
  currentPage.x = x + kLabelWidth;
  currentPage.y = y;
  currentPage.width = kPanelWidth - kLabelWidth;
  currentPage.height = 30.0f;
  ApplyCourseInfoNameStyle(currentPage.style);
  currentPage.visible = true;
  currentPage.layer = game::ui::kLayerBrowser + 1;

  m_entities.targetLabel = m_entityOwner.Create(ctx.world);
  auto &targetLabel =
      ctx.world.Add<game::components::UIText>(m_entities.targetLabel);
  targetLabel.text = L"ゴール";
  targetLabel.x = x;
  targetLabel.y = y + kRowHeight + 8.0f;
  targetLabel.width = kLabelWidth;
  targetLabel.height = 26.0f;
  ApplyCourseInfoLabelStyle(targetLabel.style);
  targetLabel.visible = true;
  targetLabel.layer = game::ui::kLayerBrowser + 1;

  m_entities.targetPage = m_entityOwner.Create(ctx.world);
  auto &targetPage =
      ctx.world.Add<game::components::UIText>(m_entities.targetPage);
  targetPage.text = L"読み込み中…";
  targetPage.x = x + kLabelWidth;
  targetPage.y = y + kRowHeight + 4.0f;
  targetPage.width = kPanelWidth - kLabelWidth;
  targetPage.height = 30.0f;
  ApplyCourseInfoNameStyle(targetPage.style);
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

  m_entities.attribution = m_entityOwner.Create(ctx.world);
  auto &attribution =
      ctx.world.Add<game::components::UIText>(m_entities.attribution);
  attribution.text = game::ui::kArticleAttribution;
  attribution.x = x;
  attribution.y = y + kRowHeight * 2.0f + 8.0f;
  attribution.width = kPanelWidth;
  attribution.height = 20.0f;
  ApplyCourseInfoAttributionStyle(attribution.style);
  attribution.visible = true;
  attribution.layer = game::ui::kLayerBrowser + 1;
}

void CourseInfoPanel::Update(
    core::GameContext &ctx,
    const game::components::GolfGameState &state) {
  SetPages(ctx, core::ToWString(state.currentPage),
           core::ToWString(state.targetPage));
}

void CourseInfoPanel::SetPages(core::GameContext &ctx,
                               const std::wstring &currentPage,
                               const std::wstring &targetPage) {
  if (auto *text =
          ctx.world.Get<game::components::UIText>(m_entities.currentPage)) {
    SetTextIfChanged(*text, currentPage);
  }
  if (auto *text =
          ctx.world.Get<game::components::UIText>(m_entities.targetPage)) {
    SetTextIfChanged(*text, targetPage);
  }
}

void CourseInfoPanel::SetVisible(core::GameContext &ctx, bool visible) {
  SetTextVisible(ctx.world, m_entities.currentRule, visible);
  SetTextVisible(ctx.world, m_entities.currentLabel, visible);
  SetTextVisible(ctx.world, m_entities.currentPage, visible);
  SetTextVisible(ctx.world, m_entities.targetLabel, visible);
  SetTextVisible(ctx.world, m_entities.targetPage, visible);
  SetTextVisible(ctx.world, m_entities.targetRule, visible);
  SetTextVisible(ctx.world, m_entities.attribution, visible);
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
