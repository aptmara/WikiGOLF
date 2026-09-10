#pragma once
/**
 * @file AimPinController.h
 * @brief 中クリックで狙い所（エイムピン）を設置する入力コントローラー
 * @details マウスから地形へのレイキャストでワールド座標を求め、AimPinState
 *          (グローバル)へ反映する。全体マップビュー中もメインカメラ自体が
 *          高所へ移動して見下ろしているだけ（実カメラによる透視投影）なので、
 *          三人称視点時と同じレイキャストがそのまま使える。
 *          ドラッグ操作（マップパン・視点回転等）と区別するため、押下から
 *          一定距離以上マウスが動いた場合はクリックとみなさずピンを設置しない。
*/

#include "../../core/GameContext.h"
#include "../../ecs/Entity.h"
#include "../../ecs/EntityOwner.h"
#include <DirectXMath.h>

namespace game::systems {
class WikiTerrainSystem;
}

namespace game::controllers {

class AimPinController {
public:
  struct UpdateParams {
    int mouseX = 0;
    int mouseY = 0;
    /** @brief ショット待機中(Idle)かつ操作可能な間だけtrue*/
    bool allowInput = true;
    ecs::Entity ballEntity = UINT32_MAX;
    ecs::Entity cameraEntity = UINT32_MAX;
    game::systems::WikiTerrainSystem *terrainSystem = nullptr;
    /**
     * @brief レイキャストでピンを探索する最大距離(ワールド単位、レイの
     * 進行距離であって水平距離ではない)。
     * @details 浅い角度で遠くの地形を見ている(カメラが見下ろす角度が
     *          浅い)場合、水平方向にはさほど遠くなくても、レイが地形の
     *          高さまで降りてくるのに必要な進行距離は非常に長くなる。
     *          小さすぎると「地形が見えているのに置けない」原因になる。
*/
    float maxDistance = 3000.0f;
  };

  struct UpdateResult {
    bool pinPlaced = false;  ///< このフレームで新しくピンを設置したか
    float distance = 0.0f;   ///< 設置した場合のボールからの水平距離
  };

  /** @brief 中クリック入力を処理し、必要ならAimPinStateを更新します。*/
  UpdateResult Update(core::GameContext &ctx, const UpdateParams &params);

  /** @brief チュートリアル誘導などで指定ワールド位置へ照準ピンを設置します。*/
  float PlacePin(core::GameContext& ctx, ecs::Entity ballEntity,
                 const DirectX::XMFLOAT3& worldPos);

  /** @brief エイムピンを無効化し、3D旗マーカーも破棄します（ショット実行時などに呼ぶ）。*/
  void ClearPin(core::GameContext &ctx);

  /** @brief 生成した3D旗マーカーのEntityを破棄します（シーン終了時に呼ぶ）。*/
  void Shutdown(core::GameContext &ctx);

private:
  /** @brief 3D旗マーカーを指定位置に建て直します（既存があれば先に破棄）。*/
  void RebuildWorldMarker(core::GameContext &ctx, const DirectX::XMFLOAT3 &worldPos);

  bool m_middleButtonDown = false;
  int m_pressX = 0;
  int m_pressY = 0;
  ecs::EntityOwner m_markerOwner; ///< 三人称視点用の3D旗マーカーEntity群
};

} // namespace game::controllers
