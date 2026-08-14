#include "MapSys.h"
#include "../../core/GameContext.h"
#include "../../core/Logger.h"
#include "../../core/Profiler.h"
#include "../../ecs/World.h"
#include "../../graphics/GraphicsDevice.h"
#include "../../graphics/Mesh.h"
#include "../../graphics/Shader.h"
#include "../../resources/ResourceManager.h"
#include "../components/MeshRenderer.h"
#include "../components/Transform.h"
#include <algorithm>
#include <unordered_map>

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
  bd.ByteWidth = sizeof(XMMATRIX) * 3 + sizeof(XMFLOAT4) * 2; // HLSL ConstantBuffer (224バイト) とアライメントサイズを完全一致させるため拡張
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

  // m_srv（このミニマップの出力）がPS/VSの入力として残っていると、これから
  // 同じリソース（m_rt）をRTVとしてバインドする際にハザードになるため、
  // 先に明示的に解除しておく。
  ID3D11ShaderResourceView *nullSRVs[2] = {nullptr, nullptr};
  ctx->PSSetShaderResources(0, 2, nullSRVs);
  ctx->VSSetShaderResources(15, 1, nullSRVs);

  ctx->OMSetRenderTargets(1, m_rtv.GetAddressOf(), m_dsv.Get());
  ctx->RSSetViewports(1, &m_vp);
  float color[] = {0.035f, 0.040f, 0.060f, 1.0f};
  ctx->ClearRenderTargetView(m_rtv.Get(), color);
  ctx->ClearDepthStencilView(m_dsv.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
}

void MapSys::EndRender(ID3D11DeviceContext *ctx) {
  // ミニマップ描画で使ったPS/VSのSRVスロットを解除してから復元する。
  ID3D11ShaderResourceView *nullSRVs[2] = {nullptr, nullptr};
  ctx->PSSetShaderResources(0, 2, nullSRVs);
  ctx->VSSetShaderResources(15, 1, nullSRVs);

  ctx->OMSetRenderTargets(1, m_saveRTV.GetAddressOf(), m_saveDSV.Get());
  ctx->RSSetViewports(1, &m_saveVP);
}

bool MapSys::EnsureInstanceBuffer(ID3D11Device *device, size_t requiredCount) {
  struct MapInstanceData {
    XMFLOAT4X4 world;
    XMFLOAT4 color;
    XMFLOAT4 flags;
  };

  if (requiredCount <= m_instancedBufferCapacity && m_instancedBuffer) {
    return true;
  }

  size_t newSize = m_instancedBufferCapacity == 0 ? 256 : m_instancedBufferCapacity * 2;
  while (newSize < requiredCount) {
    newSize *= 2;
  }

  D3D11_BUFFER_DESC desc = {};
  desc.ByteWidth = static_cast<UINT>(sizeof(MapInstanceData) * newSize);
  desc.Usage = D3D11_USAGE_DYNAMIC;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
  desc.StructureByteStride = sizeof(MapInstanceData);

  Microsoft::WRL::ComPtr<ID3D11Buffer> newBuffer;
  HRESULT hr = device->CreateBuffer(&desc, nullptr, &newBuffer);
  if (FAILED(hr)) {
    LOG_ERROR("MapSys", "Failed to create instanced structured buffer (size={})", newSize);
    return false;
  }

  D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
  srvDesc.Format = DXGI_FORMAT_UNKNOWN;
  srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
  srvDesc.Buffer.FirstElement = 0;
  srvDesc.Buffer.NumElements = static_cast<UINT>(newSize);

  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> newSRV;
  hr = device->CreateShaderResourceView(newBuffer.Get(), &srvDesc, &newSRV);
  if (FAILED(hr)) {
    LOG_ERROR("MapSys", "Failed to create SRV for instanced structured buffer");
    return false;
  }

  m_instancedBuffer = newBuffer;
  m_instancedSRV = newSRV;
  m_instancedBufferCapacity = newSize;
  return true;
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
  XMFLOAT4 flags; // HLSL側の MaterialFlags (x: hasDiffuse, y: hasNormalMap, z: uvScale, w: unused) に対応
  // w/c/flags はインスタンシング描画では未使用（構造化バッファ側に移動）。
  // cbufferのバイトレイアウト/アライメントをシェーダー側と一致させるため保持する。
};

struct MapInstanceData {
  XMFLOAT4X4 world;
  XMFLOAT4 color;
  XMFLOAT4 flags; // x: hasTexture, y: unused, z: uvScale, w: unused
};

struct MapRenderKey {
  resources::MeshHandle mesh;
  ID3D11ShaderResourceView *textureSRV = nullptr;
  components::MinimapRenderMode mode = components::MinimapRenderMode::None;

  bool operator==(const MapRenderKey &o) const {
    return mesh == o.mesh && textureSRV == o.textureSRV && mode == o.mode;
  }
};

struct MapRenderKeyHash {
  size_t operator()(const MapRenderKey &k) const {
    size_t h = 17;
    h = h * 31 + k.mesh.index;
    h = h * 31 + k.mesh.generation;
    h = h * 31 + reinterpret_cast<size_t>(k.textureSRV);
    h = h * 31 + static_cast<size_t>(k.mode);
    return h;
  }
};

struct MapRenderInstance {
  XMMATRIX worldMatrix;
  XMFLOAT4 color;
};

void MapSys::Render(core::GameContext &ctx, const MapRenderParams &params) {
  auto *device = ctx.graphics.GetDevice();
  auto *context = ctx.graphics.GetContext();
  BeginRender(context);

  // GolfGameStateへの依存を削除し、params.extentを正とする
  float extent = params.extent;

  float zoom =
      (std::max)(0.001f, params.zoom); // 大きいほど寄る（強ズーム対応）
  float heightScale = (std::max)(0.1f, params.heightScale);
  float orthoPadding = (std::max)(1.0f, params.orthoPadding);

  // 表示幅（viewSpan）の制限：下限を固定値にしてマップサイズに関係なく寄れるように
  float viewSpan = extent / zoom;
  viewSpan = std::clamp(viewSpan, 5.0f, extent * 6.0f);

  float height = (std::max)(viewSpan * heightScale, 5.0f);
  float orthoWidth = (std::max)(viewSpan * orthoPadding, viewSpan * 0.5f);

  XMMATRIX v = XMMatrixTranspose(
      GetViewMatrix(params.center.x, params.center.z, height));
  XMMATRIX p = XMMatrixTranspose(GetProjMatrix(orthoWidth, orthoWidth));

  auto shaderHandle = ctx.resource.LoadShader(
      "Minimap", L"Assets/shaders/MinimapVS.hlsl", L"Assets/shaders/MinimapPS.hlsl");
  auto *shaderPtr = ctx.resource.GetShader(shaderHandle);
  if (!shaderPtr) {
    EndRender(context);
    return;
  }

  // View/Projectionをb0へ書き込む（World/Color/Flagsはインスタンシングでは未使用）
  {
    D3D11_MAPPED_SUBRESOURCE ms;
    if (SUCCEEDED(context->Map(m_cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
      MapVSConst *c = static_cast<MapVSConst *>(ms.pData);
      c->w = XMMatrixIdentity();
      c->v = v;
      c->p = p;
      c->c = XMFLOAT4(1, 1, 1, 1);
      c->flags = XMFLOAT4(0, 0, 0, 0);
      context->Unmap(m_cb.Get(), 0);
    }
  }

  // ミニマップ対象（地形ベースメッシュ / 記事オーバーレイタイル）のみを収集し、
  // (メッシュ, テクスチャSRV, ミニマップモード) 単位でバケット化する。
  std::unordered_map<MapRenderKey, std::vector<MapRenderInstance>, MapRenderKeyHash> buckets;
  size_t itemCount = 0;

  ctx.world.Query<components::Transform, components::MeshRenderer>().Each(
      [&](ecs::Entity, components::Transform &t,
          components::MeshRenderer &r) {
        if (!r.isVisible)
          return;
        if (r.minimapMode == components::MinimapRenderMode::None)
          return;
        // 専用のミニマップメッシュ（間引き済み）が設定されていないエンティティは、
        // 本描画用フル解像度メッシュを誤って使わないよう安全に除外する。
        if (!r.minimapMesh.IsValid())
          return;

        const float cullMargin = 12.0f;
        if (std::abs(t.position.x - params.center.x) > orthoWidth * 0.5f + cullMargin ||
            std::abs(t.position.z - params.center.z) > orthoWidth * 0.5f + cullMargin) {
          return;
        }

        auto *mesh = ctx.resource.GetMesh(r.minimapMesh);
        if (!mesh || !mesh->IsValid())
          return;

        MapRenderKey key;
        key.mesh = r.minimapMesh;
        key.mode = r.minimapMode;
        // 頂点カラーのみのベース地形はTexture2DArrayを保持しているため、
        // Texture2Dとしてサンプルしないよう常にテクスチャなし扱いにする。
        key.textureSRV = (r.minimapMode == components::MinimapRenderMode::Textured &&
                          r.hasTexture && r.textureSRV)
                             ? r.textureSRV.Get()
                             : nullptr;

        MapRenderInstance inst;
        inst.worldMatrix = t.GetWorldMatrix();
        inst.color = r.color;
        buckets[key].push_back(inst);
        ++itemCount;
      });

  const bool instanceBufferReady =
      EnsureInstanceBuffer(device, (std::max<size_t>)(itemCount, 1));
  if (!instanceBufferReady) {
    LOG_ERROR("MapSys", "EnsureInstanceBuffer failed (itemCount={}); "
                        "buckets exceeding current capacity will be skipped",
              itemCount);
  }

  context->VSSetConstantBuffers(0, 1, m_cb.GetAddressOf());
  shaderPtr->Bind(context);
  context->PSSetSamplers(0, 1, m_samp.GetAddressOf());

  size_t drawCalls = 0;
  for (const auto &[key, instances] : buckets) {
    if (instances.empty())
      continue;

    auto *mesh = ctx.resource.GetMesh(key.mesh);
    if (!mesh)
      continue;

    // バッファ確保に失敗している、または古い（より小さい）バッファしかない場合、
    // このバケットの書き込みで容量を超えてオーバーフローしないよう安全にスキップする。
    if (!m_instancedBuffer || instances.size() > m_instancedBufferCapacity)
      continue;

    D3D11_MAPPED_SUBRESOURCE mappedInst;
    if (FAILED(context->Map(m_instancedBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedInst))) {
      continue;
    }
    auto *dest = static_cast<MapInstanceData *>(mappedInst.pData);
    for (size_t i = 0; i < instances.size(); ++i) {
      XMStoreFloat4x4(&dest[i].world, XMMatrixTranspose(instances[i].worldMatrix));
      dest[i].color = instances[i].color;
      const bool vertexColorTerrain =
          key.mode == components::MinimapRenderMode::VertexColor;
      dest[i].flags =
          XMFLOAT4(key.textureSRV ? 1.0f : 0.0f,
                   vertexColorTerrain ? 1.0f : 0.0f, 1.0f, 0.0f);
    }
    context->Unmap(m_instancedBuffer.Get(), 0);

    if (key.textureSRV) {
      context->PSSetShaderResources(0, 1, &key.textureSRV);
    } else {
      ID3D11ShaderResourceView *nullSRV = nullptr;
      context->PSSetShaderResources(0, 1, &nullSRV);
    }

    mesh->Bind(context);
    context->VSSetShaderResources(15, 1, m_instancedSRV.GetAddressOf());
    context->DrawIndexedInstanced(mesh->GetIndexCount(),
                                  static_cast<UINT>(instances.size()), 0, 0, 0);
    ++drawCalls;
  }

  auto &profiler = core::Profiler::Instance();
  profiler.SetCounter("Minimap.Items", static_cast<double>(itemCount));
  profiler.SetCounter("Minimap.Buckets", static_cast<double>(buckets.size()));
  profiler.SetCounter("Minimap.DrawCalls", static_cast<double>(drawCalls));

  EndRender(context);
}

} // namespace game::systems
