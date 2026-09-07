#pragma once
/**
 * @file CourseInfoPanel.h
 * @brief 現在記事、目的記事、スコアを表示するHUDパネル
 */

#include "../../../ecs/Entity.h"
#include "../../../ecs/EntityOwner.h"

namespace core {
struct GameContext;
}

namespace game::components {
struct GolfGameState;
}

namespace game::controllers::hud {

/**
 * @brief コース情報パネルのEntity生成と表示更新を管理します。
 */
class CourseInfoPanel {
public:
  /**
   * @brief パネルを構成するUI Entityを生成します。
   * @param ctx ゲーム全体の共有コンテキストです。
   */
  void Initialize(core::GameContext &ctx);

  /**
   * @brief 現在のゲーム状態を表示へ反映します。
   * @param ctx ゲーム全体の共有コンテキストです。
   * @param state 表示するWikiGolfの状態です。
   */
  void Update(core::GameContext &ctx,
              const game::components::GolfGameState &state);

  /**
   * @brief パネル全体の表示状態を変更します。
   * @param ctx ゲーム全体の共有コンテキストです。
   * @param visible 表示する場合はtrueです。
   */
  void SetVisible(core::GameContext &ctx, bool visible);

  /**
   * @brief パネルが生成したすべてのEntityを破棄します。
   * @param ctx ゲーム全体の共有コンテキストです。
   */
  void Shutdown(core::GameContext &ctx);

private:
  struct Entities {
    ecs::Entity background = UINT32_MAX;
    ecs::Entity wikiBadge = UINT32_MAX;
    ecs::Entity currentLabel = UINT32_MAX;
    ecs::Entity currentPage = UINT32_MAX;
    ecs::Entity targetLabel = UINT32_MAX;
    ecs::Entity targetPage = UINT32_MAX;
    ecs::Entity scoreBackground = UINT32_MAX;
    ecs::Entity scoreText = UINT32_MAX;
  };

  Entities m_entities;
  ecs::EntityOwner m_entityOwner;
};

} // namespace game::controllers::hud
