/**
 * @file ParticleRenderSystem.h
 * @brief パーティクルレンダリングシステム - ビルボードスプライト描画
 */

#pragma once

#include "../../core/GameContext.h"
#include "../../graphics/Shader.h"
#include "../components/Camera.h"
#include "../components/Transform.h"
#include "ParticleSystem.h"
#include <d3d11.h>
#include <wrl/client.h>

namespace game::systems {

using Microsoft::WRL::ComPtr;
using namespace DirectX;

/**
 * @brief パーティクル用頂点
 */
struct ParticleVertex {
  XMFLOAT3 position;
  XMFLOAT2 texCoord;
  XMFLOAT4 color;
  float size;
};

/**
 * @brief パーティクルレンダリングシステム
 */
class ParticleRenderSystem {
public:
  /**
   * @brief 初期化
   * @param device DirectX11デバイス
   * @return 成功ならtrue
   */
  bool Initialize(ID3D11Device *device) {
    m_device = device;

    // 定数バッファ作成
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth = sizeof(ParticleConstants);
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = device->CreateBuffer(&cbDesc, nullptr, &m_constantBuffer);
    if (FAILED(hr))
      return false;

    // 頂点バッファ作成（動的、最大パーティクル数）
    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.ByteWidth =
        sizeof(ParticleVertex) * kMaxParticles * 4; // 4頂点/パーティクル
    vbDesc.Usage = D3D11_USAGE_DYNAMIC;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    hr = device->CreateBuffer(&vbDesc, nullptr, &m_vertexBuffer);
    if (FAILED(hr))
      return false;

    // インデックスバッファ作成
    std::vector<uint16_t> indices;
    indices.reserve(kMaxParticles * 6);
    for (uint16_t i = 0; i < kMaxParticles; ++i) {
      uint16_t base = i * 4;
      indices.push_back(base + 0);
      indices.push_back(base + 1);
      indices.push_back(base + 2);
      indices.push_back(base + 2);
      indices.push_back(base + 1);
      indices.push_back(base + 3);
    }

    D3D11_BUFFER_DESC ibDesc = {};
    ibDesc.ByteWidth = static_cast<UINT>(indices.size() * sizeof(uint16_t));
    ibDesc.Usage = D3D11_USAGE_IMMUTABLE;
    ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

    D3D11_SUBRESOURCE_DATA ibData = {};
    ibData.pSysMem = indices.data();

    hr = device->CreateBuffer(&ibDesc, &ibData, &m_indexBuffer);
    if (FAILED(hr))
      return false;

    // ブレンドステート（加算合成）
    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask =
        D3D11_COLOR_WRITE_ENABLE_ALL;

    hr = device->CreateBlendState(&blendDesc, &m_blendState);
    if (FAILED(hr))
      return false;

    // 深度ステンシル（深度テスト有効、書き込み無効）
    D3D11_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable = TRUE;
    dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    dsDesc.DepthFunc = D3D11_COMPARISON_LESS;

    hr = device->CreateDepthStencilState(&dsDesc, &m_depthStencilState);
    if (FAILED(hr))
      return false;

    m_initialized = true;
    return true;
  }

  /**
   * @brief パーティクル描画
   * @param context デバイスコンテキスト
   * @param particles パーティクルリスト
   * @param view ビュー行列
   * @param proj プロジェクション行列
   * @param cameraRight カメラ右ベクトル（ビルボード用）
   * @param cameraUp カメラ上ベクトル（ビルボード用）
   */
  void Render(ID3D11DeviceContext *context,
              const std::vector<Particle> &particles, const XMMATRIX &view,
              const XMMATRIX &proj, const XMFLOAT3 &cameraRight,
              const XMFLOAT3 &cameraUp) {
    if (!m_initialized || particles.empty())
      return;

    size_t count =
        (std::min)(particles.size(), static_cast<size_t>(kMaxParticles));

    // 頂点バッファ更新
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(context->Map(m_vertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD,
                               0, &mapped))) {
      ParticleVertex *vertices = static_cast<ParticleVertex *>(mapped.pData);

      XMVECTOR right = XMLoadFloat3(&cameraRight);
      XMVECTOR up = XMLoadFloat3(&cameraUp);

      for (size_t i = 0; i < count; ++i) {
        const Particle &p = particles[i];
        XMVECTOR pos = XMLoadFloat3(&p.position);
        float halfSize = p.size * 0.5f;

        // ビルボード四角形の4隅
        XMVECTOR corners[4] = {
            XMVectorAdd(XMVectorAdd(pos, XMVectorScale(right, -halfSize)),
                        XMVectorScale(up, halfSize)),
            XMVectorAdd(XMVectorAdd(pos, XMVectorScale(right, halfSize)),
                        XMVectorScale(up, halfSize)),
            XMVectorAdd(XMVectorAdd(pos, XMVectorScale(right, -halfSize)),
                        XMVectorScale(up, -halfSize)),
            XMVectorAdd(XMVectorAdd(pos, XMVectorScale(right, halfSize)),
                        XMVectorScale(up, -halfSize)),
        };

        size_t base = i * 4;
        for (int j = 0; j < 4; ++j) {
          XMStoreFloat3(&vertices[base + j].position, corners[j]);
          vertices[base + j].color = p.color;
          vertices[base + j].size = p.size;
        }
        vertices[base + 0].texCoord = {0, 0};
        vertices[base + 1].texCoord = {1, 0};
        vertices[base + 2].texCoord = {0, 1};
        vertices[base + 3].texCoord = {1, 1};
      }

      context->Unmap(m_vertexBuffer.Get(), 0);
    }

    // 定数バッファ更新
    if (SUCCEEDED(context->Map(m_constantBuffer.Get(), 0,
                               D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
      ParticleConstants *constants =
          static_cast<ParticleConstants *>(mapped.pData);
      constants->viewProj = XMMatrixTranspose(XMMatrixMultiply(view, proj));
      context->Unmap(m_constantBuffer.Get(), 0);
    }

    // ステート設定
    context->OMSetBlendState(m_blendState.Get(), nullptr, 0xFFFFFFFF);
    context->OMSetDepthStencilState(m_depthStencilState.Get(), 0);

    // バッファバインド
    UINT stride = sizeof(ParticleVertex);
    UINT offset = 0;
    context->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride,
                                &offset);
    context->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    context->VSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());

    // 描画
    context->DrawIndexed(static_cast<UINT>(count * 6), 0, 0);

    // ステートリセット
    context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
    context->OMSetDepthStencilState(nullptr, 0);
  }

private:
  struct ParticleConstants {
    XMMATRIX viewProj;
  };

  static constexpr int kMaxParticles = 1000;

  ID3D11Device *m_device = nullptr;
  ComPtr<ID3D11Buffer> m_constantBuffer;
  ComPtr<ID3D11Buffer> m_vertexBuffer;
  ComPtr<ID3D11Buffer> m_indexBuffer;
  ComPtr<ID3D11BlendState> m_blendState;
  ComPtr<ID3D11DepthStencilState> m_depthStencilState;
  bool m_initialized = false;
};

} // namespace game::systems
