#include "RenderSystem.h"
#include "../../core/Logger.h"
#include "../../core/Profiler.h"
#include "../../ecs/World.h"
#include "../../graphics/GraphicsDevice.h"
#include "../../resources/ResourceManager.h"
#include "../components/Camera.h"
#include "../components/GrassRenderBatch.h"
#include "../components/MeshRenderer.h"
#include "../components/Transform.h"
#include "../components/WikiComponents.h"
#include <DirectXCollision.h>
#include <DirectXMath.h>
#include <chrono>
#include <d3d11.h>
#include <wrl/client.h>
#include <unordered_map>

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
  ComPtr<ID3D11RasterizerState> twoSidedRasterizerState;
  ComPtr<ID3D11Buffer> instancedBuffer;
  ComPtr<ID3D11ShaderResourceView> instancedSRV;
  size_t instancedBufferSize = 0;
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
  size_t lodDistanceSkipped = 0;
  size_t frustumSkipped = 0;
  size_t missingResourceSkipped = 0;
  size_t drawCalls = 0;
  size_t instancedDrawCalls = 0;
  size_t nonInstancedDrawCalls = 0;
  size_t grassBatchCandidates = 0;
  size_t grassInstancesConsidered = 0;
  size_t grassInstancesDistanceSkipped = 0;
  size_t grassNearLodInstances = 0;
  size_t grassMidLodInstances = 0;
};

void RenderSystem(core::GameContext &ctx) {
  PROFILE_SCOPE("RenderSystem.Total");
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
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    device->CreateBlendState(&blendDesc, &newState.addBlendState);

    D3D11_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    rasterizerDesc.CullMode = D3D11_CULL_NONE;
    rasterizerDesc.FrontCounterClockwise = FALSE;
    rasterizerDesc.DepthClipEnable = TRUE;
    device->CreateRasterizerState(&rasterizerDesc,
                                  &newState.twoSidedRasterizerState);

    world.SetGlobal(std::move(newState));
    state = world.GetGlobal<RenderState>();
  }

  // カメラ情報の取得
  XMMATRIX view = XMMatrixIdentity();
  XMMATRIX proj = XMMatrixIdentity();
  XMFLOAT4 camPos = {0, 0, 0, 1};

  float speedFactor = 0.0f;
  float windYaw = 0.0f;
  auto *golfState = world.GetGlobal<components::GolfGameState>();
  if (golfState) {
    speedFactor = std::clamp(golfState->windSpeed / 12.0f, 0.0f, 1.0f);
    windYaw = std::atan2(golfState->windDirection.y, golfState->windDirection.x);
  }

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

  DirectX::BoundingFrustum viewFrustum;
  DirectX::BoundingFrustum worldFrustum;
  DirectX::BoundingFrustum::CreateFromMatrix(viewFrustum, proj);
  XMVECTOR inverseViewDeterminant;
  const XMMATRIX inverseView = XMMatrixInverse(&inverseViewDeterminant, view);
  viewFrustum.Transform(worldFrustum, inverseView);

  // 転置（HLSLは列優先）
  view = XMMatrixTranspose(view);
  proj = XMMatrixTranspose(proj);

  // インスタンス化対応シェーダーのハンドル取得
  auto basicHandle = ctx.resource.FindShader("Basic");
  auto particleHandle = ctx.resource.FindShader("Particle");
  auto flagClothHandle = ctx.resource.FindShader("FlagCloth");
  auto grassHandle = ctx.resource.FindShader("Grass");

  // インスタンス構造体の定義
  struct InstanceData {
    XMFLOAT4X4 world;
    XMFLOAT4 color;
    XMFLOAT4 flags;
  };

  // バケットキーの定義
  struct RenderKey {
    resources::MeshHandle mesh;
    resources::ShaderHandle shader;
    ID3D11ShaderResourceView *textureSRV = nullptr;
    ID3D11ShaderResourceView *normalMapSRV = nullptr;
    components::BlendMode blendMode;
    bool isTransparent;
    bool twoSided = false;

    bool operator==(const RenderKey &o) const {
      return mesh == o.mesh && shader == o.shader &&
             textureSRV == o.textureSRV && normalMapSRV == o.normalMapSRV &&
             blendMode == o.blendMode && isTransparent == o.isTransparent &&
             twoSided == o.twoSided;
    }
  };

  // ハッシュ関数の定義
  struct RenderKeyHash {
    size_t operator()(const RenderKey &k) const {
      size_t h = 17;
      h = h * 31 + k.mesh.index;
      h = h * 31 + k.mesh.generation;
      h = h * 31 + k.shader.index;
      h = h * 31 + k.shader.generation;
      h = h * 31 + reinterpret_cast<size_t>(k.textureSRV);
      h = h * 31 + reinterpret_cast<size_t>(k.normalMapSRV);
      h = h * 31 + static_cast<size_t>(k.blendMode);
      h = h * 31 + (k.isTransparent ? 1 : 0);
      h = h * 31 + (k.twoSided ? 1 : 0);
      return h;
    }
  };

  struct RenderInstance {
    ecs::Entity entity;
    XMMATRIX worldMatrix;
    XMFLOAT4 color;
    XMFLOAT4 flags;
  };

  std::unordered_map<RenderKey, std::vector<RenderInstance>, RenderKeyHash> opaqueBuckets;
  std::unordered_map<RenderKey, std::vector<RenderInstance>, RenderKeyHash> transparentBuckets;

  // 構造化バッファの生成・リサイズ関数
  auto checkInstancedBuffer = [&](size_t requiredCount) {
    if (requiredCount <= state->instancedBufferSize && state->instancedBuffer) {
      return;
    }
    size_t newSize = state->instancedBufferSize == 0 ? 8192 : state->instancedBufferSize * 2;
    while (newSize < requiredCount) {
      newSize *= 2;
    }
    
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = static_cast<UINT>(sizeof(InstanceData) * newSize);
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    desc.StructureByteStride = sizeof(InstanceData);

    ComPtr<ID3D11Buffer> newBuffer;
    HRESULT hr = device->CreateBuffer(&desc, nullptr, &newBuffer);
    if (FAILED(hr)) {
      LOG_ERROR("RenderSystem", "Failed to create structured buffer (size={})", newSize);
      return;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = static_cast<UINT>(newSize);

    ComPtr<ID3D11ShaderResourceView> newSRV;
    hr = device->CreateShaderResourceView(newBuffer.Get(), &srvDesc, &newSRV);
    if (FAILED(hr)) {
      LOG_ERROR("RenderSystem", "Failed to create SRV for structured buffer");
      return;
    }

    state->instancedBuffer = newBuffer;
    state->instancedSRV = newSRV;
    state->instancedBufferSize = newSize;
  };

  // 全オブジェクトをクエリしてバケットに分類
  {
    PROFILE_SCOPE("RenderSystem.CollectAndBucket");
    world.Query<components::Transform, components::MeshRenderer>().Each(
      [&](ecs::Entity e, components::Transform &t, components::MeshRenderer &r) {
        ++stats.candidates;
        if (!r.isVisible) {
          ++stats.invisibleSkipped;
          return;
        }
        ++stats.visibleCandidates;

        const float dx = t.position.x - camPos.x;
        const float dy = t.position.y - camPos.y;
        const float dz = t.position.z - camPos.z;
        const float distSq = dx * dx + dy * dy + dz * dz;

        if (r.maxDrawDistance > 0.0f &&
            distSq > r.maxDrawDistance * r.maxDrawDistance) {
          ++stats.lodDistanceSkipped;
          return;
        }

        auto *candidateMesh = ctx.resource.GetMesh(r.mesh);
        if (!candidateMesh || !candidateMesh->IsValid()) {
          ++stats.missingResourceSkipped;
          return;
        }

        DirectX::BoundingSphere worldBounds;
        candidateMesh->GetBounds().Transform(worldBounds, t.GetWorldMatrix());
        worldBounds.Radius *= std::max(1.0f, r.boundsScale);
        if (worldFrustum.Contains(worldBounds) == DirectX::DISJOINT) {
          ++stats.frustumSkipped;
          return;
        }

        if (r.isTransparent) {
          if (r.color.w <= 0.01f) {
            ++stats.alphaSkipped;
            return;
          }
          const bool keepsReadableOverlay =
              world.Has<components::TerrainObject>(e) &&
              r.blendMode == components::BlendMode::Multiply;
          if (distSq > 220.0f * 220.0f &&
              !world.Has<components::HoleFlag>(e) && !keepsReadableOverlay) {
            ++stats.transparentDistanceSkipped;
            return;
          }
        }

        RenderKey key;
        key.mesh = r.mesh;
        key.shader = r.shader;
        key.textureSRV = r.textureSRV.Get();
        key.normalMapSRV = r.normalMapSRV.Get();
        key.blendMode = r.blendMode;
        key.isTransparent = r.isTransparent;

        RenderInstance inst;
        inst.entity = e;
        inst.worldMatrix = t.GetWorldMatrix();
        inst.color = r.color;

        const bool hasDiffuse = r.hasTexture && r.textureSRV;
        const bool hasNormal = r.hasNormalMap && r.normalMapSRV;
        float diffFlag = hasDiffuse ? 1.0f : 0.0f;
        float normFlag = hasNormal ? 1.0f : 0.0f;

        if (world.Has<components::HoleFlag>(e)) {
          const auto *flag = world.Get<components::HoleFlag>(e);
          inst.flags = XMFLOAT4(
              flag->phase,
              flag->amplitude,
              speedFactor,
              windYaw + flag->yawOffset
          );
        } else if (r.shader == grassHandle) {
          inst.flags = r.customFlags;
        } else {
          inst.flags = XMFLOAT4(
              diffFlag,
              normFlag,
              r.customFlags.x,
              r.customFlags.y
          );
        }

        if (r.isTransparent) {
          transparentBuckets[key].push_back(inst);
        } else {
          opaqueBuckets[key].push_back(inst);
        }
      });

    world.Query<components::GrassRenderBatch>().Each(
        [&](ecs::Entity e, components::GrassRenderBatch &batch) {
          ++stats.candidates;
          ++stats.grassBatchCandidates;
          if (batch.instances.empty()) {
            ++stats.invisibleSkipped;
            return;
          }
          ++stats.visibleCandidates;

          auto *candidateMesh = ctx.resource.GetMesh(batch.mesh);
          if (!candidateMesh || !candidateMesh->IsValid()) {
            ++stats.missingResourceSkipped;
            return;
          }
          if (batch.lodMesh.IsValid()) {
            auto *lodMesh = ctx.resource.GetMesh(batch.lodMesh);
            if (!lodMesh || !lodMesh->IsValid()) {
              ++stats.missingResourceSkipped;
              return;
            }
          }

          const XMFLOAT3 boundsCenter = {
              (batch.boundsMin.x + batch.boundsMax.x) * 0.5f,
              (batch.boundsMin.y + batch.boundsMax.y) * 0.5f,
              (batch.boundsMin.z + batch.boundsMax.z) * 0.5f};
          const XMFLOAT3 boundsExtents = {
              (batch.boundsMax.x - batch.boundsMin.x) * 0.5f,
              (batch.boundsMax.y - batch.boundsMin.y) * 0.5f,
              (batch.boundsMax.z - batch.boundsMin.z) * 0.5f};

          if (batch.maxDrawDistance > 0.0f) {
            const float distanceX =
                std::max(std::abs(camPos.x - boundsCenter.x) -
                             boundsExtents.x,
                         0.0f);
            const float distanceY =
                std::max(std::abs(camPos.y - boundsCenter.y) -
                             boundsExtents.y,
                         0.0f);
            const float distanceZ =
                std::max(std::abs(camPos.z - boundsCenter.z) -
                             boundsExtents.z,
                         0.0f);
            const float batchDistanceSq =
                distanceX * distanceX + distanceY * distanceY +
                distanceZ * distanceZ;
            if (batchDistanceSq >
                batch.maxDrawDistance * batch.maxDrawDistance) {
              ++stats.lodDistanceSkipped;
              stats.grassInstancesDistanceSkipped += batch.instances.size();
              return;
            }
          }

          const BoundingBox worldBounds(boundsCenter, boundsExtents);
          if (worldFrustum.Contains(worldBounds) == DISJOINT) {
            ++stats.frustumSkipped;
            return;
          }

          RenderKey nearKey;
          nearKey.mesh = batch.mesh;
          nearKey.shader = batch.shader;
          nearKey.blendMode = components::BlendMode::Opaque;
          nearKey.isTransparent = false;
          nearKey.twoSided = batch.twoSided;

          auto &nearInstances = opaqueBuckets[nearKey];
          nearInstances.reserve(nearInstances.size() + batch.instances.size());

          std::vector<RenderInstance> *midInstances = nullptr;
          if (batch.lodMesh.IsValid() && batch.lodSwitchDistance > 0.0f) {
            RenderKey midKey = nearKey;
            midKey.mesh = batch.lodMesh;
            midInstances = &opaqueBuckets[midKey];
            midInstances->reserve(midInstances->size() +
                                  batch.instances.size());
          }

          for (const auto &grassInstance : batch.instances) {
            ++stats.grassInstancesConsidered;
            const float dx = grassInstance.position.x - camPos.x;
            const float dy = grassInstance.position.y - camPos.y;
            const float dz = grassInstance.position.z - camPos.z;
            const float distanceSq = dx * dx + dy * dy + dz * dz;
            if (batch.maxDrawDistance > 0.0f &&
                distanceSq >
                    batch.maxDrawDistance * batch.maxDrawDistance) {
              ++stats.grassInstancesDistanceSkipped;
              continue;
            }

            RenderInstance instance;
            instance.entity = e;
            instance.worldMatrix = XMLoadFloat4x4(&grassInstance.world);
            instance.color = grassInstance.color;
            instance.flags = grassInstance.flags;

            if (midInstances &&
                distanceSq >
                    batch.lodSwitchDistance * batch.lodSwitchDistance) {
              midInstances->push_back(instance);
              ++stats.grassMidLodInstances;
            } else {
              nearInstances.push_back(instance);
              if (midInstances) {
                ++stats.grassNearLodInstances;
              }
            }
          }
        });
  }

  // バケットごとの描画関数
  auto DrawBucket = [&](const RenderKey &key, const std::vector<RenderInstance> &instances) {
    if (instances.empty()) return;

    auto *mesh = ctx.resource.GetMesh(key.mesh);
    auto *shader = ctx.resource.GetShader(key.shader);

    if (!mesh || !shader) {
      stats.missingResourceSkipped += instances.size();
      return;
    }

    // ブレンドステート設定
    if (key.blendMode == components::BlendMode::Alpha) {
      context->OMSetBlendState(state->alphaBlendState.Get(), nullptr, 0xFFFFFFFF);
    } else if (key.blendMode == components::BlendMode::Multiply) {
      context->OMSetBlendState(state->multiplyBlendState.Get(), nullptr, 0xFFFFFFFF);
    } else if (key.blendMode == components::BlendMode::Add) {
      context->OMSetBlendState(state->addBlendState.Get(), nullptr, 0xFFFFFFFF);
    } else if (key.isTransparent) {
      context->OMSetBlendState(state->alphaBlendState.Get(), nullptr, 0xFFFFFFFF);
    } else {
      context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
    }
    context->RSSetState(key.twoSided ? state->twoSidedRasterizerState.Get()
                                     : nullptr);

    shader->Bind(context);

    // テクスチャバインド
    ID3D11ShaderResourceView *nullSRV = nullptr;
    if (key.textureSRV) {
      context->PSSetShaderResources(0, 1, &key.textureSRV);
    } else {
      context->PSSetShaderResources(0, 1, &nullSRV);
    }

    // 法線マップのバインド
    if (key.normalMapSRV) {
      context->PSSetShaderResources(1, 1, &key.normalMapSRV);
    } else {
      context->PSSetShaderResources(1, 1, &nullSRV);
    }

    context->PSSetSamplers(0, 1, state->sampler.GetAddressOf());
    mesh->Bind(context);

    const bool supportsInstancing =
        (key.shader == basicHandle || key.shader == particleHandle ||
         key.shader == flagClothHandle || key.shader == grassHandle);

    if (supportsInstancing) {
      checkInstancedBuffer(instances.size());

      D3D11_MAPPED_SUBRESOURCE mappedInst;
      if (SUCCEEDED(context->Map(state->instancedBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedInst))) {
        InstanceData *dest = static_cast<InstanceData *>(mappedInst.pData);
        for (size_t i = 0; i < instances.size(); ++i) {
          XMStoreFloat4x4(&dest[i].world, XMMatrixTranspose(instances[i].worldMatrix));
          dest[i].color = instances[i].color;
          dest[i].flags = instances[i].flags;
        }
        context->Unmap(state->instancedBuffer.Get(), 0);
      }

      // 定数バッファのバインド（カメラView/Proj情報のみ）
      D3D11_MAPPED_SUBRESOURCE mappedCB;
      if (SUCCEEDED(context->Map(state->cBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedCB))) {
        VSConstants *constants = static_cast<VSConstants *>(mappedCB.pData);
        constants->view = view;
        constants->projection = proj;
        constants->lightDir = XMFLOAT4(0.5f, -1.0f, 0.5f, ctx.time);
        constants->cameraPos = camPos;
        constants->world = XMMatrixIdentity();
        constants->materialColor = XMFLOAT4(1, 1, 1, 1);
        if (key.shader == grassHandle && golfState) {
          constants->materialFlags =
              XMFLOAT4(golfState->windDirection.x,
                       golfState->windDirection.y, golfState->windSpeed, 0.0f);
        } else {
          constants->materialFlags = XMFLOAT4(0, 0, 0, 0);
        }
        context->Unmap(state->cBuffer.Get(), 0);
      }
      context->VSSetConstantBuffers(0, 1, state->cBuffer.GetAddressOf());
      context->PSSetConstantBuffers(0, 1, state->cBuffer.GetAddressOf());

      // 構造化バッファをVSのt15スロットにバインド
      context->VSSetShaderResources(15, 1, state->instancedSRV.GetAddressOf());

      // インスタンス化描画！
      context->DrawIndexedInstanced(mesh->GetIndexCount(), static_cast<UINT>(instances.size()), 0, 0, 0);
      ++stats.drawCalls;
      ++stats.instancedDrawCalls;

      // 解除
      ID3D11ShaderResourceView *nullVSs[1] = {nullptr};
      context->VSSetShaderResources(15, 1, nullVSs);

      // 統計情報更新
      stats.drawn += instances.size();
      for (const auto &inst : instances) {
        if (key.isTransparent) ++stats.transparentDrawn;
        else ++stats.opaqueDrawn;
        if (key.textureSRV) ++stats.texturedDrawn;
        if (key.normalMapSRV) ++stats.normalMappedDrawn;
        if (world.Has<components::TerrainObject>(inst.entity)) ++stats.terrainDrawn;
        if (world.Has<components::HoleFlag>(inst.entity)) ++stats.holeFlagDrawn;
      }
    } else {
      // インスタンス非対応：従来の通常描画
      context->VSSetConstantBuffers(0, 1, state->cBuffer.GetAddressOf());
      context->PSSetConstantBuffers(0, 1, state->cBuffer.GetAddressOf());

      for (const auto &inst : instances) {
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(context->Map(state->cBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
          VSConstants *constants = static_cast<VSConstants *>(mapped.pData);
          constants->world = XMMatrixTranspose(inst.worldMatrix);
          constants->view = view;
          constants->projection = proj;
          constants->materialColor = inst.color;
          constants->materialFlags = inst.flags;
          constants->lightDir = XMFLOAT4(0.5f, -1.0f, 0.5f, ctx.time);
          constants->cameraPos = camPos;
          context->Unmap(state->cBuffer.Get(), 0);
        }

        mesh->Draw(context);
        ++stats.drawCalls;
        ++stats.nonInstancedDrawCalls;

        ++stats.drawn;
        if (key.isTransparent) ++stats.transparentDrawn;
        else ++stats.opaqueDrawn;
        if (key.textureSRV) ++stats.texturedDrawn;
        if (key.normalMapSRV) ++stats.normalMappedDrawn;
        if (world.Has<components::TerrainObject>(inst.entity)) ++stats.terrainDrawn;
        if (world.Has<components::HoleFlag>(inst.entity)) ++stats.holeFlagDrawn;
      }
    }
  };

  {
    PROFILE_SCOPE("RenderSystem.SubmitDraws");
    // 不透明描画実行
    for (const auto &pair : opaqueBuckets) {
      DrawBucket(pair.first, pair.second);
    }

    // 半透明描画実行
    for (const auto &pair : transparentBuckets) {
      DrawBucket(pair.first, pair.second);
    }
  }

  context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
  context->RSSetState(nullptr);

  auto &profiler = core::Profiler::Instance();
  profiler.SetCounter("Render.Candidates", static_cast<double>(stats.candidates));
  profiler.SetCounter("Render.VisibleCandidates",
                      static_cast<double>(stats.visibleCandidates));
  profiler.SetCounter("Render.DrawnInstances", static_cast<double>(stats.drawn));
  profiler.SetCounter("Render.DrawCalls", static_cast<double>(stats.drawCalls));
  profiler.SetCounter("Render.InstancedDrawCalls",
                      static_cast<double>(stats.instancedDrawCalls));
  profiler.SetCounter("Render.NonInstancedDrawCalls",
                      static_cast<double>(stats.nonInstancedDrawCalls));
  profiler.SetCounter("Render.GrassBatchCandidates",
                      static_cast<double>(stats.grassBatchCandidates));
  profiler.SetCounter("Render.GrassInstancesConsidered",
                      static_cast<double>(stats.grassInstancesConsidered));
  profiler.SetCounter(
      "Render.GrassInstancesDistanceSkipped",
      static_cast<double>(stats.grassInstancesDistanceSkipped));
  profiler.SetCounter("Render.GrassNearLodInstances",
                      static_cast<double>(stats.grassNearLodInstances));
  profiler.SetCounter("Render.GrassMidLodInstances",
                      static_cast<double>(stats.grassMidLodInstances));
  profiler.SetCounter("Render.OpaqueBuckets",
                      static_cast<double>(opaqueBuckets.size()));
  profiler.SetCounter("Render.TransparentBuckets",
                      static_cast<double>(transparentBuckets.size()));
  profiler.SetCounter("Render.HoleFlags", static_cast<double>(stats.holeFlagDrawn));
  profiler.SetCounter("Render.SkippedInvisible",
                      static_cast<double>(stats.invisibleSkipped));
  profiler.SetCounter("Render.SkippedTransparentDistance",
                      static_cast<double>(stats.transparentDistanceSkipped));
  profiler.SetCounter("Render.SkippedLodDistance",
                      static_cast<double>(stats.lodDistanceSkipped));
  profiler.SetCounter("Render.SkippedFrustum",
                      static_cast<double>(stats.frustumSkipped));

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
             "grassBatches={} grassInstancesConsidered={} "
             "grassInstancesDistanceSkipped={} grassNearLod={} grassMidLod={} "
             "drawn={} opaque={} transparent={} textured={} normalMapped={} "
             "terrain={} holeFlags={} skippedInvisible={} skippedAlpha={} "
             "skippedTransparentDistance={} skippedLodDistance={} "
             "skippedFrustum={} skippedMissingResource={}",
             s_frameIndex, static_cast<double>(elapsedUs) / 1000.0,
             stats.candidates, stats.visibleCandidates,
             stats.grassBatchCandidates, stats.grassInstancesConsidered,
             stats.grassInstancesDistanceSkipped, stats.grassNearLodInstances,
             stats.grassMidLodInstances, stats.drawn,
             stats.opaqueDrawn, stats.transparentDrawn, stats.texturedDrawn,
             stats.normalMappedDrawn, stats.terrainDrawn, stats.holeFlagDrawn,
             stats.invisibleSkipped, stats.alphaSkipped,
             stats.transparentDistanceSkipped, stats.lodDistanceSkipped,
             stats.frustumSkipped, stats.missingResourceSkipped);
    s_lastStatsLogAt = now;
    if (isSlowFrame) {
      s_lastSlowStatsLogAt = now;
    }
  }
}

} // namespace game::systems
