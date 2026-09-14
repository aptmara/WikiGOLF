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

} // namespace game::utils
