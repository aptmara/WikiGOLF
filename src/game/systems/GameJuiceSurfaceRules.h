#pragma once

#include "../utils/TerrainBounds.h"
#include "../utils/GameplayPhysicsConstants.h"
#include <DirectXMath.h>

namespace game::systems::juice_detail {

struct EffectSurface {
  DirectX::XMFLOAT3 position;
  DirectX::XMFLOAT3 normal{0.0f, 1.0f, 0.0f};
};

inline EffectSurface SampleEffectSurface(const TerrainData *terrain,
                                         const DirectX::XMFLOAT3 &position) {
  EffectSurface surface{position};
  if (!terrain || !game::utils::HasValidTerrainGrid(*terrain)) {
    return surface;
  }
  const auto bounds = game::utils::CalculateTerrainWorldBounds(*terrain);
  if (!bounds.Contains(position.x, position.z)) {
    return surface;
  }
  const float x0 = (std::max)(bounds.minX, position.x - 0.05f);
  const float x1 = (std::min)(bounds.maxX, position.x + 0.05f);
  const float z0 = (std::max)(bounds.minZ, position.z - 0.05f);
  const float z1 = (std::min)(bounds.maxZ, position.z + 0.05f);
  const float slopeX = (game::utils::SampleTerrainHeight(*terrain, x1, position.z) -
                        game::utils::SampleTerrainHeight(*terrain, x0, position.z)) / (x1 - x0);
  const float slopeZ = (game::utils::SampleTerrainHeight(*terrain, position.x, z1) -
                        game::utils::SampleTerrainHeight(*terrain, position.x, z0)) / (z1 - z0);
  DirectX::XMStoreFloat3(&surface.normal, DirectX::XMVector3Normalize(
      DirectX::XMVectorSet(-slopeX, 1.0f, -slopeZ, 0.0f)));
  surface.position.y = game::physics::ToVisualSurfaceHeight(
      game::utils::SampleTerrainHeight(*terrain, position.x, position.z));
  return surface;
}

inline DirectX::XMFLOAT4 SurfaceRotation(const DirectX::XMFLOAT3 &normal,
                                        float yaw = 0.0f) {
  using namespace DirectX;
  const XMVECTOR up = XMLoadFloat3(&normal);
  const XMVECTOR right = XMVector3Normalize(XMVector3Cross(
      XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), up));
  const XMVECTOR forward = XMVector3Cross(right, up);
  // ローカルYを法線へ揃え、接平面内の回転を保持する。
  XMMATRIX basis(-right, up, -forward, XMVectorSet(0, 0, 0, 1));
  XMFLOAT4 rotation;
  XMStoreFloat4(&rotation, XMQuaternionRotationMatrix(
      XMMatrixRotationY(yaw) * basis));
  return rotation;
}

} // namespace game::systems::juice_detail
