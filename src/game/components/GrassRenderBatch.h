#pragma once
/**
 * @file GrassRenderBatch.h
 * @brief 空間チャンク単位で描画する芝インスタンス群
 */

#include "../../resources/ResourceManager.h"
#include "../../ecs/Entity.h"
#include <DirectXMath.h>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

namespace game::components {

struct GrassRenderInstance {
  DirectX::XMFLOAT4X4 world;
  DirectX::XMFLOAT4 color = {1.0f, 1.0f, 1.0f, 1.0f};
  DirectX::XMFLOAT4 flags = {100000.0f, 100000.0f, 0.0f, 0.0f};
  DirectX::XMFLOAT3 position = {0.0f, 0.0f, 0.0f};
};

struct GrassRenderBatch {
  resources::MeshHandle mesh;
  resources::MeshHandle lodMesh = resources::MeshHandle::Invalid();
  resources::ShaderHandle shader;
  float lodSwitchDistance = 0.0f;
  float maxDrawDistance = 0.0f;
  float maxThreeDOverheadRatio = 1.1f;
  bool twoSided = false;
  DirectX::XMFLOAT3 boundsMin = {
      (std::numeric_limits<float>::max)(),
      (std::numeric_limits<float>::max)(),
      (std::numeric_limits<float>::max)()};
  DirectX::XMFLOAT3 boundsMax = {
      (std::numeric_limits<float>::lowest)(),
      (std::numeric_limits<float>::lowest)(),
      (std::numeric_limits<float>::lowest)()};
  std::vector<GrassRenderInstance> instances;
};

struct GrassRenderSpatialIndex {
  static uint64_t MakeKey(int chunkX, int chunkZ) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(chunkX)) << 32u) |
           static_cast<uint32_t>(chunkZ);
  }

  float chunkSize = 0.0f;
  float maxDrawDistance = 0.0f;
  float maxHorizontalExtent = 0.0f;
  std::unordered_map<uint64_t, std::vector<ecs::Entity>> batchesByChunk;
};

} // namespace game::components
