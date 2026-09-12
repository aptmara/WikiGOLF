/**
 * @file ShadowRenderSystem.cpp
 * @brief 平行光源用リアルタイムシャドウマップ描画の実装
 */

#include "ShadowRenderSystem.h"
#include "../../core/Logger.h"
#include "../../core/Profiler.h"
#include "../../ecs/World.h"
#include "../../graphics/GraphicsDevice.h"
#include "../../resources/ResourceManager.h"
#include "../components/MeshRenderer.h"
#include "../components/Transform.h"
#include "../components/WikiComponents.h"
#include <algorithm>
#include <cmath>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

namespace game::systems {
namespace {

constexpr UINT kShadowMapSize = 2048;
constexpr float kShadowCoverage = 90.0f;
constexpr XMFLOAT3 kLightDirection = {0.5f, -1.0f, 0.5f};

struct ShadowConstants {
  XMMATRIX world;
  XMMATRIX lightViewProjection;
};

bool InitializeShadowResources(ID3D11Device *device, ShadowRenderState &state) {
  if (!device) {
    return false;
  }

  if (!state.depthShader.LoadFromFile(
          device, L"Assets/shaders/ShadowDepthVS.hlsl", "main",
          L"Assets/shaders/ShadowDepthPS.hlsl", "main",
          graphics::Shader::GetDefaultInputLayout())) {
    LOG_ERROR("ShadowRenderSystem", "Failed to compile shadow depth shader");
    return false;
  }

  D3D11_TEXTURE2D_DESC textureDesc = {};
  textureDesc.Width = kShadowMapSize;
  textureDesc.Height = kShadowMapSize;
  textureDesc.MipLevels = 1;
  textureDesc.ArraySize = 1;
  textureDesc.Format = DXGI_FORMAT_R32_TYPELESS;
  textureDesc.SampleDesc.Count = 1;
  textureDesc.Usage = D3D11_USAGE_DEFAULT;
  textureDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
  if (FAILED(device->CreateTexture2D(&textureDesc, nullptr,
                                     &state.depthTexture))) {
    return false;
  }

  D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
  dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
  dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
  if (FAILED(device->CreateDepthStencilView(state.depthTexture.Get(), &dsvDesc,
                                            &state.depthView))) {
    return false;
  }

  D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
  srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
  srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  srvDesc.Texture2D.MipLevels = 1;
  if (FAILED(device->CreateShaderResourceView(state.depthTexture.Get(), &srvDesc,
                                              &state.shaderResourceView))) {
    return false;
  }

  D3D11_SAMPLER_DESC samplerDesc = {};
  samplerDesc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
  samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
  samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
  samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
  samplerDesc.BorderColor[0] = 1.0f;
  samplerDesc.BorderColor[1] = 1.0f;
  samplerDesc.BorderColor[2] = 1.0f;
  samplerDesc.BorderColor[3] = 1.0f;
  samplerDesc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
  samplerDesc.MinLOD = 0.0f;
  samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
  if (FAILED(device->CreateSamplerState(&samplerDesc,
                                        &state.comparisonSampler))) {
    return false;
  }

  D3D11_RASTERIZER_DESC rasterizerDesc = {};
  rasterizerDesc.FillMode = D3D11_FILL_SOLID;
  rasterizerDesc.CullMode = D3D11_CULL_BACK;
  rasterizerDesc.DepthBias = 1400;
  rasterizerDesc.SlopeScaledDepthBias = 2.0f;
  rasterizerDesc.DepthBiasClamp = 0.01f;
  rasterizerDesc.DepthClipEnable = TRUE;
  if (FAILED(device->CreateRasterizerState(&rasterizerDesc,
                                           &state.rasterizerState))) {
    return false;
  }

  D3D11_BUFFER_DESC bufferDesc = {};
  bufferDesc.ByteWidth = sizeof(ShadowConstants);
  bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
  bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  if (FAILED(device->CreateBuffer(&bufferDesc, nullptr,
                                  &state.constantBuffer))) {
    return false;
  }

  state.initialized = true;
  LOG_INFO("ShadowRenderSystem", "Initialized {}x{} shadow map",
           kShadowMapSize, kShadowMapSize);
  return true;
}

} // namespace

void ShadowRenderSystem(core::GameContext &ctx) {
  PROFILE_SCOPE("ShadowRenderSystem.Total");

  auto &world = ctx.world;
  auto *state = world.GetGlobal<ShadowRenderState>();
  if (!state) {
    world.SetGlobal(ShadowRenderState{});
    state = world.GetGlobal<ShadowRenderState>();
  }
  state->validThisFrame = false;

  auto *golfState = world.GetGlobal<components::GolfGameState>();
  if (!golfState || !world.IsAlive(golfState->ballEntity)) {
    return;
  }
  const auto *ballTransform =
      world.Get<components::Transform>(golfState->ballEntity);
  if (!ballTransform) {
    return;
  }

  auto *device = ctx.graphics.GetDevice();
  auto *context = ctx.graphics.GetContext();
  if (!state->initialized && !InitializeShadowResources(device, *state)) {
    return;
  }

  const XMVECTOR lightDirection =
      XMVector3Normalize(XMLoadFloat3(&kLightDirection));
  const XMVECTOR focus = XMVectorSet(ballTransform->position.x,
                                     ballTransform->position.y,
                                     ballTransform->position.z, 1.0f);
  const XMVECTOR lightPosition =
      XMVectorSubtract(focus, XMVectorScale(lightDirection, 110.0f));
  const XMMATRIX lightView = XMMatrixLookAtLH(
      lightPosition, focus, XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
  const XMMATRIX lightProjection = XMMatrixOrthographicLH(
      kShadowCoverage, kShadowCoverage, 1.0f, 240.0f);
  state->lightViewProjection = lightView * lightProjection;

  ComPtr<ID3D11RenderTargetView> oldRenderTarget;
  ComPtr<ID3D11DepthStencilView> oldDepthView;
  context->OMGetRenderTargets(1, &oldRenderTarget, &oldDepthView);

  D3D11_VIEWPORT oldViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
  UINT oldViewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
  context->RSGetViewports(&oldViewportCount, oldViewports);

  ComPtr<ID3D11RasterizerState> oldRasterizerState;
  context->RSGetState(&oldRasterizerState);

  ID3D11ShaderResourceView *nullShadowResource = nullptr;
  context->PSSetShaderResources(14, 1, &nullShadowResource);
  context->ClearDepthStencilView(state->depthView.Get(), D3D11_CLEAR_DEPTH,
                                 1.0f, 0);
  context->OMSetRenderTargets(0, nullptr, state->depthView.Get());

  D3D11_VIEWPORT shadowViewport = {};
  shadowViewport.Width = static_cast<float>(kShadowMapSize);
  shadowViewport.Height = static_cast<float>(kShadowMapSize);
  shadowViewport.MinDepth = 0.0f;
  shadowViewport.MaxDepth = 1.0f;
  context->RSSetViewports(1, &shadowViewport);
  context->RSSetState(state->rasterizerState.Get());
  state->depthShader.Bind(context);
  context->VSSetConstantBuffers(0, 1, state->constantBuffer.GetAddressOf());

  size_t drawCalls = 0;
  world.Query<components::Transform, components::MeshRenderer>().Each(
      [&](ecs::Entity, components::Transform &transform,
          components::MeshRenderer &renderer) {
        if (!renderer.isVisible || renderer.isTransparent ||
            renderer.color.w <= 0.01f) {
          return;
        }

        auto *mesh = ctx.resource.GetMesh(renderer.mesh);
        if (!mesh || !mesh->IsValid()) {
          return;
        }

        BoundingSphere bounds;
        mesh->GetBounds().Transform(bounds, transform.GetWorldMatrix());
        const float dx = bounds.Center.x - ballTransform->position.x;
        const float dz = bounds.Center.z - ballTransform->position.z;
        const float maximumDistance = kShadowCoverage * 0.72f + bounds.Radius;
        if (dx * dx + dz * dz > maximumDistance * maximumDistance) {
          return;
        }

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED(context->Map(state->constantBuffer.Get(), 0,
                                D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
          return;
        }
        auto *constants = static_cast<ShadowConstants *>(mapped.pData);
        constants->world = XMMatrixTranspose(transform.GetWorldMatrix());
        constants->lightViewProjection =
            XMMatrixTranspose(state->lightViewProjection);
        context->Unmap(state->constantBuffer.Get(), 0);

        mesh->Bind(context);
        mesh->Draw(context);
        ++drawCalls;
      });

  context->OMSetRenderTargets(1, oldRenderTarget.GetAddressOf(),
                              oldDepthView.Get());
  if (oldViewportCount > 0) {
    context->RSSetViewports(oldViewportCount, oldViewports);
  }
  context->RSSetState(oldRasterizerState.Get());

  state->validThisFrame = true;
  core::Profiler::Instance().SetCounter("Shadow.DrawCalls",
                                         static_cast<double>(drawCalls));
}

} // namespace game::systems
