#pragma once

#include <DirectXCollision.h>
#include <DirectXMath.h>

namespace game::systems::shadow_detail {

inline constexpr float kCasterDistance = 50.0f;
inline constexpr float kScreenMargin = 8.0f;

inline bool ShouldRenderFrame(bool isMapView) {
  return !isMapView;
}

inline bool IsNearFocus(const DirectX::BoundingSphere &bounds,
                        const DirectX::XMFLOAT3 &focus) {
  const float dx = bounds.Center.x - focus.x;
  const float dz = bounds.Center.z - focus.z;
  const float maximumDistance = kCasterDistance + bounds.Radius;
  return dx * dx + dz * dz <= maximumDistance * maximumDistance;
}

} // namespace game::systems::shadow_detail
