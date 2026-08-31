#include "SkyboxRenderSystem.h"
#include "../../ecs/World.h"
#include "../../graphics/GraphicsDevice.h"
#include "../../graphics/MeshPrimitives.h"
#include "../../resources/ResourceManager.h"
#include "../components/Camera.h"
#include "../components/Skybox.h"
#include "../components/Transform.h"
#include <DirectXMath.h>
#include <d3d11.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

namespace game::systems {

namespace {

/**
 * @brief スカイボックス用定数バッファ
 */
struct SkyboxConstants {
  XMMATRIX view;
  XMMATRIX projection;
  XMFLOAT4 tintColor;
  float brightness;
  float saturation;
  float time;
  float padding;
  XMFLOAT3 sunDirection;
  float padding2;
};

/**
 * @brief スカイボックスレンダリング用グローバルステート
 */
struct SkyboxRenderState {
  ComPtr<ID3D11Buffer> constantBuffer;
  ComPtr<ID3D11Buffer> vertexBuffer;
  ComPtr<ID3D11Buffer> indexBuffer;
  ComPtr<ID3D11SamplerState> samplerState;
  ComPtr<ID3D11DepthStencilState> depthStencilState;
  ComPtr<ID3D11RasterizerState> rasterizerState;
  uint32_t indexCount = 0;
  bool initialized = false;
};

/**
 * @brief スカイボックス用キューブメッシュ初期化
 */
bool InitializeSkyboxMesh(ID3D11Device *device, SkyboxRenderState &state) {
  // キューブの頂点（中心原点）
  using graphics::Vertex;
  const float s = 500.0f; // サイズを大きくする
  Vertex vertices[] = {
      // 各面の頂点
      {{-s, -s, -s}, {0, 0, 0}, {0, 0}, {1, 1, 1, 1}, {1, 0, 0}, {0, 1, 0}},
      {{-s, -s, s}, {0, 0, 0}, {0, 0}, {1, 1, 1, 1}, {1, 0, 0}, {0, 1, 0}},
      {{-s, s, -s}, {0, 0, 0}, {0, 0}, {1, 1, 1, 1}, {1, 0, 0}, {0, 1, 0}},
      {{-s, s, s}, {0, 0, 0}, {0, 0}, {1, 1, 1, 1}, {1, 0, 0}, {0, 1, 0}},
      {{s, -s, -s}, {0, 0, 0}, {0, 0}, {1, 1, 1, 1}, {1, 0, 0}, {0, 1, 0}},
      {{s, -s, s}, {0, 0, 0}, {0, 0}, {1, 1, 1, 1}, {1, 0, 0}, {0, 1, 0}},
      {{s, s, -s}, {0, 0, 0}, {0, 0}, {1, 1, 1, 1}, {1, 0, 0}, {0, 1, 0}},
      {{s, s, s}, {0, 0, 0}, {0, 0}, {1, 1, 1, 1}, {1, 0, 0}, {0, 1, 0}},
  };

  // インデックス（内側から見るため反時計回り）
  uint16_t indices[] = {
      // -X
      0,
      2,
      1,
      1,
      2,
      3,
      // +X
      4,
      5,
      6,
      5,
      7,
      6,
      // -Y
      0,
      1,
      4,
      1,
      5,
      4,
      // +Y
      2,
      6,
      3,
      3,
      6,
      7,
      // -Z
      0,
      4,
      2,
      2,
      4,
      6,
      // +Z
      1,
      3,
      5,
      3,
      7,
      5,
  };

  state.indexCount = sizeof(indices) / sizeof(uint16_t);

  // 頂点バッファ作成
  D3D11_BUFFER_DESC vbDesc = {};
  vbDesc.ByteWidth = sizeof(vertices);
  vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
  vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

  D3D11_SUBRESOURCE_DATA vbData = {};
  vbData.pSysMem = vertices;

  HRESULT hr = device->CreateBuffer(&vbDesc, &vbData, &state.vertexBuffer);
  if (FAILED(hr)) {
    return false;
  }

  // インデックスバッファ作成
  D3D11_BUFFER_DESC ibDesc = {};
  ibDesc.ByteWidth = sizeof(indices);
  ibDesc.Usage = D3D11_USAGE_IMMUTABLE;
  ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

  D3D11_SUBRESOURCE_DATA ibData = {};
  ibData.pSysMem = indices;

  hr = device->CreateBuffer(&ibDesc, &ibData, &state.indexBuffer);
  if (FAILED(hr)) {
    return false;
  }

  return true;
}

/**
 * @brief スカイボックス用ステート初期化
 */
bool InitializeSkyboxStates(ID3D11Device *device, SkyboxRenderState &state) {
  // 定数バッファ
  D3D11_BUFFER_DESC cbDesc = {};
  cbDesc.ByteWidth = sizeof(SkyboxConstants);
  cbDesc.Usage = D3D11_USAGE_DYNAMIC;
  cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

  HRESULT hr = device->CreateBuffer(&cbDesc, nullptr, &state.constantBuffer);
  if (FAILED(hr)) {
    return false;
  }

  // サンプラーステート
  D3D11_SAMPLER_DESC sampDesc = {};
  sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
  sampDesc.MinLOD = 0;
  sampDesc.MaxLOD = D3D11_FLOAT32_MAX;

  hr = device->CreateSamplerState(&sampDesc, &state.samplerState);
  if (FAILED(hr)) {
    return false;
  }

  // 深度ステンシルステート（深度書き込み無効、最遠景描画）
  D3D11_DEPTH_STENCIL_DESC dsDesc = {};
  dsDesc.DepthEnable = FALSE; // 深度テスト無効（常に最背面）
  dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
  dsDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
  dsDesc.StencilEnable = FALSE;

  hr = device->CreateDepthStencilState(&dsDesc, &state.depthStencilState);
  if (FAILED(hr)) {
    return false;
  }

  // ラスタライザーステート（カリングなし）
  D3D11_RASTERIZER_DESC rsDesc = {};
  rsDesc.FillMode = D3D11_FILL_SOLID;
  rsDesc.CullMode = D3D11_CULL_NONE; // カリングなし
  rsDesc.FrontCounterClockwise = FALSE;
  rsDesc.DepthClipEnable = TRUE;

  hr = device->CreateRasterizerState(&rsDesc, &state.rasterizerState);
  if (FAILED(hr)) {
    return false;
  }

  return true;
}

} // namespace

void SkyboxRenderSystem(core::GameContext &ctx) {
  auto *device = ctx.graphics.GetDevice();
  auto *context = ctx.graphics.GetContext();
  auto &world = ctx.world;

  // グローバルステート取得または初期化
  auto *state = world.GetGlobal<SkyboxRenderState>();
  if (!state) {
    SkyboxRenderState newState;
    if (!InitializeSkyboxMesh(device, newState)) {
      return;
    }
    if (!InitializeSkyboxStates(device, newState)) {
      return;
    }
    newState.initialized = true;
    world.SetGlobal(std::move(newState));
    state = world.GetGlobal<SkyboxRenderState>();
  }

  if (!state || !state->initialized) {
    return;
  }

  // カメラ情報取得
  XMMATRIX view = XMMatrixIdentity();
  XMMATRIX proj = XMMatrixIdentity();
  bool cameraFound = false;

  world.Query<components::Transform, components::Camera>().Each(
      [&](ecs::Entity, components::Transform &t, components::Camera &c) {
        if (!cameraFound) {
          view = c.GetViewMatrix(t);
          // RenderSystemと同じく、アスペクト比は現在の実描画サイズから都度計算する。
          proj = XMMatrixPerspectiveFovLH(c.fov, ctx.graphics.GetAspectRatio(),
                                          c.nearZ, c.farZ);
          cameraFound = true;
        }
      });

  if (!cameraFound) {
    return; // カメラがなければ描画しない
  }

  // スカイボックスシェーダー取得
  auto skyboxShaderHandle = ctx.resource.LoadShader(
      "Skybox", L"Assets/shaders/SkyboxVS.hlsl", L"Assets/shaders/SkyboxPS.hlsl");
  auto skyboxShader = ctx.resource.GetShader(skyboxShaderHandle);
  if (!skyboxShader) {
    LOG_WARN("Skybox", "Skybox shader not loaded");
    return;
  }
  if (!skyboxShader->IsValid()) {
    LOG_WARN("Skybox", "Skybox shader is loaded but not valid!");
    return;
  }

  // 転置（HLSLは列優先）
  view = XMMatrixTranspose(view);
  proj = XMMatrixTranspose(proj);

  // スカイボックスコンポーネントを持つエンティティを描画
  world.Query<components::Skybox>().Each([&](ecs::Entity e,
                                             components::Skybox &skybox) {
    if (!skybox.isVisible || !skybox.cubemapSRV) {
      return;
    }

    // 定数バッファ更新
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(context->Map(state->constantBuffer.Get(), 0,
                               D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
      SkyboxConstants *constants = static_cast<SkyboxConstants *>(mapped.pData);
      constants->view = view;
      constants->projection = proj;
      constants->tintColor = skybox.tintColor;
      constants->brightness = skybox.brightness;
      constants->saturation = skybox.saturation;
      constants->time = skybox.time;
      constants->sunDirection = skybox.sunDirection;
      context->Unmap(state->constantBuffer.Get(), 0);
    }

    // シェーダーバインド
    skyboxShader->Bind(context);

    // 定数バッファバインド
    context->VSSetConstantBuffers(0, 1, state->constantBuffer.GetAddressOf());
    context->PSSetConstantBuffers(0, 1, state->constantBuffer.GetAddressOf());

    // テクスチャバインド
    context->PSSetShaderResources(0, 1, skybox.cubemapSRV.GetAddressOf());
    context->PSSetSamplers(0, 1, state->samplerState.GetAddressOf());

    // ステートバインド
    context->OMSetDepthStencilState(state->depthStencilState.Get(), 0);
    context->RSSetState(state->rasterizerState.Get());

    // 頂点バッファバインド
    UINT stride = sizeof(graphics::Vertex);
    UINT offset = 0;
    context->IASetVertexBuffers(0, 1, state->vertexBuffer.GetAddressOf(),
                                &stride, &offset);
    context->IASetIndexBuffer(state->indexBuffer.Get(), DXGI_FORMAT_R16_UINT,
                              0);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // 描画
    context->DrawIndexed(state->indexCount, 0, 0);

    // ステートリセット
    context->OMSetDepthStencilState(nullptr, 0);
    context->RSSetState(nullptr);
  });
}

} // namespace game::systems
