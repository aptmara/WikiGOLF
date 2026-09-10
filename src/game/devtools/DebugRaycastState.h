#pragma once
/**
 * @file DebugRaycastState.h
 * @brief 中クリックのレイキャスト結果をデバッグオーバーレイへ橋渡しするグローバル状態
*/

#include <DirectXMath.h>

namespace game::debug {

/**
 * @brief AimPinControllerが直近に実行したレイキャストの結果。
 * @details AimPinController::Updateが中クリックのたびに更新し、
 *          DebugColliderRendererがこれを読んでワールド空間に線分として
 *          描画する（衝突タブの「中クリックのレイキャストを表示」）。
*/
struct DebugRaycastState {
  bool hasRay = false; ///< 一度でもレイキャストが実行されたか
  DirectX::XMFLOAT3 origin{0.0f, 0.0f, 0.0f};
  DirectX::XMFLOAT3 direction{0.0f, 0.0f, 0.0f}; ///< 正規化済み
  bool hit = false;
  DirectX::XMFLOAT3 hitPosition{0.0f, 0.0f, 0.0f};
  float maxDistance = 0.0f;
};

} // namespace game::debug
