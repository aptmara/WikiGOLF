#pragma once

#include <DirectXMath.h>
#include <algorithm>
#include <cmath>

namespace game::utils {

inline constexpr float kFlagFadeHiddenDistance = 1.25f;
inline constexpr float kFlagFadeVisibleDistance = 4.0f;
inline constexpr float kFlagOcclusionHiddenRadius = 0.65f;
inline constexpr float kFlagOcclusionVisibleRadius = 1.75f;

inline float SmoothStep(float edge0, float edge1, float value) {
  const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

inline float CalculateFlagFadeAlpha(
    const DirectX::XMFLOAT3 &cameraPosition,
    const DirectX::XMFLOAT3 &flagPosition,
    const DirectX::XMFLOAT3 &golferPosition) {
  const float cameraToFlagX = flagPosition.x - cameraPosition.x;
  const float cameraToFlagY = flagPosition.y - cameraPosition.y;
  const float cameraToFlagZ = flagPosition.z - cameraPosition.z;
  const float cameraDistance = std::sqrt(
      cameraToFlagX * cameraToFlagX + cameraToFlagY * cameraToFlagY +
      cameraToFlagZ * cameraToFlagZ);
  const float proximityAlpha = SmoothStep(
      kFlagFadeHiddenDistance, kFlagFadeVisibleDistance, cameraDistance);

  const float cameraToGolferX = golferPosition.x - cameraPosition.x;
  const float cameraToGolferY = golferPosition.y - cameraPosition.y;
  const float cameraToGolferZ = golferPosition.z - cameraPosition.z;
  const float golferDistanceSquared =
      cameraToGolferX * cameraToGolferX +
      cameraToGolferY * cameraToGolferY +
      cameraToGolferZ * cameraToGolferZ;
  if (golferDistanceSquared <= 0.0001f) {
    return proximityAlpha;
  }

  const float projection =
      (cameraToFlagX * cameraToGolferX +
       cameraToFlagY * cameraToGolferY +
       cameraToFlagZ * cameraToGolferZ) /
      golferDistanceSquared;
  if (projection <= 0.0f || projection >= 1.0f) {
    return proximityAlpha;
  }

  const float closestX = cameraPosition.x + cameraToGolferX * projection;
  const float closestY = cameraPosition.y + cameraToGolferY * projection;
  const float closestZ = cameraPosition.z + cameraToGolferZ * projection;
  const float offsetX = flagPosition.x - closestX;
  const float offsetY = flagPosition.y - closestY;
  const float offsetZ = flagPosition.z - closestZ;
  const float lineDistance = std::sqrt(
      offsetX * offsetX + offsetY * offsetY + offsetZ * offsetZ);
  const float occlusionAlpha = SmoothStep(
      kFlagOcclusionHiddenRadius, kFlagOcclusionVisibleRadius, lineDistance);
  return (std::min)(proximityAlpha, occlusionAlpha);
}

/**
 * @brief ロボットとボールのどちらかを遮っていれば薄くする描画率を返します。
 * @details カメラからロボットへの線と、カメラからボールへの線の両方で判定し、
 * 薄いほうを使います。ボールが飛んでロボットから離れても、ボールを隠しません。
*/
inline float CalculateFlagFadeAlpha(
    const DirectX::XMFLOAT3 &cameraPosition,
    const DirectX::XMFLOAT3 &flagPosition,
    const DirectX::XMFLOAT3 &golferPosition,
    const DirectX::XMFLOAT3 &ballPosition) {
  return (std::min)(
      CalculateFlagFadeAlpha(cameraPosition, flagPosition, golferPosition),
      CalculateFlagFadeAlpha(cameraPosition, flagPosition, ballPosition));
}

inline float CalculateBillboardFadeAlpha(
    const DirectX::XMFLOAT3 &cameraPosition,
    const DirectX::XMFLOAT3 &billboardPosition,
    const DirectX::XMFLOAT4 &billboardRotation,
    const DirectX::XMFLOAT3 &billboardScale,
    const DirectX::XMFLOAT3 &targetPosition) {
  using namespace DirectX;

  const XMVECTOR camera = XMLoadFloat3(&cameraPosition);
  const XMVECTOR position = XMLoadFloat3(&billboardPosition);
  const float cameraDistance = XMVectorGetX(
      XMVector3Length(XMVectorSubtract(position, camera)));
  const float proximityAlpha = SmoothStep(
      kFlagFadeHiddenDistance, kFlagFadeVisibleDistance, cameraDistance);

  const XMVECTOR inverseRotation =
      XMQuaternionInverse(XMLoadFloat4(&billboardRotation));
  auto toLocal = [&](const XMFLOAT3 &worldPosition) {
    const XMVECTOR offset =
        XMVectorSubtract(XMLoadFloat3(&worldPosition), position);
    return XMVector3Rotate(offset, inverseRotation);
  };

  const XMVECTOR localCamera = toLocal(cameraPosition);
  const XMVECTOR localTarget = toLocal(targetPosition);
  const XMVECTOR segment = XMVectorSubtract(localTarget, localCamera);
  const float segmentZ = XMVectorGetZ(segment);
  if (std::fabs(segmentZ) <= 1e-6f) {
    return proximityAlpha;
  }

  const float intersectionT = -XMVectorGetZ(localCamera) / segmentZ;
  if (intersectionT <= 0.0f || intersectionT >= 1.0f) {
    return proximityAlpha;
  }

  const XMVECTOR intersection =
      XMVectorAdd(localCamera, XMVectorScale(segment, intersectionT));
  const float halfWidth = std::fabs(billboardScale.x) * 0.5f;
  const float halfHeight = std::fabs(billboardScale.y) * 0.5f;
  const float distanceX =
      (std::max)(std::fabs(XMVectorGetX(intersection)) - halfWidth, 0.0f);
  const float distanceY =
      (std::max)(std::fabs(XMVectorGetY(intersection)) - halfHeight, 0.0f);
  const float rectangleDistance =
      std::sqrt(distanceX * distanceX + distanceY * distanceY);
  const float occlusionAlpha = SmoothStep(
      kFlagOcclusionHiddenRadius, kFlagOcclusionVisibleRadius,
      rectangleDistance);
  return (std::min)(proximityAlpha, occlusionAlpha);
}

inline float CalculateBillboardFadeAlpha(
    const DirectX::XMFLOAT3 &cameraPosition,
    const DirectX::XMFLOAT3 &billboardPosition,
    const DirectX::XMFLOAT4 &billboardRotation,
    const DirectX::XMFLOAT3 &billboardScale,
    const DirectX::XMFLOAT3 &golferPosition,
    const DirectX::XMFLOAT3 &ballPosition) {
  return (std::min)(
      CalculateBillboardFadeAlpha(
          cameraPosition, billboardPosition, billboardRotation,
          billboardScale, golferPosition),
      CalculateBillboardFadeAlpha(
          cameraPosition, billboardPosition, billboardRotation,
          billboardScale, ballPosition));
}

} // namespace game::utils
