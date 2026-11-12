#pragma once
/**
 * @file StaticBoxObstacles.h
 * @brief ショット予測に使う、動かない箱（外周の壁・障害物）の集まり
 *
 * 実際の物理(PhysicsSystem)がボールをぶつけている相手と同じものを集め、
 * 同じ当たり判定で道すじを折り返す。これが無いと予測線は壁を突き抜ける。
*/

#include "../../utils/ShotGuidePath.h"
#include <DirectXMath.h>
#include <vector>

namespace ecs {
class World;
}

namespace game::guide {

class StaticBoxObstacles final : public IShotGuideObstacles {
public:
  /** @brief ワールドから動かない箱を集め直す（ホールは通り抜けるので除く）*/
  void Collect(ecs::World &world);

  /** @brief 集め直すたびに増える番号。変わったら道すじを計算し直す*/
  int Revision() const { return m_revision; }

  bool Resolve(game::physics::BallSimState &state, float ballRadius,
               float ballRestitution) const override;

private:
  struct Box {
    DirectX::XMFLOAT3 center;
    DirectX::XMFLOAT3 size;     ///< 全寸法（拡大率込み）
    DirectX::XMFLOAT4 rotation;
    float restitution = 0.5f;
    float reach = 0.0f;         ///< 中心から最も遠い角までの距離
  };
  std::vector<Box> m_boxes;
  int m_revision = 0;
};

} // namespace game::guide
