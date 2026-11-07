#pragma once

/**
 * @file OcclusionFadeRules.h
 * @brief カメラとボールの間に入った物体を薄くする割合を決めます。
 * @details 描画はロボットと同じドット状ディザーフェード(MeshRenderer の
 * usesDitherFade と color.w)を使います。ここでは描画率だけを計算します。
*/

#include <DirectXMath.h>
#include <algorithm>

#include "FlagFadeRules.h"

namespace game::utils {

/// 物体の形(半径0.5の単位メッシュ)を何倍まで膨らませた範囲で薄くし始めるか
inline constexpr float kOcclusionFadeVisibleScale = 1.7f;
/// 物体の形に線が触れたときの描画率。0 で完全に消えます
inline constexpr float kOcclusionFadeMinAlpha = 0.0f;

/**
 * @brief カメラからボールへの線分が物体を通るときの描画率を返します。
 * @param cameraPosition カメラ位置
 * @param targetPosition ボール位置
 * @param objectPosition 物体の位置
 * @param objectRotation 物体の回転(クォータニオン)
 * @param objectScale 物体の拡大率。メッシュは半径0.5の単位形状とみなします
 * @return 1 で通常表示、kOcclusionFadeMinAlpha で最も薄い状態
 * @details 線分を物体のローカル空間へ移し、原点(物体の中心)までの最短距離で
 * 判定します。細長い幹や平たい岩でも形に沿って判定できます。
*/
inline float CalculateOcclusionFadeAlpha(
    const DirectX::XMFLOAT3 &cameraPosition,
    const DirectX::XMFLOAT3 &targetPosition,
    const DirectX::XMFLOAT3 &objectPosition,
    const DirectX::XMFLOAT4 &objectRotation,
    const DirectX::XMFLOAT3 &objectScale) {
  using namespace DirectX;
  constexpr float kMinScale = 0.0001f;
  const XMVECTOR scale = XMVectorMax(XMLoadFloat3(&objectScale),
                                     XMVectorReplicate(kMinScale));
  const XMVECTOR position = XMLoadFloat3(&objectPosition);
  const XMVECTOR inverseRotation =
      XMQuaternionInverse(XMLoadFloat4(&objectRotation));

  auto toLocal = [&](const XMFLOAT3 &world) {
    const XMVECTOR offset = XMVectorSubtract(XMLoadFloat3(&world), position);
    return XMVectorDivide(XMVector3Rotate(offset, inverseRotation), scale);
  };
  const XMVECTOR localCamera = toLocal(cameraPosition);
  const XMVECTOR localTarget = toLocal(targetPosition);

  // 線分上で原点に最も近い点。ボールの向こう側やカメラの後ろは含めない
  const XMVECTOR segment = XMVectorSubtract(localTarget, localCamera);
  const float lengthSquared = XMVectorGetX(XMVector3LengthSq(segment));
  float t = 0.0f;
  if (lengthSquared > 1e-8f) {
    t = -XMVectorGetX(XMVector3Dot(localCamera, segment)) / lengthSquared;
    t = std::clamp(t, 0.0f, 1.0f);
  }
  const XMVECTOR closest = XMVectorAdd(localCamera, XMVectorScale(segment, t));
  const float distance = XMVectorGetX(XMVector3Length(closest));

  constexpr float kMeshRadius = 0.5f;
  const float visibility = SmoothStep(
      kMeshRadius, kMeshRadius * kOcclusionFadeVisibleScale, distance);
  return kOcclusionFadeMinAlpha +
         (1.0f - kOcclusionFadeMinAlpha) * visibility;
}

} // namespace game::utils
