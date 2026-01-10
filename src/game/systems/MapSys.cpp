#include "MapSys.h"
#include "../../core/GameContext.h"
#include "../../core/Logger.h"
#include "../../ecs/World.h"
#include "../../graphics/GraphicsDevice.h"
#include "../../graphics/Mesh.h"
#include "../../graphics/Shader.h"
#include "../../resources/ResourceManager.h"
#include "../components/MeshRenderer.h"
#include "../components/WikiComponents.h"
#include "../components/Transform.h"
#include <algorithm>

using namespace DirectX;

namespace game::systems {

bool MapSys::Initialize(ID3D11Device *device, int width, int height) {
  m_width = width;
  m_height = height;

  D3D11_TEXTURE2D_DESC td = {};
  td.Width = width;
  td.Height = height;
  td.MipLevels = 1;
  td.ArraySize = 1;
  td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  td.SampleDesc.Count = 1;
  td.Usage = D3D11_USAGE_DEFAULT;
  td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
  if (FAILED(device->CreateTexture2D(&td, nullptr, &m_rt)))
    return false;
  if (FAILED(device->CreateRenderTargetView(m_rt.Get(), nullptr, &m_rtv)))
    return false;
  if (FAILED(device->CreateShaderResourceView(m_rt.Get(), nullptr, &m_srv)))
    return false;

  D3D11_TEXTURE2D_DESC dd = {};
  dd.Width = width;
  dd.Height = height;
  dd.MipLevels = 1;
  dd.ArraySize = 1;
  dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
  dd.SampleDesc.Count = 1;
  dd.Usage = D3D11_USAGE_DEFAULT;
  dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
  if (FAILED(device->CreateTexture2D(&dd, nullptr, &m_ds)))
    return false;
  if (FAILED(device->CreateDepthStencilView(m_ds.Get(), nullptr, &m_dsv)))
    return false;

  m_vp = {0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f};

  D3D11_BUFFER_DESC bd = {};
  bd.ByteWidth = sizeof(XMMATRIX) * 3 + sizeof(XMFLOAT4);
  bd.Usage = D3D11_USAGE_DYNAMIC;
  bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  if (FAILED(device->CreateBuffer(&bd, nullptr, &m_cb)))
    return false;

  D3D11_SAMPLER_DESC sd = {};
  sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
  sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
  sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
  sd.MaxLOD = D3D11_FLOAT32_MAX;
  device->CreateSamplerState(&sd, &m_samp);

  return true;
}

void MapSys::BeginRender(ID3D11DeviceContext *ctx) {
  UINT n = 1;
  ctx->OMGetRenderTargets(1, m_saveRTV.ReleaseAndGetAddressOf(),
                          m_saveDSV.ReleaseAndGetAddressOf());
  ctx->RSGetViewports(&n, &m_saveVP);
  ctx->OMSetRenderTargets(1, m_rtv.GetAddressOf(), m_dsv.Get());
  ctx->RSSetViewports(1, &m_vp);
  float color[] = {0.1f, 0.1f, 0.15f, 0.8f};
  ctx->ClearRenderTargetView(m_rtv.Get(), color);
  ctx->ClearDepthStencilView(m_dsv.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
}

void MapSys::EndRender(ID3D11DeviceContext *ctx) {
  ctx->OMSetRenderTargets(1, m_saveRTV.GetAddressOf(), m_saveDSV.Get());
  ctx->RSSetViewports(1, &m_saveVP);
}

XMMATRIX MapSys::GetViewMatrix(float cx, float cz, float h) {
  return XMMatrixLookAtLH(XMVectorSet(cx, h, cz, 1), XMVectorSet(cx, 0, cz, 1),
                          XMVectorSet(0, 0, 1, 0));
}

XMMATRIX MapSys::GetProjMatrix(float w, float d) {
  return XMMatrixOrthographicLH(w * 1.2f, d * 1.2f, 0.1f, 1000.0f);
}

struct MapVSConst {
  XMMATRIX w, v, p;
  XMFLOAT4 c;
};

void MapSys::RenderMinimap(core::GameContext &ctx) {
  auto *context = ctx.graphics.GetContext();
  BeginRender(context);

  float fw = 50.0f, fd = 50.0f;
  auto *state = ctx.world.GetGlobal<game::components::GolfGameState>();
  if (state) {
    fw = std::max(fw, state->fieldWidth);
    fd = std::max(fd, state->fieldDepth);
  }
  float extent = std::max(fw, fd);

  XMMATRIX v =
      XMMatrixTranspose(GetViewMatrix(0, 0, extent * 2.5f + 5.0f)); // 俯瞰高さ
  XMMATRIX p = XMMatrixTranspose(GetProjMatrix(extent, extent));

  context->VSSetConstantBuffers(0, 1, m_cb.GetAddressOf());

  ctx.world.Query<components::Transform, components::MeshRenderer>().Each(
      [&](ecs::Entity, components::Transform &t, components::MeshRenderer &r) {
        if (!r.isVisible)
          return;
        auto *mesh = ctx.resource.GetMesh(r.mesh);
        auto shader =
            ctx.resource.LoadShader("Basic", L"Assets/shaders/BasicVS.hlsl",
                                    L"Assets/shaders/BasicPS.hlsl");
        auto *shaderPtr = ctx.resource.GetShader(shader);
        if (!mesh || !shaderPtr)
          return;

        shaderPtr->Bind(context);

        D3D11_MAPPED_SUBRESOURCE ms;
        if (SUCCEEDED(
                context->Map(m_cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
          MapVSConst *c = (MapVSConst *)ms.pData;
          c->w = XMMatrixTranspose(t.GetWorldMatrix());
          c->v = v;
          c->p = p;
          c->c = r.color;
          context->Unmap(m_cb.Get(), 0);
        }

        if (r.hasTexture && r.textureSRV) {
          context->PSSetShaderResources(0, 1, r.textureSRV.GetAddressOf());
          context->PSSetSamplers(0, 1, m_samp.GetAddressOf());
        } else {
          ID3D11ShaderResourceView *nullSRV = nullptr;
          context->PSSetShaderResources(0, 1, &nullSRV);
        }
        mesh->Bind(context);
        mesh->Draw(context);
      });

  EndRender(context);
}

} // namespace game::systems
