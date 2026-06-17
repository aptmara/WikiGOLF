#include "RenderSystem.h"
#include "../../core/Logger.h"
#include "../../ecs/World.h"
#include "../../graphics/GraphicsDevice.h"
#include "../../resources/ResourceManager.h"
#include "../components/Camera.h"
#include "../components/MeshRenderer.h"
#include "../components/Transform.h"
#include "../components/WikiComponents.h"
#include <DirectXMath.h>
#include <chrono>
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
  XMFLOAT4
      materialFlags; // マテリアルフラグ。x:テクスチャ有無、y:法線マップ有無、z:カスタムフラグx、w:カスタムフラグy
  XMFLOAT4 lightDir;
  XMFLOAT4 cameraPos;
};

struct RenderState {
  ComPtr<ID3D11Buffer> cBuffer;
  ComPtr<ID3D11SamplerState> sampler;
  ComPtr<ID3D11BlendState> alphaBlendState;
  ComPtr<ID3D11BlendState> multiplyBlendState;
  ComPtr<ID3D11BlendState> addBlendState;
};

/**
 * @brief 1フレーム分の描画負荷を集計します。 山内陽
 */
struct RenderFrameStats {
  size_t candidates = 0;
  size_t visibleCandidates = 0;
  size_t drawn = 0;
  size_t opaqueDrawn = 0;
  size_t transparentDrawn = 0;
  size_t texturedDrawn = 0;
  size_t normalMappedDrawn = 0;
  size_t terrainDrawn = 0;
  size_t holeFlagDrawn = 0;
  size_t invisibleSkipped = 0;
  size_t alphaSkipped = 0;
  size_t transparentDistanceSkipped = 0;
  size_t missingResourceSkipped = 0;
};

void RenderSystem(core::GameContext &ctx) {
  static auto s_lastStatsLogAt = std::chrono::steady_clock::time_point::min();
  static auto s_lastSlowStatsLogAt =
      std::chrono::steady_clock::time_point::min();
  static uint64_t s_frameIndex = 0;
  const auto frameStartedAt = std::chrono::steady_clock::now();
  ++s_frameIndex;

  auto *device = ctx.graphics.GetDevice();
  auto *context = ctx.graphics.GetContext();
  auto &world = ctx.world;
  RenderFrameStats stats;

  // 定数バッファ等の取得または作成（Global Dataを使用）
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
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MinLOD = 0;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
    device->CreateSamplerState(&sampDesc, &newState.sampler);

    // ブレンドステート作成（半透明対応）
    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.AlphaToCoverageEnable = FALSE;
    blendDesc.IndependentBlendEnable = FALSE;
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask =
        D3D11_COLOR_WRITE_ENABLE_ALL;
    device->CreateBlendState(&blendDesc, &newState.alphaBlendState);

    // 乗算ブレンド
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_DEST_COLOR;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ZERO;
    device->CreateBlendState(&blendDesc, &newState.multiplyBlendState);

    // 加算ブレンド
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    device->CreateBlendState(&blendDesc, &newState.addBlendState);

    world.SetGlobal(std::move(newState));
    state = world.GetGlobal<RenderState>();
  }

  // カメラ情報の取得
  XMMATRIX view = XMMatrixIdentity();
  XMMATRIX proj = XMMatrixIdentity();
  XMFLOAT4 camPos = {0, 0, 0, 1};

  bool cameraFound = false;
  world.Query<components::Transform, components::Camera>().Each(
      [&](ecs::Entity, components::Transform &t, components::Camera &c) {
        if (c.isMainCamera || !cameraFound) {
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

  // レンダリングループ
  context->VSSetConstantBuffers(0, 1, state->cBuffer.GetAddressOf());
  context->PSSetConstantBuffers(0, 1, state->cBuffer.GetAddressOf());

  auto DrawRenderer = [&](ecs::Entity e, components::Transform &t,
                          components::MeshRenderer &r) {
    ++stats.candidates;
    if (!r.isVisible) {
      ++stats.invisibleSkipped;
      return;
    }
    ++stats.visibleCandidates;
    if (r.isTransparent) {
      if (r.color.w <= 0.01f) {
        ++stats.alphaSkipped;
        return;
      }
      const float dx = t.position.x - camPos.x;
      const float dy = t.position.y - camPos.y;
      const float dz = t.position.z - camPos.z;
      const float distSq = dx * dx + dy * dy + dz * dz;
      const bool keepsReadableOverlay =
          world.Has<components::TerrainObject>(e) &&
          r.blendMode == components::BlendMode::Multiply;
      if (distSq > 220.0f * 220.0f &&
          !world.Has<components::HoleFlag>(e) && !keepsReadableOverlay) {
        ++stats.transparentDistanceSkipped;
        return;
      }
    }

    auto *mesh = ctx.resource.GetMesh(r.mesh);
    auto *shader = ctx.resource.GetShader(r.shader);

    if (mesh && shader) {
      ++stats.drawn;
      if (r.isTransparent) {
        ++stats.transparentDrawn;
      } else {
        ++stats.opaqueDrawn;
      }
      if (r.hasTexture && r.textureSRV) {
        ++stats.texturedDrawn;
      }
      if (r.hasNormalMap && r.normalMapSRV) {
        ++stats.normalMappedDrawn;
      }
      if (world.Has<components::TerrainObject>(e)) {
        ++stats.terrainDrawn;
      }
      if (world.Has<components::HoleFlag>(e)) {
        ++stats.holeFlagDrawn;
      }

      // ブレンドステート設定
      if (r.blendMode == components::BlendMode::Alpha) {
        context->OMSetBlendState(state->alphaBlendState.Get(), nullptr,
                                 0xFFFFFFFF);
      } else if (r.blendMode == components::BlendMode::Multiply) {
        context->OMSetBlendState(state->multiplyBlendState.Get(), nullptr,
                                 0xFFFFFFFF);
      } else if (r.blendMode == components::BlendMode::Add) {
        context->OMSetBlendState(state->addBlendState.Get(), nullptr,
                                 0xFFFFFFFF);
      } else if (r.isTransparent) {
        context->OMSetBlendState(state->alphaBlendState.Get(), nullptr,
                                 0xFFFFFFFF);
      } else {
        context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
      }

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
        const bool hasDiffuse = r.hasTexture && r.textureSRV;
        const bool hasNormal = r.hasNormalMap && r.normalMapSRV;
        float diffFlag = 0.0f;
        if (hasDiffuse) diffFlag = 1.0f;
        float normFlag = 0.0f;
        if (hasNormal) normFlag = 1.0f;
        constants->materialFlags = {diffFlag,
                                    normFlag, r.customFlags.x,
                                    r.customFlags.y};
        constants->lightDir = {0.5f, -1.0f, 0.5f, 0.0f};
        constants->cameraPos = camPos;
        context->Unmap(state->cBuffer.Get(), 0);
      }

      // テクスチャバインド
      ID3D11ShaderResourceView *nullSRV = nullptr;
      if (r.hasTexture && r.textureSRV) {
        context->PSSetShaderResources(0, 1, r.textureSRV.GetAddressOf());
      } else {
        context->PSSetShaderResources(0, 1, &nullSRV);
      }

      // 法線マップのバインド
      if (r.hasNormalMap && r.normalMapSRV) {
        context->PSSetShaderResources(1, 1, r.normalMapSRV.GetAddressOf());
      } else {
        context->PSSetShaderResources(1, 1, &nullSRV);
      }

      context->PSSetSamplers(0, 1, state->sampler.GetAddressOf());
      mesh->Bind(context);
      mesh->Draw(context);
    } else {
      ++stats.missingResourceSkipped;
    }
  };

  // 不透明パス
  world.Query<components::Transform, components::MeshRenderer>().Each(
      [&](ecs::Entity e, components::Transform &t,
          components::MeshRenderer &r) {
        if (!r.isTransparent) {
          DrawRenderer(e, t, r);
        }
      });

  // 半透明パス
  world.Query<components::Transform, components::MeshRenderer>().Each(
      [&](ecs::Entity e, components::Transform &t,
          components::MeshRenderer &r) {
        if (r.isTransparent) {
          DrawRenderer(e, t, r);
        }
      });

  context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);

  const auto now = std::chrono::steady_clock::now();
  const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
                             now - frameStartedAt)
                             .count();
  const bool isSlowFrame =
      elapsedUs >= 4000 &&
      (s_lastSlowStatsLogAt == std::chrono::steady_clock::time_point::min() ||
       now - s_lastSlowStatsLogAt >= std::chrono::seconds(1));
  const bool isPeriodicLog =
      s_lastStatsLogAt == std::chrono::steady_clock::time_point::min() ||
      now - s_lastStatsLogAt >= std::chrono::seconds(5);
  if (isSlowFrame || isPeriodicLog) {
    LOG_INFO("RenderSystem",
             "Render stats frame={} elapsed={:.3f}ms candidates={} visible={} "
             "drawn={} opaque={} transparent={} textured={} normalMapped={} "
             "terrain={} holeFlags={} skippedInvisible={} skippedAlpha={} "
             "skippedTransparentDistance={} skippedMissingResource={}",
             s_frameIndex, static_cast<double>(elapsedUs) / 1000.0,
             stats.candidates, stats.visibleCandidates, stats.drawn,
             stats.opaqueDrawn, stats.transparentDrawn, stats.texturedDrawn,
             stats.normalMappedDrawn, stats.terrainDrawn, stats.holeFlagDrawn,
             stats.invisibleSkipped, stats.alphaSkipped,
             stats.transparentDistanceSkipped, stats.missingResourceSkipped);
    s_lastStatsLogAt = now;
    if (isSlowFrame) {
      s_lastSlowStatsLogAt = now;
    }
  }
}

} // namespace game::systems
