#pragma once
/**
 * @file MinimapGuideOverlay.h
 * @brief ミニマップに重ねるショット予測（流れる破線・止まりそうな範囲）
 *
 * 道すじ(ShotGuidePath)と「ワールド座標→画面座標」の変換だけを受け取って描く。
 * ミニマップの中身（地形の描画や他のマーカー）のことは知らない。
*/

#include "../../../ecs/Entity.h"
#include "../../../ecs/EntityOwner.h"
#include "../../utils/ShotGuidePath.h"
#include "../../utils/ShotGuideZone.h"
#include <DirectXMath.h>
#include <functional>
#include <vector>

namespace core {
struct GameContext;
}

namespace game::guide {

class MinimapGuideOverlay {
public:
  /** @brief ワールド座標を画面座標へ。地図の外なら false を返す*/
  using Projector =
      std::function<bool(const DirectX::XMFLOAT3 &world, float &x, float &y)>;

  /** @param layer UIImage の重ね順（これと、これ+1 を使う）*/
  void Initialize(core::GameContext &ctx, int layer);

  /**
   * @param path 表示する道すじ。nullptr なら隠す
   * @param zone 止まりそうな範囲。nullptr なら範囲は描かない
   * @param projector ワールド座標→画面座標
  */
  void Update(core::GameContext &ctx, const ShotGuidePath *path,
              const ShotGuideZone *zone, const Projector &projector);
  void Hide(core::GameContext &ctx);
  void Shutdown(core::GameContext &ctx);

private:
  std::vector<ecs::Entity> m_dashes;
  ecs::Entity m_zone = UINT32_MAX; ///< 止まりそうな範囲（だ円の輪）
  ecs::EntityOwner m_owner;

  struct ScreenPoint {
    float x = 0.0f;
    float y = 0.0f;
    float length = 0.0f; ///< 始点からの画面上の長さ[px]
  };
  std::vector<ScreenPoint> m_polyline; ///< 毎回の確保を避けるための作業領域
};

} // namespace game::guide
