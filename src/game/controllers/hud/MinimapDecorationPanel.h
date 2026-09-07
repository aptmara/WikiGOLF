#pragma once
/**
 * @file MinimapDecorationPanel.h
 * @brief ミニマップの枠、方位、縮尺、凡例を管理するHUDパネル
 */

#include "../../../ecs/EntityOwner.h"

namespace core {
struct GameContext;
}

namespace game::controllers::hud {

/** @brief MinimapControllerが描画する地図の周辺装飾を管理します。 */
class MinimapDecorationPanel {
public:
  /** @brief ミニマップ周辺の固定表示を生成します。 */
  void Initialize(core::GameContext &ctx);

  /** @brief 装飾全体の表示状態を変更します。 */
  void SetVisible(core::GameContext &ctx, bool visible);

  /** @brief 生成したすべてのEntityを破棄します。 */
  void Shutdown(core::GameContext &ctx);

private:
  ecs::EntityOwner m_entityOwner;
};

} // namespace game::controllers::hud
