/**
 * @file ProceduralFlag.cpp
 * @brief プロシージャル旗生成ユーティリティ実装
 */

#include "ProceduralFlag.h"
#include "../../core/GameContext.h"
#include "../../ecs/World.h"
#include "../../graphics/Mesh.h"
#include "../components/MeshRenderer.h"
#include "../components/Transform.h"
#include "../components/WikiComponents.h"
#include <algorithm>
#include <cmath>

namespace game::utils {

using namespace DirectX;
using game::components::BlendMode;
using game::components::HoleFlag;
using game::components::MeshRenderer;
using game::components::Transform;

namespace {

constexpr int kClothColumns = 14;
constexpr int kClothRows = 5;
constexpr float kClothMeshWidth = 1.24f;
constexpr float kClothMeshHeight = 0.82f;
constexpr float kClothPoleInset = 0.045f;
constexpr float kClothMeshThickness = 0.018f;

/**
 * @brief エンティティを生成して共通リストへ登録します。
 */
ecs::Entity CreateTrackedEntity(core::GameContext &ctx,
                                ProceduralFlagResult &result) {
  ecs::Entity entity = ctx.world.CreateEntity();
  result.allEntities.push_back(entity);
  return entity;
}

/**
 * @brief 色を少し暗くします。
 */
XMFLOAT4 Darken(const XMFLOAT4 &color, float factor, float alpha) {
  return {color.x * factor, color.y * factor, color.z * factor, alpha};
}

/**
 * @brief 旗布メッシュ頂点を追加します。
 */
void AddClothVertex(std::vector<graphics::Vertex> &vertices, float u, float v,
                    float z, const XMFLOAT3 &normal) {
  const float anchoredU = u * u;
  const float initialWave =
      std::sin(u * 3.14159f * 1.35f) * 0.026f * anchoredU;
  graphics::Vertex vertex{};
  vertex.position = {kClothPoleInset + u * kClothMeshWidth,
                     (0.5f - v) * kClothMeshHeight, z + initialWave};
  vertex.normal = normal;
  vertex.texCoord = {u, v};
  const float shade = 0.92f + 0.08f * u;
  vertex.color = {shade, shade, shade, 1.0f};
  vertex.tangent = {1.0f, 0.0f, 0.0f};
  vertex.bitangent = {0.0f, -1.0f, 0.0f};
  vertices.push_back(vertex);
}

/**
 * @brief GPU変形用の一枚旗布メッシュを生成します。
 */
resources::MeshHandle CreateFlagClothMesh(core::GameContext &ctx,
                                          ecs::Entity ownerEntity) {
  std::vector<graphics::Vertex> vertices;
  std::vector<uint32_t> indices;
  vertices.reserve(static_cast<size_t>(kClothColumns + 1) *
                   static_cast<size_t>(kClothRows + 1) * 2 + 32);
  indices.reserve(static_cast<size_t>(kClothColumns) *
                  static_cast<size_t>(kClothRows) * 12 + 96);

  const float frontZ = kClothMeshThickness * 0.5f;
  const float backZ = -kClothMeshThickness * 0.5f;
  for (int y = 0; y <= kClothRows; ++y) {
    const float v = static_cast<float>(y) / static_cast<float>(kClothRows);
    for (int x = 0; x <= kClothColumns; ++x) {
      const float u = static_cast<float>(x) / static_cast<float>(kClothColumns);
      AddClothVertex(vertices, u, v, frontZ, {0.0f, 0.0f, 1.0f});
    }
  }
  for (int y = 0; y <= kClothRows; ++y) {
    const float v = static_cast<float>(y) / static_cast<float>(kClothRows);
    for (int x = 0; x <= kClothColumns; ++x) {
      const float u = static_cast<float>(x) / static_cast<float>(kClothColumns);
      AddClothVertex(vertices, u, v, backZ, {0.0f, 0.0f, -1.0f});
    }
  }

  const int stride = kClothColumns + 1;
  for (int y = 0; y < kClothRows; ++y) {
    for (int x = 0; x < kClothColumns; ++x) {
      const uint32_t a = static_cast<uint32_t>(y * stride + x);
      const uint32_t b = static_cast<uint32_t>(y * stride + x + 1);
      const uint32_t c = static_cast<uint32_t>((y + 1) * stride + x);
      const uint32_t d = static_cast<uint32_t>((y + 1) * stride + x + 1);
      indices.insert(indices.end(), {a, b, d, a, d, c});

      const uint32_t backOffset =
          static_cast<uint32_t>((kClothColumns + 1) * (kClothRows + 1));
      const uint32_t ba = backOffset + a;
      const uint32_t bb = backOffset + b;
      const uint32_t bc = backOffset + c;
      const uint32_t bd = backOffset + d;
      indices.insert(indices.end(), {ba, bd, bb, ba, bc, bd});
    }
  }

  const auto addEdgeQuad = [&](uint32_t f0, uint32_t f1, uint32_t b0,
                              uint32_t b1) {
    indices.insert(indices.end(), {f0, f1, b1, f0, b1, b0});
  };
  const uint32_t backOffset =
      static_cast<uint32_t>((kClothColumns + 1) * (kClothRows + 1));
  for (int x = 0; x < kClothColumns; ++x) {
    addEdgeQuad(static_cast<uint32_t>(x), static_cast<uint32_t>(x + 1),
                backOffset + static_cast<uint32_t>(x),
                backOffset + static_cast<uint32_t>(x + 1));
    const uint32_t bottom = static_cast<uint32_t>(kClothRows * stride + x);
    addEdgeQuad(bottom + 1, bottom, backOffset + bottom + 1,
                backOffset + bottom);
  }
  for (int y = 0; y < kClothRows; ++y) {
    const uint32_t left0 = static_cast<uint32_t>(y * stride);
    const uint32_t left1 = static_cast<uint32_t>((y + 1) * stride);
    addEdgeQuad(left1, left0, backOffset + left1, backOffset + left0);
    const uint32_t right0 = static_cast<uint32_t>(y * stride + kClothColumns);
    const uint32_t right1 =
        static_cast<uint32_t>((y + 1) * stride + kClothColumns);
    addEdgeQuad(right0, right1, backOffset + right0, backOffset + right1);
  }

  const std::string meshName =
      "ProceduralFlagClothGpu_" + std::to_string(ownerEntity);
  return ctx.resource.CreateDynamicMesh(meshName, vertices, indices);
}

} // namespace

ProceduralFlagResult CreateProceduralFlag(
    core::GameContext &ctx, const XMFLOAT3 &basePosition,
    const XMFLOAT4 &color, const ProceduralFlagOptions &options) {
  ProceduralFlagResult result;

  const auto basicShader = ctx.resource.LoadShader(
      "Basic", L"Assets/shaders/BasicVS.hlsl",
      L"Assets/shaders/BasicPS.hlsl");
  const auto particleShader = ctx.resource.LoadShader(
      "Particle", L"Assets/shaders/ParticleVS.hlsl",
      L"Assets/shaders/ParticlePS.hlsl");
  (void)ctx.resource.LoadShader("FlagCloth", L"Assets/shaders/FlagVS.hlsl",
                                L"Assets/shaders/FlagPS.hlsl");

  const float size = options.large ? 1.05f : 0.90f;
  const float poleHeight = 2.65f * size;
  const float clothHeight = kClothMeshHeight * size;
  const float clothY = basePosition.y + poleHeight * 0.84f;

  auto poleEntity = CreateTrackedEntity(ctx, result);
  auto &poleT = ctx.world.Add<Transform>(poleEntity);
  poleT.position = {basePosition.x, basePosition.y + poleHeight * 0.5f,
                    basePosition.z};
  poleT.scale = {0.055f * size, poleHeight, 0.055f * size};
  auto &poleMr = ctx.world.Add<MeshRenderer>(poleEntity);
  poleMr.mesh = ctx.resource.LoadMesh("builtin/cube");
  poleMr.shader = basicShader;
  poleMr.color = {0.16f, 0.15f, 0.13f, 1.0f};
  
  auto &poleFlag = ctx.world.Add<HoleFlag>(poleEntity);
  poleFlag.holeEntity = options.holeEntity;
  poleFlag.kind = HoleFlag::Kind::Accent;
  poleFlag.colorFactor = 1.0f;
  poleFlag.basePosition = poleT.position;
  poleFlag.baseScale = poleT.scale;

  result.poleEntity = poleEntity;

  auto capEntity = CreateTrackedEntity(ctx, result);
  auto &capT = ctx.world.Add<Transform>(capEntity);
  capT.position = {basePosition.x, basePosition.y + poleHeight + 0.08f * size,
                   basePosition.z};
  capT.scale = {0.18f * size, 0.18f * size, 0.18f * size};
  auto &capMr = ctx.world.Add<MeshRenderer>(capEntity);
  capMr.mesh = ctx.resource.LoadMesh("builtin/sphere");
  capMr.shader = basicShader;
  capMr.color = Darken(color, 0.85f, 1.0f);
  auto &capFlag = ctx.world.Add<HoleFlag>(capEntity);
  capFlag.holeEntity = options.holeEntity;
  capFlag.kind = HoleFlag::Kind::Accent;
  capFlag.colorFactor = 0.85f;
  capFlag.basePosition = capT.position;
  capFlag.baseScale = capT.scale;

  auto clothEntity = CreateTrackedEntity(ctx, result);
  auto &clothT = ctx.world.Add<Transform>(clothEntity);
  clothT.position = {basePosition.x, clothY, basePosition.z};
  clothT.scale = {size, size, size};
  auto &clothMr = ctx.world.Add<MeshRenderer>(clothEntity);
  clothMr.mesh = CreateFlagClothMesh(ctx, clothEntity);
  clothMr.shader = ctx.resource.LoadShader("FlagCloth",
                                           L"Assets/shaders/FlagVS.hlsl",
                                           L"Assets/shaders/FlagPS.hlsl");
  clothMr.color = color;

  auto &clothFlag = ctx.world.Add<HoleFlag>(clothEntity);
  clothFlag.holeEntity = options.holeEntity;
  clothFlag.kind = HoleFlag::Kind::Cloth;
  clothFlag.phase = basePosition.x * 0.11f + basePosition.z * 0.07f;
  clothFlag.amplitude = std::clamp(options.animationWeight * 1.55f, 0.35f, 1.85f);
  clothFlag.yawOffset = 0.0f;
  clothFlag.basePosition = clothT.position;
  clothFlag.baseScale = clothT.scale;
  result.firstClothEntity = clothEntity;

  auto seamEntity = CreateTrackedEntity(ctx, result);
  auto &seamT = ctx.world.Add<Transform>(seamEntity);
  seamT.position = {basePosition.x + 0.04f * size, clothY, basePosition.z};
  seamT.scale = {0.055f * size, clothHeight * 1.08f, 0.09f * size};
  auto &seamMr = ctx.world.Add<MeshRenderer>(seamEntity);
  seamMr.mesh = ctx.resource.LoadMesh("builtin/cube");
  seamMr.shader = basicShader;
  seamMr.color = Darken(color, 0.62f, 1.0f);
  auto &seamFlag = ctx.world.Add<HoleFlag>(seamEntity);
  seamFlag.holeEntity = options.holeEntity;
  seamFlag.kind = HoleFlag::Kind::Accent;
  seamFlag.colorFactor = 0.62f;
  seamFlag.basePosition = seamT.position;
  seamFlag.baseScale = seamT.scale;

  if (options.createParticles) {
    constexpr int kParticleCount = 5;
    for (int i = 0; i < kParticleCount; ++i) {
      auto particleEntity = CreateTrackedEntity(ctx, result);
      auto &particleT = ctx.world.Add<Transform>(particleEntity);
      particleT.position = {basePosition.x + 0.35f * size +
                                0.22f * static_cast<float>(i),
                            clothY + 0.12f * static_cast<float>(i % 3),
                            basePosition.z};
      const float particleScale = (0.045f + 0.012f * (i % 2)) * size;
      particleT.scale = {particleScale, particleScale, particleScale};
      auto &particleMr = ctx.world.Add<MeshRenderer>(particleEntity);
      particleMr.mesh = ctx.resource.LoadMesh("builtin/sphere");
      particleMr.shader = particleShader;
      particleMr.color = {color.x, color.y, color.z, 0.26f};
      particleMr.isTransparent = true;
      particleMr.blendMode = BlendMode::Add;
      particleMr.customFlags = {0.0f, 1.0f, 0.0f, 0.0f};

      auto &flag = ctx.world.Add<HoleFlag>(particleEntity);
      flag.holeEntity = options.holeEntity;
      flag.kind = HoleFlag::Kind::Particle;
      flag.phase = basePosition.x * 0.09f + static_cast<float>(i) * 1.21f;
      flag.amplitude = std::clamp(options.animationWeight, 0.15f, 1.2f);
      flag.basePosition = particleT.position;
      flag.baseScale = particleT.scale;

      result.particleEntities.push_back(particleEntity);
    }
  }

  return result;
}

} // namespace game::utils
