#pragma once
/**
 * @file GameplayControlsPanel.h
 * @brief ショット開始ボタンと通常操作ヒントを管理するHUDパネル
*/

#include "../../../ecs/Entity.h"
#include "../../../ecs/EntityOwner.h"

namespace core {
struct GameContext;
}

namespace game::controllers::hud {

/** @brief 通常操作中だけ表示する固定操作UIを管理します。*/
class GameplayControlsPanel {
public:
  /** @brief 固定操作UIを生成します。*/
  void Initialize(core::GameContext &ctx);

  /** @brief ショット入力中の表示状態を切り替えます。*/
  void SetShotPhaseVisible(core::GameContext &ctx, bool shotPhase);

  /** @brief パネル全体の表示状態を変更します。*/
  void SetVisible(core::GameContext &ctx, bool visible);

  /** @brief チュートリアル専用ガイドと競合する通常操作表示を抑止します。*/
  void SetTutorialMode(core::GameContext &ctx, bool enabled);

  /** @brief 生成したすべてのEntityを破棄します。*/
  void Shutdown(core::GameContext &ctx);

private:
  ecs::Entity m_shotButtonBackground = UINT32_MAX;
  ecs::Entity m_shotButtonText = UINT32_MAX;
  ecs::Entity m_controlHint = UINT32_MAX;
  bool m_tutorialMode = false;
  ecs::EntityOwner m_entityOwner;
};

} // namespace game::controllers::hud
