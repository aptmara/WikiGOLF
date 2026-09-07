/**
 * @file MinimapDecorationPanel.cpp
 * @brief ミニマップ装飾パネルの実装
 */

#include "MinimapDecorationPanel.h"
#include "HudStyles.h"
#include "../../../core/GameContext.h"
#include "../../../ecs/World.h"
#include "../../components/UIText.h"
#include "../../utils/UIConstants.h"

namespace game::controllers::hud {

void MinimapDecorationPanel::Initialize(core::GameContext &ctx) {
  const float x = game::ui::kMinimapX;
  const float y = game::ui::kMinimapY;
  const float width = game::ui::kMinimapWidth;
  const float height = game::ui::kMinimapHeight;
  const int foregroundLayer = game::ui::kLayerMinimap + 1;

  ecs::Entity entity = m_entityOwner.Create(ctx.world);
  auto &background = ctx.world.Add<game::components::UIText>(entity);
  background.x = x - 6.0f;
  background.y = y - 6.0f;
  background.width = width + 12.0f;
  background.height = height + 36.0f;
  ApplySurfaceStyle(background.style);
  background.visible = true;
  background.layer = game::ui::kLayerMinimap - 1;

  entity = m_entityOwner.Create(ctx.world);
  auto &frame = ctx.world.Add<game::components::UIText>(entity);
  frame.x = x;
  frame.y = y;
  frame.width = width;
  frame.height = height;
  frame.style.bgColor = {0.0f, 0.0f, 0.0f, 0.22f};
  frame.style.borderWidth = 3.0f;
  frame.style.borderColor = game::ui::kColorBgDark;
  frame.style.cornerRadius = game::ui::kRadiusPanel;
  frame.visible = true;
  frame.layer = foregroundLayer;

  entity = m_entityOwner.Create(ctx.world);
  auto &north = ctx.world.Add<game::components::UIText>(entity);
  north.text = L"N\n▲";
  north.x = x + width - 26.0f;
  north.y = y + 6.0f;
  north.width = 22.0f;
  north.height = 30.0f;
  north.style = graphics::TextStyle::Guide();
  north.style.fontSize = 12.0f;
  north.style.color = game::ui::kColorWhite;
  north.style.align = graphics::TextAlign::Center;
  north.visible = true;
  north.layer = foregroundLayer;

  const auto createScale = [&](float offsetY, const std::wstring &text) {
    const ecs::Entity scaleEntity = m_entityOwner.Create(ctx.world);
    auto &scale = ctx.world.Add<game::components::UIText>(scaleEntity);
    scale.text = text;
    scale.x = x + width - 42.0f;
    scale.y = y + offsetY;
    scale.width = 36.0f;
    scale.height = 15.0f;
    scale.style = graphics::TextStyle::Guide();
    scale.style.fontSize = 10.0f;
    scale.style.color = {0.8f, 0.8f, 0.8f, 1.0f};
    scale.style.align = graphics::TextAlign::Right;
    scale.visible = true;
    scale.layer = foregroundLayer;
  };
  createScale(30.0f, L"150m");
  createScale(height / 2.0f, L" 50m");
  createScale(height - 15.0f, L"  0m");

  entity = m_entityOwner.Create(ctx.world);
  auto &ballLegend = ctx.world.Add<game::components::UIText>(entity);
  ballLegend.text = L"● BALL";
  ballLegend.x = x + 8.0f;
  ballLegend.y = y + height + 5.0f;
  ballLegend.width = 72.0f;
  ballLegend.height = 20.0f;
  ballLegend.style.fontFamily = "Meiryo";
  ballLegend.style.fontSize = 12.0f;
  ballLegend.style.color = game::ui::kColorAccent;
  ballLegend.style.align = graphics::TextAlign::Left;
  ballLegend.visible = true;
  ballLegend.layer = foregroundLayer;

  entity = m_entityOwner.Create(ctx.world);
  auto &targetLegend = ctx.world.Add<game::components::UIText>(entity);
  targetLegend.text = L"⛳ TARGET";
  targetLegend.x = x + 92.0f;
  targetLegend.y = y + height + 5.0f;
  targetLegend.width = 80.0f;
  targetLegend.height = 20.0f;
  targetLegend.style.fontFamily = "Meiryo";
  targetLegend.style.fontSize = 12.0f;
  targetLegend.style.color = game::ui::kColorError;
  targetLegend.style.align = graphics::TextAlign::Left;
  targetLegend.visible = true;
  targetLegend.layer = foregroundLayer;
}

void MinimapDecorationPanel::SetVisible(core::GameContext &ctx,
                                        bool visible) {
  ctx.world.Query<game::components::UIText>().Each(
      [&](ecs::Entity entity, game::components::UIText &text) {
        if (m_entityOwner.Owns(entity)) {
          text.visible = visible;
        }
      });
}

void MinimapDecorationPanel::Shutdown(core::GameContext &ctx) {
  m_entityOwner.DestroyAll(ctx.world);
}

} // namespace game::controllers::hud
