/**
 * @file GrassFieldRenderer.cpp
 * @brief 手続き的に生やす芝の GPU 描画の実装
 */

#include "GrassFieldRenderer.h"
#include "GrassFieldRules.h"
#include "GrassImpulseRules.h"
#include "../../core/DisplaySettings.h"
#include "../../core/Logger.h"
#include "../../core/Profiler.h"
#include "../../ecs/World.h"
#include "../../graphics/GraphicsDevice.h"
#include "../../graphics/Shader.h"
#include "../components/GrassField.h"
#include "../components/WikiComponents.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

namespace game::systems {

namespace {

constexpr uint32_t kRegionCount = kGrassLayerCount * kGrassMaxLodCount;
constexpr uint32_t kArgsPerRegion = 5;
constexpr uint32_t kLodClassCount = 3;
constexpr UINT kWindResolution = 128;
constexpr float kWindTexelSize = 1.0f;
constexpr UINT kTrampleResolution = 256;
constexpr float kTrampleTexelSize = 0.0625f;
constexpr float kTrampleRadius = 0.45f;
constexpr float kTrampleRecoverySeconds = 3.0f;
constexpr UINT kFrameSlot = 5;
constexpr UINT kDrawSlot = 7;
constexpr UINT kGolfCupSlot = 3;
constexpr uint32_t kStatsQueryCount = 4;

/** @brief GrassFieldCommon.hlsli の GrassFrame と同じ並び */
struct GrassFrameConstants {
  XMFLOAT4X4 viewProjection;
  XMFLOAT4X4 shadowViewProjection;
  XMFLOAT4 camera;
  XMFLOAT4 light;
  XMFLOAT4 shadowParams;
  XMFLOAT4 fieldRect;
  XMFLOAT4 fieldInfo;
  XMINT4 heightInfo;
  XMFLOAT4 windOrigin;
  XMFLOAT4 windParams;
  XMFLOAT4 trampleOrigin;
  XMFLOAT4 trampleBall;
  XMFLOAT4 trampleBallPrev;
  XMFLOAT4 trampleMotion;
  XMFLOAT4 frustum[6];
  XMFLOAT4 layerA[kGrassLayerCount];
  XMFLOAT4 layerB[kGrassLayerCount];
  XMINT4 layerWindow[kGrassLayerCount];
  XMINT4 layerGrid[kGrassLayerCount];
  XMFLOAT4 impulseA[components::GrassImpulseEvents::kCapacity];
  XMFLOAT4 impulseB[components::GrassImpulseEvents::kCapacity];
};

/** @brief GrassFieldCommon.hlsli の GrassDraw と同じ並び */
struct GrassDrawConstants {
  uint32_t layer;
  uint32_t lod;
  uint32_t regionBase;
  uint32_t vertsPerBlade;
};

struct GrassTexture {
  ComPtr<ID3D11Texture2D> texture;
  ComPtr<ID3D11ShaderResourceView> srv;
  ComPtr<ID3D11UnorderedAccessView> uav;
};

} // namespace

/** @brief 芝の GPU 資源と、フレームをまたぐ状態（ECS グローバル） */
struct GrassFieldRendererState {
  bool initialized = false;
  bool failed = false;
  ComPtr<ID3D11ComputeShader> cullShader;
  ComPtr<ID3D11ComputeShader> windShader;
  ComPtr<ID3D11ComputeShader> trampleShader;
  ComPtr<ID3D11VertexShader> vertexShader;
  ComPtr<ID3D11PixelShader> pixelShader;
  ComPtr<ID3D11Buffer> frameBuffer;
  std::array<ComPtr<ID3D11Buffer>, kRegionCount> drawBuffers;
  std::array<ComPtr<ID3D11Buffer>, kGrassLayerCount> cullBuffers;
  ComPtr<ID3D11Buffer> indexBuffer;
  std::array<uint32_t, kLodClassCount> indexSectionStart{};
  ComPtr<ID3D11Buffer> argsBuffer;
  ComPtr<ID3D11UnorderedAccessView> argsUav;
  ComPtr<ID3D11Buffer> visibleBuffer;
  ComPtr<ID3D11UnorderedAccessView> visibleUav;
  ComPtr<ID3D11ShaderResourceView> visibleSrv;
  uint32_t regionCapacity = 0;
  std::array<GrassTexture, 2> wind;
  std::array<GrassTexture, 2> trample;
  ComPtr<ID3D11SamplerState> linearClamp;
  ComPtr<ID3D11SamplerState> linearWrap;
  ComPtr<ID3D11RasterizerState> rasterizer;

  uint64_t fieldVersion = 0;
  std::shared_ptr<const components::GrassFieldData> field;
  ComPtr<ID3D11ShaderResourceView> heightSrv;
  ComPtr<ID3D11ShaderResourceView> fieldSrv;
  std::array<ComPtr<ID3D11ShaderResourceView>, kGrassLayerCount> tileSrv;

  uint32_t current = 0;
  bool hasPrevious = false;
  XMFLOAT2 windOrigin = {0.0f, 0.0f};
  XMFLOAT2 trampleOrigin = {0.0f, 0.0f};
  XMFLOAT3 previousBall = {0.0f, 0.0f, 0.0f};
  float previousTime = 0.0f;
  GrassGpuLoadController load;

  /** 芝の描画だけのパイプライン統計（数フレーム遅れで読む） */
  std::array<ComPtr<ID3D11Query>, kStatsQueryCount> statsQueries;
  std::array<bool, kStatsQueryCount> statsIssued{};
  uint32_t statsNext = 0;
  D3D11_QUERY_DATA_PIPELINE_STATISTICS lastStats{};
};

namespace {

template <typename T>
bool CreateConstantBuffer(ID3D11Device *device, ComPtr<ID3D11Buffer> &buffer) {
  D3D11_BUFFER_DESC desc = {};
  desc.ByteWidth = static_cast<UINT>((sizeof(T) + 15u) & ~15u);
  desc.Usage = D3D11_USAGE_DYNAMIC;
  desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  return SUCCEEDED(device->CreateBuffer(&desc, nullptr, &buffer));
}

template <typename T>
void WriteConstantBuffer(ID3D11DeviceContext *context, ID3D11Buffer *buffer,
                         const T &value) {
  D3D11_MAPPED_SUBRESOURCE mapped;
  if (SUCCEEDED(context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
    std::memcpy(mapped.pData, &value, sizeof(T));
    context->Unmap(buffer, 0);
  }
}

bool LoadComputeShader(ID3D11Device *device, const wchar_t *path,
                       ComPtr<ID3D11ComputeShader> &shader) {
  ComPtr<ID3DBlob> blob;
  if (!graphics::LoadShaderBytecode(path, "cs_5_0", blob)) {
    return false;
  }
  return SUCCEEDED(device->CreateComputeShader(
      blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &shader));
}

bool CreateGrassTexture(ID3D11Device *device, UINT size, DXGI_FORMAT format,
                        GrassTexture &out) {
  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = size;
  desc.Height = size;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = format;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
  const UINT bytesPerTexel = format == DXGI_FORMAT_R16G16B16A16_FLOAT ? 8u : 4u;
  std::vector<uint8_t> zeros(static_cast<size_t>(size) * size * bytesPerTexel, 0);
  if (format == DXGI_FORMAT_R8G8B8A8_UNORM) {
    // 踏み跡の向き rg は 0.5 が 0
    for (size_t i = 0; i < zeros.size(); i += 4) {
      zeros[i] = 128;
      zeros[i + 1] = 128;
    }
  }
  D3D11_SUBRESOURCE_DATA data = {zeros.data(), size * bytesPerTexel, 0};
  if (FAILED(device->CreateTexture2D(&desc, &data, &out.texture))) {
    return false;
  }
  return SUCCEEDED(device->CreateShaderResourceView(out.texture.Get(), nullptr,
                                                    &out.srv)) &&
         SUCCEEDED(device->CreateUnorderedAccessView(out.texture.Get(), nullptr,
                                                     &out.uav));
}

ComPtr<ID3D11ShaderResourceView> CreateStaticTexture(ID3D11Device *device,
                                                     UINT width, UINT height,
                                                     DXGI_FORMAT format,
                                                     const void *texels,
                                                     UINT bytesPerTexel) {
  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = width;
  desc.Height = height;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = format;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_IMMUTABLE;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  D3D11_SUBRESOURCE_DATA data = {texels, width * bytesPerTexel, 0};
  ComPtr<ID3D11Texture2D> texture;
  ComPtr<ID3D11ShaderResourceView> srv;
  if (SUCCEEDED(device->CreateTexture2D(&desc, &data, &texture))) {
    device->CreateShaderResourceView(texture.Get(), nullptr, &srv);
  }
  return srv;
}

/** @brief 層の窓の一辺のタイル数（カメラの周り最大距離まで） */
int WindowTileCount(const GrassLayerParams &params) {
  const int radius =
      static_cast<int>(std::ceil(params.maxDistance / params.tileSize)) + 1;
  return radius * 2 + 1;
}

/** @brief 葉の番号 b の三角形（根本から2頂点ずつ、最後が葉先） */
void AppendBladeIndices(std::vector<uint32_t> &indices, uint32_t blade,
                        uint32_t vertsPerBlade) {
  const uint32_t base = blade * vertsPerBlade;
  const uint32_t segments = (vertsPerBlade - 1u) / 2u;
  for (uint32_t s = 0; s < segments; ++s) {
    const uint32_t v0 = base + s * 2u;
    if (s + 1u < segments) {
      indices.insert(indices.end(), {v0, v0 + 1u, v0 + 2u, v0 + 2u, v0 + 1u, v0 + 3u});
    } else {
      indices.insert(indices.end(), {v0, v0 + 1u, v0 + 2u});
    }
  }
}

bool Initialize(ID3D11Device *device, GrassFieldRendererState &state) {
  if (!LoadComputeShader(device, L"Assets/shaders/GrassCullCS.hlsl", state.cullShader) ||
      !LoadComputeShader(device, L"Assets/shaders/GrassWindCS.hlsl", state.windShader) ||
      !LoadComputeShader(device, L"Assets/shaders/GrassTrampleCS.hlsl", state.trampleShader)) {
    return false;
  }
  ComPtr<ID3DBlob> vsBlob;
  ComPtr<ID3DBlob> psBlob;
  if (!graphics::LoadShaderBytecode(L"Assets/shaders/GrassFieldVS.hlsl", "vs_5_0", vsBlob) ||
      !graphics::LoadShaderBytecode(L"Assets/shaders/GrassFieldPS.hlsl", "ps_5_0", psBlob) ||
      FAILED(device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                                        nullptr, &state.vertexShader)) ||
      FAILED(device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
                                       nullptr, &state.pixelShader))) {
    return false;
  }

  if (!CreateConstantBuffer<GrassFrameConstants>(device, state.frameBuffer)) {
    return false;
  }
  D3D11_QUERY_DESC statsDesc = {};
  statsDesc.Query = D3D11_QUERY_PIPELINE_STATISTICS;
  for (auto &query : state.statsQueries) {
    if (FAILED(device->CreateQuery(&statsDesc, &query))) {
      return false;
    }
  }
  for (auto &buffer : state.drawBuffers) {
    if (!CreateConstantBuffer<GrassDrawConstants>(device, buffer)) {
      return false;
    }
  }
  for (auto &buffer : state.cullBuffers) {
    if (!CreateConstantBuffer<GrassDrawConstants>(device, buffer)) {
      return false;
    }
  }

  // 各形の段の区分は、どのプリセットでも足りるよう Ultra の枚数で作る
  std::array<uint32_t, kLodClassCount> sectionBlades{};
  for (uint32_t layer = 0; layer < kGrassLayerCount; ++layer) {
    const auto grassLayer = static_cast<GrassLayer>(layer);
    const auto ultra = GetGrassLayerParams(core::GraphicsPreset::Ultra, grassLayer);
    for (uint32_t lod = 0; lod < kGrassMaxLodCount; ++lod) {
      const uint32_t shape = GrassShapeLod(grassLayer, lod);
      sectionBlades[shape] = std::max(sectionBlades[shape], GrassBladesForLod(ultra, lod));
    }
  }
  std::vector<uint32_t> indices;
  for (uint32_t lodClass = 0; lodClass < kLodClassCount; ++lodClass) {
    const uint32_t blades = sectionBlades[lodClass];
    state.indexSectionStart[lodClass] = static_cast<uint32_t>(indices.size());
    const uint32_t vertsPerBlade = GrassVerticesPerBlade(lodClass);
    indices.reserve(indices.size() + blades * GrassIndicesPerBlade(lodClass));
    for (uint32_t blade = 0; blade < blades; ++blade) {
      AppendBladeIndices(indices, blade, vertsPerBlade);
    }
  }
  D3D11_BUFFER_DESC indexDesc = {};
  indexDesc.ByteWidth = static_cast<UINT>(indices.size() * sizeof(uint32_t));
  indexDesc.Usage = D3D11_USAGE_IMMUTABLE;
  indexDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
  D3D11_SUBRESOURCE_DATA indexData = {indices.data(), 0, 0};
  if (FAILED(device->CreateBuffer(&indexDesc, &indexData, &state.indexBuffer))) {
    return false;
  }

  D3D11_BUFFER_DESC argsDesc = {};
  argsDesc.ByteWidth = kRegionCount * kArgsPerRegion * sizeof(uint32_t);
  argsDesc.Usage = D3D11_USAGE_DEFAULT;
  argsDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
  argsDesc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS |
                       D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
  if (FAILED(device->CreateBuffer(&argsDesc, nullptr, &state.argsBuffer))) {
    return false;
  }
  D3D11_UNORDERED_ACCESS_VIEW_DESC argsUavDesc = {};
  argsUavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
  argsUavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
  argsUavDesc.Buffer.NumElements = kRegionCount * kArgsPerRegion;
  argsUavDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
  if (FAILED(device->CreateUnorderedAccessView(state.argsBuffer.Get(), &argsUavDesc,
                                               &state.argsUav))) {
    return false;
  }

  int maxWindow = 0;
  for (uint32_t layer = 0; layer < kGrassLayerCount; ++layer) {
    maxWindow = std::max(maxWindow,
                         WindowTileCount(GetGrassLayerParams(
                             core::GraphicsPreset::Ultra, static_cast<GrassLayer>(layer))));
  }
  state.regionCapacity = static_cast<uint32_t>(maxWindow * maxWindow);
  D3D11_BUFFER_DESC visibleDesc = {};
  visibleDesc.ByteWidth = kRegionCount * state.regionCapacity * sizeof(uint32_t) * 2u;
  visibleDesc.Usage = D3D11_USAGE_DEFAULT;
  visibleDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
  visibleDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
  visibleDesc.StructureByteStride = sizeof(uint32_t) * 2u;
  if (FAILED(device->CreateBuffer(&visibleDesc, nullptr, &state.visibleBuffer)) ||
      FAILED(device->CreateUnorderedAccessView(state.visibleBuffer.Get(), nullptr,
                                               &state.visibleUav)) ||
      FAILED(device->CreateShaderResourceView(state.visibleBuffer.Get(), nullptr,
                                              &state.visibleSrv))) {
    return false;
  }

  for (auto &texture : state.wind) {
    if (!CreateGrassTexture(device, kWindResolution, DXGI_FORMAT_R16G16B16A16_FLOAT,
                            texture)) {
      return false;
    }
  }
  for (auto &texture : state.trample) {
    if (!CreateGrassTexture(device, kTrampleResolution, DXGI_FORMAT_R8G8B8A8_UNORM,
                            texture)) {
      return false;
    }
  }

  D3D11_SAMPLER_DESC samplerDesc = {};
  samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
  samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
  samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
  if (FAILED(device->CreateSamplerState(&samplerDesc, &state.linearClamp))) {
    return false;
  }
  samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
  samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
  samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
  if (FAILED(device->CreateSamplerState(&samplerDesc, &state.linearWrap))) {
    return false;
  }

  D3D11_RASTERIZER_DESC rasterizerDesc = {};
  rasterizerDesc.FillMode = D3D11_FILL_SOLID;
  rasterizerDesc.CullMode = D3D11_CULL_NONE;
  rasterizerDesc.DepthClipEnable = TRUE;
  return SUCCEEDED(device->CreateRasterizerState(&rasterizerDesc, &state.rasterizer));
}

void UploadField(ID3D11Device *device, GrassFieldRendererState &state,
                 const components::GrassFieldData &field) {
  state.heightSrv = CreateStaticTexture(
      device, static_cast<UINT>(field.heightResolutionX),
      static_cast<UINT>(field.heightResolutionZ), DXGI_FORMAT_R32_FLOAT,
      field.heights.data(), sizeof(float));
  state.fieldSrv = CreateStaticTexture(
      device, static_cast<UINT>(field.fieldResolutionX),
      static_cast<UINT>(field.fieldResolutionZ), DXGI_FORMAT_R8G8B8A8_UNORM,
      field.fieldTexels.data(), sizeof(uint32_t));
  for (uint32_t layer = 0; layer < kGrassLayerCount; ++layer) {
    const auto &grid = field.layers[layer];
    state.tileSrv[layer].Reset();
    if (!grid.tiles.empty()) {
      state.tileSrv[layer] = CreateStaticTexture(
          device, static_cast<UINT>(grid.resolutionX),
          static_cast<UINT>(grid.resolutionZ), DXGI_FORMAT_R32G32B32A32_FLOAT,
          grid.tiles.data(), sizeof(XMFLOAT4));
    }
  }
}

/** @brief 行ベクトル規約の View*Projection から視錐台の6平面を取り出す */
void ExtractFrustumPlanes(const XMMATRIX &viewProjection, XMFLOAT4 (&planes)[6]) {
  XMFLOAT4X4 m;
  XMStoreFloat4x4(&m, viewProjection);
  const auto column = [&](int j) {
    return XMVectorSet(m.m[0][j], m.m[1][j], m.m[2][j], m.m[3][j]);
  };
  const XMVECTOR c0 = column(0);
  const XMVECTOR c1 = column(1);
  const XMVECTOR c2 = column(2);
  const XMVECTOR c3 = column(3);
  const XMVECTOR raw[6] = {c3 + c0, c3 - c0, c3 + c1, c3 - c1, c2, c3 - c2};
  for (int i = 0; i < 6; ++i) {
    XMStoreFloat4(&planes[i], XMPlaneNormalize(raw[i]));
  }
}

XMFLOAT2 SnapOrigin(float centerX, float centerZ, float texelSize, UINT resolution) {
  const float half = texelSize * static_cast<float>(resolution) * 0.5f;
  return {std::floor((centerX - half) / texelSize) * texelSize,
          std::floor((centerZ - half) / texelSize) * texelSize};
}

} // namespace

void ClearGrassField(core::GameContext &ctx) {
  auto &world = ctx.world;
  const auto *source = world.GetGlobal<components::GrassFieldSource>();
  components::GrassFieldSource cleared;
  cleared.version = source ? source->version + 1 : 1;
  world.SetGlobal(std::move(cleared));
  world.SetGlobal(components::GrassTrampleInput{});
  world.SetGlobal(components::GrassImpulseEvents{});
  if (auto *state = world.GetGlobal<GrassFieldRendererState>()) {
    state->field.reset();
    state->heightSrv.Reset();
    state->fieldSrv.Reset();
    for (auto &srv : state->tileSrv) {
      srv.Reset();
    }
    state->hasPrevious = false;
  }
}

void AddGrassImpulse(core::GameContext &ctx, components::GrassImpulse impulse) {
  auto *events = ctx.world.GetGlobal<components::GrassImpulseEvents>();
  if (!events) {
    ctx.world.SetGlobal(components::GrassImpulseEvents{});
    events = ctx.world.GetGlobal<components::GrassImpulseEvents>();
  }
  impulse.startTime = ctx.time;
  PushGrassImpulse(*events, impulse);
}

void RenderGrassField(core::GameContext &ctx, const GrassFieldFrameInput &input) {
  auto &world = ctx.world;
  const auto *source = world.GetGlobal<components::GrassFieldSource>();
  if (!source || !source->data) {
    return;
  }
  core::GraphicsPreset preset = core::GraphicsPreset::High;
  if (ctx.displaySettings) {
    preset = ctx.displaySettings->GetEffectiveGraphicsPreset();
  }
  std::array<GrassLayerParams, kGrassLayerCount> params;
  bool anyLayer = false;
  for (uint32_t layer = 0; layer < kGrassLayerCount; ++layer) {
    params[layer] = GetGrassLayerParams(preset, static_cast<GrassLayer>(layer));
    anyLayer = anyLayer || params[layer].enabled;
  }
  if (!anyLayer) {
    return;
  }

  auto *device = ctx.graphics.GetDevice();
  auto *context = ctx.graphics.GetContext();
  auto *state = world.GetGlobal<GrassFieldRendererState>();
  if (!state) {
    world.SetGlobal(GrassFieldRendererState{});
    state = world.GetGlobal<GrassFieldRendererState>();
  }
  if (state->failed) {
    return;
  }
  if (!state->initialized) {
    if (!Initialize(device, *state)) {
      LOG_ERROR("Grass", "芝の GPU 資源を作れなかったため、芝を描きません");
      state->failed = true;
      return;
    }
    state->initialized = true;
  }
  if (state->fieldVersion != source->version || state->field != source->data) {
    UploadField(device, *state, *source->data);
    state->field = source->data;
    state->fieldVersion = source->version;
  }
  const auto &field = *state->field;
  if (!state->heightSrv || !state->fieldSrv) {
    return;
  }

  PROFILE_SCOPE("RenderSystem.Grass");
  UpdateGrassGpuLoad(state->load, ctx.graphics.GetLatestGpuFrameMs());
  const float distanceScale = GrassDistanceScaleForBudget(state->load.budgetScale);

  const float time = ctx.time;
  const XMFLOAT4 &camera = input.cameraPosition;
  const uint32_t current = state->current;
  const uint32_t previous = 1u - current;

  const XMFLOAT2 windOrigin =
      SnapOrigin(camera.x, camera.z, kWindTexelSize, kWindResolution);
  XMFLOAT3 ball = {camera.x, camera.y, camera.z};
  bool touching = false;
  XMFLOAT2 ballVelocity = {0.0f, 0.0f};
  if (const auto *trample = world.GetGlobal<components::GrassTrampleInput>()) {
    ball = trample->position;
    touching = trample->touching;
    ballVelocity = trample->velocity;
  }
  const XMFLOAT2 trampleOrigin =
      SnapOrigin(ball.x, ball.z, kTrampleTexelSize, kTrampleResolution);
  const float ballStepX = ball.x - state->previousBall.x;
  const float ballStepZ = ball.z - state->previousBall.z;
  if (ballStepX * ballStepX + ballStepZ * ballStepZ > 4.0f) {
    // 打ち直しなどで瞬間移動したときは、前の位置から跡を引かない
    state->previousBall = ball;
  }
  if (!state->hasPrevious) {
    state->windOrigin = windOrigin;
    state->trampleOrigin = trampleOrigin;
    state->previousBall = ball;
    state->previousTime = time;
  }
  const float deltaTime = std::clamp(time - state->previousTime, 0.0f, 0.1f);

  XMFLOAT2 windDirection = {0.62f, 0.78f};
  float windSpeed = 0.0f;
  if (const auto *golf = world.GetGlobal<components::GolfGameState>()) {
    const float length = std::sqrt(golf->windDirection.x * golf->windDirection.x +
                                   golf->windDirection.y * golf->windDirection.y);
    if (length > 0.0001f) {
      windDirection = {golf->windDirection.x / length, golf->windDirection.y / length};
    }
    windSpeed = golf->windSpeed;
  }

  GrassFrameConstants frame = {};
  XMStoreFloat4x4(&frame.viewProjection, XMMatrixTranspose(input.viewProjection));
  XMStoreFloat4x4(&frame.shadowViewProjection,
                  XMMatrixTranspose(input.shadowViewProjection));
  frame.camera = camera;
  frame.light = XMFLOAT4(0.5f, -1.0f, 0.5f, time);
  frame.shadowParams = XMFLOAT4(input.shadowEnabled ? 1.0f : 0.0f, 0, 0, 0);
  frame.fieldRect = XMFLOAT4(field.minX, field.minZ, field.width, field.depth);
  std::array<components::GrassImpulse, components::GrassImpulseEvents::kCapacity> impulses{};
  uint32_t impulseCount = 0;
  if (const auto *events = world.GetGlobal<components::GrassImpulseEvents>()) {
    impulseCount = CollectActiveGrassImpulses(*events, time, impulses);
  }
  for (uint32_t i = 0; i < impulseCount; ++i) {
    const auto &impulse = impulses[i];
    frame.impulseA[i] = XMFLOAT4(impulse.position.x, impulse.position.z, impulse.startTime,
                                 static_cast<float>(impulse.kind));
    frame.impulseB[i] = XMFLOAT4(impulse.direction.x, impulse.direction.y, impulse.strength,
                                 0.0f);
  }
  frame.fieldInfo = XMFLOAT4(field.heightOffset, state->previousTime, deltaTime,
                             static_cast<float>(impulseCount));
  frame.heightInfo = XMINT4(field.heightResolutionX, field.heightResolutionZ, 0, 0);
  frame.windOrigin = XMFLOAT4(windOrigin.x, windOrigin.y, state->windOrigin.x,
                              state->windOrigin.y);
  frame.windParams = XMFLOAT4(windDirection.x, windDirection.y,
                              std::clamp(windSpeed / 12.0f, 0.0f, 1.0f), windSpeed);
  frame.trampleOrigin = XMFLOAT4(trampleOrigin.x, trampleOrigin.y,
                                 state->trampleOrigin.x, state->trampleOrigin.y);
  frame.trampleBall = XMFLOAT4(ball.x, ball.y, ball.z, touching ? 1.0f : 0.0f);
  frame.trampleBallPrev = XMFLOAT4(state->previousBall.x, state->previousBall.y,
                                   state->previousBall.z, kTrampleRadius);
  frame.trampleMotion = XMFLOAT4(ballVelocity.x, ballVelocity.y,
                                 std::exp(-deltaTime / kTrampleRecoverySeconds), 0.0f);
  ExtractFrustumPlanes(input.cullViewProjection, frame.frustum);

  std::array<uint32_t, kGrassLayerCount> lodCounts{};
  for (uint32_t layer = 0; layer < kGrassLayerCount; ++layer) {
    const auto &p = params[layer];
    const auto &grid = field.layers[layer];
    const bool enabled = p.enabled && state->tileSrv[layer] && grid.tileSize > 0.0f;
    lodCounts[layer] = enabled ? GrassLodCount(p) : 0u;
    const int window = enabled ? WindowTileCount(p) : 0;
    const int radius = window / 2;
    const int centerX =
        static_cast<int>(std::floor((camera.x - field.minX) / std::max(grid.tileSize, 0.01f)));
    const int centerZ =
        static_cast<int>(std::floor((camera.z - field.minZ) / std::max(grid.tileSize, 0.01f)));
    frame.layerA[layer] = XMFLOAT4(grid.tileSize, p.fullDensityDistance * distanceScale,
                                   p.maxDistance, p.fadeWidth);
    frame.layerB[layer] = XMFLOAT4(
        p.maxBladeHeight, static_cast<float>(lodCounts[layer]),
        static_cast<float>(GrassBaseBladesPerTile(p) / kGrassLeavesPerShoot),
        enabled ? 1.0f : 0.0f);
    frame.layerWindow[layer] = XMINT4(centerX - radius, centerZ - radius, window, window);
    frame.layerGrid[layer] = XMINT4(grid.resolutionX, grid.resolutionZ,
                                    static_cast<int32_t>(state->regionCapacity), 0);
  }
  WriteConstantBuffer(context, state->frameBuffer.Get(), frame);

  // 間接描画の引数を毎フレーム作り直す（InstanceCount は判定で数える）
  std::array<uint32_t, kRegionCount * kArgsPerRegion> args{};
  for (uint32_t layer = 0; layer < kGrassLayerCount; ++layer) {
    for (uint32_t lod = 0; lod < kGrassMaxLodCount; ++lod) {
      const uint32_t region = layer * kGrassMaxLodCount + lod;
      const uint32_t shape = GrassShapeLod(static_cast<GrassLayer>(layer), lod);
      if (lod < lodCounts[layer]) {
        args[region * kArgsPerRegion + 0] =
            GrassBladesForLod(params[layer], lod) * GrassIndicesPerBlade(shape);
        args[region * kArgsPerRegion + 2] = state->indexSectionStart[shape];
      }
      const GrassDrawConstants draw = {layer, lod, region * state->regionCapacity,
                                       GrassVerticesPerBlade(shape)};
      WriteConstantBuffer(context, state->drawBuffers[region].Get(), draw);
    }
    const GrassDrawConstants cull = {layer, 0u, 0u, 0u};
    WriteConstantBuffer(context, state->cullBuffers[layer].Get(), cull);
  }
  context->UpdateSubresource(state->argsBuffer.Get(), 0, nullptr, args.data(), 0, 0);

  ID3D11ShaderResourceView *nullSrvs[8] = {};
  ID3D11UnorderedAccessView *nullUavs[2] = {};
  context->VSSetShaderResources(0, 8, nullSrvs);
  {
    graphics::ScopedGpuTimer gpuTimer(ctx.graphics, "GPU.Grass.Compute");
    context->CSSetConstantBuffers(kFrameSlot, 1, state->frameBuffer.GetAddressOf());
    context->CSSetConstantBuffers(kGolfCupSlot, 1, &input.golfCupBuffer);

    context->CSSetShader(state->windShader.Get(), nullptr, 0);
    context->CSSetUnorderedAccessViews(0, 1, state->wind[current].uav.GetAddressOf(), nullptr);
    context->Dispatch(kWindResolution / 8, kWindResolution / 8, 1);
    context->CSSetUnorderedAccessViews(0, 1, nullUavs, nullptr);

    context->CSSetShader(state->trampleShader.Get(), nullptr, 0);
    context->CSSetShaderResources(0, 1, state->trample[previous].srv.GetAddressOf());
    context->CSSetUnorderedAccessViews(0, 1, state->trample[current].uav.GetAddressOf(),
                                       nullptr);
    context->Dispatch(kTrampleResolution / 8, kTrampleResolution / 8, 1);
    context->CSSetUnorderedAccessViews(0, 1, nullUavs, nullptr);
    context->CSSetShaderResources(0, 1, nullSrvs);

    context->CSSetShader(state->cullShader.Get(), nullptr, 0);
    ID3D11UnorderedAccessView *cullUavs[2] = {state->argsUav.Get(), state->visibleUav.Get()};
    context->CSSetUnorderedAccessViews(0, 2, cullUavs, nullptr);
    for (uint32_t layer = 0; layer < kGrassLayerCount; ++layer) {
      if (lodCounts[layer] == 0) {
        continue;
      }
      const UINT groups = static_cast<UINT>((frame.layerWindow[layer].z + 7) / 8);
      context->CSSetConstantBuffers(kDrawSlot, 1, state->cullBuffers[layer].GetAddressOf());
      context->CSSetShaderResources(0, 1, state->tileSrv[layer].GetAddressOf());
      context->Dispatch(groups, groups, 1);
    }
    context->CSSetUnorderedAccessViews(0, 2, nullUavs, nullptr);
    context->CSSetShaderResources(0, 1, nullSrvs);
    context->CSSetShader(nullptr, nullptr, 0);
  }

  {
    graphics::ScopedGpuTimer gpuTimer(ctx.graphics, "GPU.Meshes.Grass");
    for (uint32_t i = 0; i < kStatsQueryCount; ++i) {
      const uint32_t slot = (state->statsNext + i) % kStatsQueryCount;
      if (state->statsIssued[slot] &&
          context->GetData(state->statsQueries[slot].Get(), &state->lastStats,
                           sizeof(state->lastStats), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK) {
        state->statsIssued[slot] = false;
      }
    }
    const uint32_t statsSlot = state->statsNext;
    const bool measure = !state->statsIssued[statsSlot];
    if (measure) {
      context->Begin(state->statsQueries[statsSlot].Get());
    }
    context->IASetInputLayout(nullptr);
    context->IASetIndexBuffer(state->indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(state->vertexShader.Get(), nullptr, 0);
    context->PSSetShader(state->pixelShader.Get(), nullptr, 0);
    context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
    context->RSSetState(state->rasterizer.Get());
    context->VSSetConstantBuffers(kFrameSlot, 1, state->frameBuffer.GetAddressOf());
    context->PSSetConstantBuffers(kFrameSlot, 1, state->frameBuffer.GetAddressOf());
    ID3D11ShaderResourceView *vsSrvs[7] = {
        state->heightSrv.Get(),         state->fieldSrv.Get(),
        state->wind[current].srv.Get(), state->wind[previous].srv.Get(),
        state->trample[current].srv.Get(), state->visibleSrv.Get(),
        state->trample[previous].srv.Get()};
    context->VSSetShaderResources(0, 7, vsSrvs);
    ID3D11SamplerState *samplers[3] = {state->linearClamp.Get(), nullptr,
                                       state->linearWrap.Get()};
    context->VSSetSamplers(0, 3, samplers);
    if (input.shadowEnabled && input.shadowMap && input.shadowSampler) {
      context->PSSetShaderResources(14, 1, &input.shadowMap);
      context->PSSetSamplers(1, 1, &input.shadowSampler);
    }

    // 近い LOD から描き、遠くの葉を深度で早く捨てる
    for (uint32_t lod = 0; lod < kGrassMaxLodCount; ++lod) {
      for (uint32_t layer = 0; layer < kGrassLayerCount; ++layer) {
        if (lod >= lodCounts[layer]) {
          continue;
        }
        const uint32_t region = layer * kGrassMaxLodCount + lod;
        context->VSSetConstantBuffers(kDrawSlot, 1, state->drawBuffers[region].GetAddressOf());
        context->DrawIndexedInstancedIndirect(
            state->argsBuffer.Get(), region * kArgsPerRegion * sizeof(uint32_t));
      }
    }
    context->VSSetShaderResources(0, 7, nullSrvs);
    context->RSSetState(nullptr);
    if (measure) {
      context->End(state->statsQueries[statsSlot].Get());
      state->statsIssued[statsSlot] = true;
      state->statsNext = (statsSlot + 1u) % kStatsQueryCount;
    }
  }

  state->windOrigin = windOrigin;
  state->trampleOrigin = trampleOrigin;
  state->previousBall = ball;
  state->previousTime = time;
  state->hasPrevious = true;
  state->current = previous;

  auto &profiler = core::Profiler::Instance();
  profiler.SetCounter("Render.GrassBudgetScale",
                      static_cast<double>(state->load.budgetScale));
  profiler.SetCounter("Render.GrassSmoothedGpuMs",
                      static_cast<double>(state->load.smoothedGpuMs));
  profiler.SetCounter("Render.GrassVsInvocations",
                      static_cast<double>(state->lastStats.VSInvocations));
  profiler.SetCounter("Render.GrassRasterPrimitives",
                      static_cast<double>(state->lastStats.CPrimitives));
  profiler.SetCounter("Render.GrassPsInvocations",
                      static_cast<double>(state->lastStats.PSInvocations));
}

} // namespace game::systems
