#pragma once
/**
 * @file GrassFieldRenderer.h
 * @brief 手続き的に生やす芝の GPU 描画（タイルの判定・風・踏み跡を計算シェーダーで行う）
 */

#include "../../core/GameContext.h"
#include <DirectXMath.h>
#include <d3d11.h>

namespace game::systems {

/** @brief 芝の描画に渡す1フレーム分のカメラ・影などの情報 */
struct GrassFieldFrameInput {
  /** 描画用の View*Projection（ジッター込み、転置前） */
  DirectX::XMMATRIX viewProjection;
  /** 判定用の View*Projection（ジッター無し、転置前） */
  DirectX::XMMATRIX cullViewProjection;
  /** xyz: カメラ位置、w: フォグ有効(1/0) */
  DirectX::XMFLOAT4 cameraPosition;
  /** 転置前 */
  DirectX::XMMATRIX shadowViewProjection;
  bool shadowEnabled = false;
  ID3D11ShaderResourceView *shadowMap = nullptr;
  ID3D11SamplerState *shadowSampler = nullptr;
  /** GolfCupClip.hlsli の GolfCupBuffer */
  ID3D11Buffer *golfCupBuffer = nullptr;
};

/**
 * @brief GrassFieldSource があれば芝を描きます。
 * @details シーンの描画先（カラー・速度・深度）を設定した後に呼ぶ。
 *          画質プリセットは毎フレーム読み、変わればすぐ反映する。
 */
void RenderGrassField(core::GameContext &ctx, const GrassFieldFrameInput &input);

/**
 * @brief 芝の元データと踏み跡の入力を消し、GPU 上のフィールドも解放します。
 * @details World はシーンをまたいで共有されるので、芝を生やしたシーンの終了時に呼ぶ。
 */
void ClearGrassField(core::GameContext &ctx);

} // namespace game::systems
