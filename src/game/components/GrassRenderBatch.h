#pragma once
/**
 * @file GrassRenderBatch.h
 * @brief 空間チャンク単位で描画する芝インスタンス群
 */

#include "../../resources/ResourceManager.h"
#include <DirectXMath.h>
#include <cstddef>
#include <limits>
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
  resources::ShaderHandle shader;
  float maxDrawDistance = 0.0f;
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

} // namespace game::components
