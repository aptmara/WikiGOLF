#pragma once
/**
 * @file TrajectoryPredictor.h
 * @brief TrajectoryPredictor クラスおよび関連定義
*/

#include "../../ecs/Entity.h"
#include "../../ecs/EntityOwner.h"
#include "../utils/CarryDistanceTable.h"
#include <DirectXMath.h>
#include <cstddef>
#include <vector>

namespace core {
struct GameContext;
}

namespace game::systems {
class WikiTerrainSystem;
}

namespace game::controllers {

class TrajectoryPredictor {
public:
  struct Params {
    ecs::Entity ballEntity = UINT32_MAX;
    ecs::Entity arrowEntity = UINT32_MAX;
    DirectX::XMFLOAT3 shotDirection = {0.0f, 0.0f, 1.0f};
    /** @brief 平坦・無風フェアウェイでのフルスイング基準キャリー飛距離(ヤード)*/
    float baseCarryDistance = 0.0f;
    /** @brief 「目標飛距離→初速」を求めるためのクラブ別対応表への参照*/
    const game::utils::CarryDistanceTable *carryTable = nullptr;
    float launchAngle = 0.0f;
    bool isMapView = false;
    game::systems::WikiTerrainSystem *terrainSystem = nullptr;
    float powerRatio = 0.0f;
  };

  void Initialize(core::GameContext &ctx, size_t dotCount = 30);

  /**
   * @brief 軌道予測表示が生成したEntityを破棄します。
   * @param ctx ゲーム全体の共有コンテキストです。
*/
  void Shutdown(core::GameContext &ctx);
  void Update(core::GameContext &ctx, const Params &params);
  void Hide(core::GameContext &ctx);
  const std::vector<ecs::Entity> &GetDots() const;

private:
  std::vector<ecs::Entity> m_dots;
  ecs::Entity m_landingEntity = UINT32_MAX; ///< 着地点マーカーエンティティ
  ecs::EntityOwner m_entityOwner;
};

} // namespace game::controllers
