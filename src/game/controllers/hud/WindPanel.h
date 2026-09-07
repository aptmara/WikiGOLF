#pragma once
/**
 * @file WindPanel.h
 * @brief 風速とカメラ相対の風向きを表示するHUDパネル
*/

#include "../../../ecs/Entity.h"
#include "../../../ecs/EntityOwner.h"
#include <DirectXMath.h>

namespace core {
struct GameContext;
}

namespace game::controllers::hud {

/**
 * @brief 風情報パネルのEntity生成と表示更新を管理します。
*/
class WindPanel {
public:
  /** @brief 風情報を構成するUI Entityを生成します。*/
  void Initialize(core::GameContext &ctx);

  /**
   * @brief 風速とカメラ相対方向を表示へ反映します。
   * @param ctx ゲーム全体の共有コンテキストです。
   * @param elapsedTime HUD開始からの経過時間です。
   * @param windSpeed 風速です。
   * @param windDirection ワールド座標上の風向きです。
   * @param cameraYaw カメラのヨー角です。
*/
  void Update(core::GameContext &ctx, float elapsedTime, float windSpeed,
              const DirectX::XMFLOAT2 &windDirection, float cameraYaw);

  /** @brief パネル全体の表示状態を変更します。*/
  void SetVisible(core::GameContext &ctx, bool visible);

  /** @brief パネルが生成したすべてのEntityを破棄します。*/
  void Shutdown(core::GameContext &ctx);

private:
  ecs::Entity m_background = UINT32_MAX;
  ecs::Entity m_label = UINT32_MAX;
  ecs::Entity m_value = UINT32_MAX;
  ecs::Entity m_directionAndUnit = UINT32_MAX;
  ecs::EntityOwner m_entityOwner;
};

} // namespace game::controllers::hud
