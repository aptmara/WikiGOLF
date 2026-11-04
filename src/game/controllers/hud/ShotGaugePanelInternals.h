#pragma once
/**
 * @file ShotGaugePanelInternals.h
 * @brief ショットゲージ表示処理で共有する内部ヘルパー
*/

#include "../../../ecs/World.h"
#include "../../components/UIText.h"
#include <DirectXMath.h>
#include <string>

namespace game::controllers::hud::shot_gauge_detail {

inline void SetText(ecs::World &world, ecs::Entity entity,
                    const std::wstring &value) {
  auto *text = world.Get<game::components::UIText>(entity);
  if (!text || text->text == value) {
    return;
  }
  text->text = value;
}

inline void SetColor(ecs::World &world, ecs::Entity entity,
                     const DirectX::XMFLOAT4 &value) {
  auto *text = world.Get<game::components::UIText>(entity);
  if (!text) {
    return;
  }
  text->style.color = value;
}

inline void SetTextVisible(ecs::World &world, ecs::Entity entity,
                           bool visible) {
  auto *text = world.Get<game::components::UIText>(entity);
  if (!text) {
    return;
  }
  text->visible = visible;
}

} // namespace game::controllers::hud::shot_gauge_detail

