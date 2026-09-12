#pragma once
/**
 * @file ShadowRenderSystem.h
 * @brief 平行光源用リアルタイムシャドウマップ描画
 */

#include "../../core/GameContext.h"
#include "../../graphics/Shader.h"
#include <DirectXMath.h>
#include <d3d11.h>
#include <wrl/client.h>

namespace game::systems {

struct ShadowRenderState {
  graphics::Shader depthShader;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> depthTexture;
  Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depthView;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shaderResourceView;
  Microsoft::WRL::ComPtr<ID3D11SamplerState> comparisonSampler;
  Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizerState;
  Microsoft::WRL::ComPtr<ID3D11Buffer> constantBuffer;
  DirectX::XMMATRIX lightViewProjection = DirectX::XMMatrixIdentity();
  bool initialized = false;
  bool validThisFrame = false;
};

void ShadowRenderSystem(core::GameContext &ctx);

} // namespace game::systems
