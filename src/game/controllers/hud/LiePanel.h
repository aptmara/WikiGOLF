#pragma once
/**
 * @file LiePanel.h
 * @brief ボールが停止している地形状態を表示するHUDパネル
 */

#include "../../../ecs/Entity.h"
#include "../../../ecs/EntityOwner.h"
#include "../../components/WikiComponents.h"

namespace core {
struct GameContext;
}

namespace game::controllers::hud {

/** @brief 地形種別からライ名、状態説明、意味色を決定します。 */
class LiePanel {
public:
  /** @brief 必要ならEntityを生成し、現在のライを表示へ反映します。 */
  void Update(core::GameContext &ctx,
              game::components::TerrainMaterial material);

  /** @brief ショット中の表示状態を切り替えます。 */
  void SetShotPhaseVisible(core::GameContext &ctx, bool shotPhase);

  /** @brief パネル全体の表示状態を変更します。 */
  void SetVisible(core::GameContext &ctx, bool visible);

  /** @brief 生成したすべてのEntityを破棄します。 */
  void Shutdown(core::GameContext &ctx);

private:
  void Initialize(core::GameContext &ctx);

  ecs::Entity m_background = UINT32_MAX;
  ecs::Entity m_label = UINT32_MAX;
  ecs::Entity m_value = UINT32_MAX;
  ecs::Entity m_condition = UINT32_MAX;
  ecs::EntityOwner m_entityOwner;
};

} // namespace game::controllers::hud
