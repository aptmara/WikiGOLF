#pragma once
/**
 * @file MeshRenderer.h
 * @brief MeshRenderer Component
 */

#include "../../resources/ResourceManager.h"
#include <d3d11.h>
#include <wrl/client.h>

namespace game::components {

struct MeshRenderer {
  resources::MeshHandle mesh;
  resources::ShaderHandle shader;
  bool isVisible = true;
  DirectX::XMFLOAT4 color = {1.0f, 1.0f, 1.0f, 1.0f}; // マテリアルカラー

  // テクスチャ（オプション）
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> textureSRV;
  bool hasTexture = false;
};

} // namespace game::components
