#include "RenderSystem.h"
#include "../../ecs/World.h"
#include "../../graphics/GraphicsDevice.h"
#include "../../resources/ResourceManager.h"
#include "../components/Camera.h"
#include "../components/MeshRenderer.h"
#include "../components/Transform.h"
#include <DirectXMath.h>
#include <d3d11.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

namespace game::systems {

struct VSConstants {
  XMMATRIX world;
  XMMATRIX view;
  XMMATRIX projection;
  XMFLOAT4 materialColor;
  XMFLOAT4 lightDir;
  XMFLOAT4 cameraPos;
};

struct RenderState {
  ComPtr<ID3D11Buffer> cBuffer;
  ComPtr<ID3D11SamplerState> sampler;
};

void RenderSystem(core::GameContext &ctx) {
  auto *device = ctx.graphics.GetDevice();
  auto *context = ctx.graphics.GetContext();
  auto &world = ctx.world;

  // 1. 定数バッファの取得または作成（Global Dataを使用）
  auto *state = world.GetGlobal<RenderState>();
  if (!state) {
    RenderState newState;
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(VSConstants);
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    device->CreateBuffer(&desc, nullptr, &newState.cBuffer);

    // サンプラーステート作成
    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MinLOD = 0;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
    device->CreateSamplerState(&sampDesc, &newState.sampler);

    world.SetGlobal(std::move(newState));
    state = world.GetGlobal<RenderState>();
  }

  // 2. カメラ情報の取得
  XMMATRIX view = XMMatrixIdentity();
  XMMATRIX proj = XMMatrixIdentity();
  XMFLOAT4 camPos = {0, 0, 0, 1};

  bool cameraFound = false;
  world.Query<components::Transform, components::Camera>().Each(
      [&](ecs::Entity, components::Transform &t, components::Camera &c) {
        if (!cameraFound) {
          view = c.GetViewMatrix(t);
          proj = c.GetProjectionMatrix();
          camPos = {t.position.x, t.position.y, t.position.z, 1.0f};
          cameraFound = true;
        }
      });

  if (!cameraFound) {
    // フォールバックカメラ
    view = XMMatrixLookAtLH(XMVectorSet(0, 0, -5, 1), XMVectorSet(0, 0, 0, 1),
                            XMVectorSet(0, 1, 0, 0));
    proj = XMMatrixPerspectiveFovLH(XM_PIDIV4, 16.0f / 9.0f, 0.01f, 100.0f);
  }

  // 転置（HLSLは列優先）
  view = XMMatrixTranspose(view);
  proj = XMMatrixTranspose(proj);

  // 3. レンダリングループ
  context->VSSetConstantBuffers(0, 1, state->cBuffer.GetAddressOf());

  world.Query<components::Transform, components::MeshRenderer>().Each(
      [&](ecs::Entity e, components::Transform &t,
          components::MeshRenderer &r) {
        if (!r.isVisible)
          return;

        auto *mesh = ctx.resource.GetMesh(r.mesh);
        auto *shader = ctx.resource.GetShader(r.shader);

        if (mesh && shader) {
          shader->Bind(context);

          // 定数バッファ更新
          D3D11_MAPPED_SUBRESOURCE mapped;
          if (SUCCEEDED(context->Map(state->cBuffer.Get(), 0,
                                     D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            VSConstants *constants = static_cast<VSConstants *>(mapped.pData);
            constants->world = XMMatrixTranspose(t.GetWorldMatrix());
            constants->view = view;
            constants->projection = proj;
            constants->materialColor = r.color;
            // 簡易ライティング用 (左上奥からの光)
            constants->lightDir = {0.5f, -1.0f, 0.5f, 0.0f}; 
            constants->cameraPos = camPos;
            context->Unmap(state->cBuffer.Get(), 0);
          }

          // テクスチャバインド（あれば）
          if (r.hasTexture && r.textureSRV) {
            context->PSSetShaderResources(0, 1, r.textureSRV.GetAddressOf());
            context->PSSetSamplers(0, 1, state->sampler.GetAddressOf());
          } else {
            ID3D11ShaderResourceView *nullSRV = nullptr;
            context->PSSetShaderResources(0, 1, &nullSRV);
          }

          mesh->Bind(context);
          mesh->Draw(context);
        }
      });
}

} // namespace game::systems
