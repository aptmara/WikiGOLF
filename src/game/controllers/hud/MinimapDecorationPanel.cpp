/**
 * @file MinimapDecorationPanel.cpp
 * @brief 参考HUDに合わせた大型ミニマップ枠
 */

#include "MinimapDecorationPanel.h"
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

  ecs::Entity entity = m_entityOwner.Create(ctx.world);
  auto &panel = ctx.world.Add<game::components::UIText>(entity);
  panel.x = x - 14.0f;
  panel.y = y - 14.0f;
  panel.width = width + 28.0f;
  panel.height = height + 28.0f;
  panel.style.bgColor = {0.84f, 0.91f, 0.82f, 0.30f};
  panel.style.borderWidth = 5.0f;
  panel.style.borderColor = {1.0f, 1.0f, 1.0f, 0.95f};
  panel.style.cornerRadius = 20.0f;
  panel.style.hasShadow = true;
  panel.style.shadowColor = {0.0f, 0.0f, 0.0f, 0.45f};
  panel.style.shadowOffsetY = 3.0f;
  panel.layer = game::ui::kLayerMinimap - 1;

  entity = m_entityOwner.Create(ctx.world);
  auto &north = ctx.world.Add<game::components::UIText>(entity);
  north.text = L"N\n▲";
  north.x = x + width - 34.0f;
  north.y = y + 10.0f;
  north.width = 24.0f;
  north.height = 34.0f;
  north.style = graphics::TextStyle::Guide();
  north.style.fontFamily = "Barlow Condensed Black";
  north.style.fontSize = 13.0f;
  north.layer = game::ui::kLayerMinimap + 4;
}

void MinimapDecorationPanel::SetVisible(core::GameContext &ctx,
                                        bool visible) {
  ctx.world.Query<game::components::UIText>().Each(
      [&](ecs::Entity entity, game::components::UIText &text) {
        if (m_entityOwner.Owns(entity)) text.visible = visible;
      });
}

void MinimapDecorationPanel::SetOpacity(core::GameContext &ctx,
                                        float opacity) {
  ctx.world.Query<game::components::UIText>().Each(
      [&](ecs::Entity entity, game::components::UIText &text) {
        if (m_entityOwner.Owns(entity)) text.opacity = opacity;
      });
}

void MinimapDecorationPanel::Shutdown(core::GameContext &ctx) {
  m_entityOwner.DestroyAll(ctx.world);
}

} // namespace game::controllers::hud
