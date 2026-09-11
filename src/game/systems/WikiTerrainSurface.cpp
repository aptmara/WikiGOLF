/**
 * @file WikiTerrainSurface.cpp
 * @brief 地形表面の芝生成を実装します。
*/

#include "WikiTerrainSystem.h"
#include "GrassStreamingRules.h"
#include "../../core/DisplaySettings.h"
#include "../../core/GameContext.h"
#include "../../core/Logger.h"
#include "../../ecs/World.h"
#include "../../resources/ResourceManager.h"
#include "../components/GrassRenderBatch.h"
#include "../components/MeshRenderer.h"
#include "../components/Transform.h"
#include "../components/WikiComponents.h"
#include "core/Profiler.h"
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <cfloat>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace game::systems {

using namespace DirectX;
using namespace game::components;

namespace {

constexpr float kGrassStreamChunkSize = 12.0f;
constexpr float kGrassStreamingPadding = 24.0f;
constexpr size_t kGrassChunksGeneratedPerFrame = 4;

float GrassStreamingDrawDistance(core::GraphicsPreset preset) {
  if (preset == core::GraphicsPreset::Ultra) {
    return 90.0f;
  }
  return 62.0f;
}

} // namespace

void WikiTerrainSystem::CreateSurfaceGrass(core::GameContext &ctx,
                                           float fieldWidth,
                                           float fieldDepth) {
  BeginSurfaceGrassBuild(ctx, fieldWidth, fieldDepth);
  UpdateSurfaceGrassChunks(ctx, 0.0f, 0.0f,
                           (std::numeric_limits<size_t>::max)());
}

void WikiTerrainSystem::BeginSurfaceGrassBuild(core::GameContext &ctx,
                                               float fieldWidth,
                                               float fieldDepth) {
  m_grassFieldWidth = fieldWidth;
  m_grassFieldDepth = fieldDepth;
  ClearSurfaceGrass(ctx);
  UpdateSurfaceGrassChunks(ctx, 0.0f, 0.0f, 0);
}

bool WikiTerrainSystem::StepSurfaceGrassBuild(core::GameContext &ctx) {
  UpdateSurfaceGrassChunks(ctx, 0.0f, 0.0f,
                           kGrassChunksGeneratedPerFrame);
  return m_pendingSurfaceGrassChunks.empty();
}

void WikiTerrainSystem::GenerateSurfaceGrassChunk(core::GameContext &ctx,
                                                  int streamChunkX,
                                                  int streamChunkZ) {
  const float fieldWidth = m_grassFieldWidth;
  const float fieldDepth = m_grassFieldDepth;
  if (!m_terrainData || fieldWidth <= 0.0f || fieldDepth <= 0.0f) {
    return;
  }

  const int resX = m_terrainData->config.resolutionX;
  const int resZ = m_terrainData->config.resolutionZ;
  if (resX < 2 || resZ < 2 || m_terrainData->materialMap.empty()) {
    return;
  }

  core::GraphicsPreset graphicsPreset = core::GraphicsPreset::High;
  if (ctx.displaySettings) {
    graphicsPreset = ctx.displaySettings->GetEffectiveGraphicsPreset();
  }
  if (graphicsPreset == core::GraphicsPreset::Low) {
    return;
  }

  struct VegetationQuality {
    float roughSpacing;
    float semiRoughDrawDistance;
    float roughDrawDistance;
    float turfSpacing;
    float greenLodDistance;
    float fairwayLodDistance;
    float greenDrawDistance;
    float fairwayDrawDistance;
  } quality{};
  switch (graphicsPreset) {
  case core::GraphicsPreset::Medium:
    quality = {0.85f, 40.0f, 52.0f, 1.20f,
               4.0f, 5.0f, 16.0f, 19.0f};
    break;
  case core::GraphicsPreset::ExHigh:
    quality = {0.72f, 46.0f, 60.0f, 1.05f,
               6.0f, 8.0f, 19.0f, 22.0f};
    break;
  case core::GraphicsPreset::Ultra:
    quality = {0.50f, 70.0f, 90.0f, 0.70f,
               10.0f, 13.0f, 26.0f, 32.0f};
    break;
  case core::GraphicsPreset::High:
  default:
    quality = {0.72f, 46.0f, 60.0f, 1.05f,
               5.0f, 6.0f, 18.0f, 20.0f};
    break;
  }

  // 1パッチ内を高密度の芝床として生成し、パッチ自体は適度に広げて重ねる。
  // 小さな草株を大量に並べる方式より、連続面としてのラフを保ちやすい。
  const float spacing = quality.roughSpacing;
  const int columns =
      std::max(1, static_cast<int>(std::ceil(fieldWidth * 0.96f / spacing)));
  const int rows =
      std::max(1, static_cast<int>(std::ceil(fieldDepth * 0.96f / spacing)));
  const unsigned grassSeed = static_cast<unsigned>(
      resX * 73856093u ^ resZ * 19349663u ^ (m_biome + 1) * 83492791u);
  const float fieldHalfWidth = fieldWidth * 0.5f;
  const float fieldHalfDepth = fieldDepth * 0.5f;
  GrassStreamingBounds streamingBounds;
  streamingBounds.minX = std::max(
      -fieldHalfWidth, static_cast<float>(streamChunkX) * kGrassStreamChunkSize);
  streamingBounds.maxX = std::min(
      fieldHalfWidth,
      static_cast<float>(streamChunkX + 1) * kGrassStreamChunkSize);
  streamingBounds.minZ = std::max(
      -fieldHalfDepth, static_cast<float>(streamChunkZ) * kGrassStreamChunkSize);
  streamingBounds.maxZ = std::min(
      fieldHalfDepth,
      static_cast<float>(streamChunkZ + 1) * kGrassStreamChunkSize);
  if (streamingBounds.minX >= streamingBounds.maxX ||
      streamingBounds.minZ >= streamingBounds.maxZ) {
    return;
  }
  const uint64_t streamChunkKey =
      MakeGrassStreamChunkKey(streamChunkX, streamChunkZ);
  auto &ownedEntities = m_surfaceGrassEntitiesByChunk[streamChunkKey];

  auto materialAt = [&](float x, float z) {
    const float u = x / fieldWidth + 0.5f;
    const float v = 0.5f - z / fieldDepth;
    const int gx = std::clamp(
        static_cast<int>(u * static_cast<float>(resX - 1) + 0.5f), 0,
        resX - 1);
    const int gz = std::clamp(
        static_cast<int>(v * static_cast<float>(resZ - 1) + 0.5f), 0,
        resZ - 1);
    return static_cast<TerrainMaterial>(
        m_terrainData->materialMap[gz * resX + gx]);
  };

  // 芝が生えてよいマテリアルか（バンカー・水・溶岩・氷・石畳などは不可）。
  auto isGrassCompatibleMaterial = [](TerrainMaterial material) {
    return material == TerrainMaterial::Fairway ||
           material == TerrainMaterial::Rough ||
           material == TerrainMaterial::Green;
  };

  // パッチはセル中心1点のマテリアルだけで配置を決めるが、実際の描画サイズは
  // ほぼセル間隔と同じ幅を持つため、中心が芝地でもパッチの四隅がバンカー等の
  // 隣接セルへはみ出すことがある。四隅を複数半径で確認し、非対応マテリアルに
  // 食い込まない最大の確認半径を、そのまま「配置してよい確率」として返す
  // （1.0=常に配置、0.0=配置しない）。パッチ自体のサイズはフルサイズのまま
  // 確率的に間引くことで密度を滑らかに下げる。パッチを縮小してしまうと、
  // 地形テクスチャがなめらかに混ざる境界の上に周囲より一回り小さい正方形が
  // ぽつぽつ浮いて見え、むしろ形状が目立ってしまうため採らない。
  auto computeEdgeCoverage = [&](float x, float z, float halfExtent) {
    static constexpr float kProbeSteps[] = {1.0f, 0.8f, 0.6f, 0.4f, 0.2f};
    static constexpr float kCornerOffsets[4][2] = {
        {-1.0f, -1.0f}, {1.0f, -1.0f}, {-1.0f, 1.0f}, {1.0f, 1.0f}};
    for (const float step : kProbeSteps) {
      const float testExtent = halfExtent * step;
      bool allInside = true;
      for (const auto &offset : kCornerOffsets) {
        const TerrainMaterial cornerMaterial = materialAt(
            x + offset[0] * testExtent, z + offset[1] * testExtent);
        if (!isGrassCompatibleMaterial(cornerMaterial)) {
          allInside = false;
          break;
        }
      }
      if (allInside) {
        return step;
      }
    }
    return 0.0f;
  };

  // 近傍の高さから地形の法線を求め、株の「上」をその法線に合わせる
  // 回転を返す。坂の途中で根本が斜面にめり込んだり浮いたりしないよう、
  // ラフと境界のセミラフで共通利用する。あわせて、株を法線に合わせて
  // 傾けるほど水平面から見た footprint が斜面の cos 分だけ縮み、同じ
  // グリッド間隔でも急な坂ほど株の間に隙間が見えてまばらになるため、
  // その分を補う横方向の拡大率も返す。
  auto computeSlopeAlignQuat = [&](float x, float z,
                                   float *outFootprintScale) {
    constexpr float normalSampleOffset = 0.15f;
    const float heightLeft = GetHeight(x - normalSampleOffset, z);
    const float heightRight = GetHeight(x + normalSampleOffset, z);
    const float heightDown = GetHeight(x, z - normalSampleOffset);
    const float heightUp = GetHeight(x, z + normalSampleOffset);
    XMVECTOR slopeNormal = XMVector3Normalize(XMVectorSet(
        (heightLeft - heightRight) / (2.0f * normalSampleOffset), 1.0f,
        (heightDown - heightUp) / (2.0f * normalSampleOffset), 0.0f));

    if (outFootprintScale) {
      const float slopeCos =
          std::clamp(XMVectorGetY(slopeNormal), 0.55f, 1.0f);
      *outFootprintScale = 1.0f / slopeCos;
    }

    XMVECTOR worldUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    XMVECTOR alignAxis = XMVector3Cross(worldUp, slopeNormal);
    const float alignAxisLenSq = XMVectorGetX(XMVector3LengthSq(alignAxis));
    if (alignAxisLenSq <= 1e-8f) {
      return XMQuaternionIdentity();
    }
    const float cosAngle =
        std::clamp(XMVectorGetX(XMVector3Dot(worldUp, slopeNormal)), -1.0f,
                  1.0f);
    return XMQuaternionRotationAxis(XMVector3Normalize(alignAxis),
                                    std::acos(cosAngle));
  };

  // 同じ形のパッチが並んで見えないよう、配置ごとに複数のメッシュ
  // バリアント（別seedで生成した葉の配置）から選ぶ。
  constexpr int kGrassVariantCount = 4;
  std::string grassMeshPrefix = "builtin/grass_patch_";
  if (graphicsPreset == core::GraphicsPreset::Medium) {
    grassMeshPrefix = "builtin/grass_patch_medium_";
  } else if (graphicsPreset == core::GraphicsPreset::Ultra) {
    grassMeshPrefix = "builtin/grass_patch_ultra_";
  }
  resources::MeshHandle grassMeshVariants[kGrassVariantCount] = {
      ctx.resource.LoadMesh(grassMeshPrefix + "0"),
      ctx.resource.LoadMesh(grassMeshPrefix + "1"),
      ctx.resource.LoadMesh(grassMeshPrefix + "2"),
      ctx.resource.LoadMesh(grassMeshPrefix + "3"),
  };
  auto grassShader = ctx.resource.LoadShader(
      "Grass", L"Assets/shaders/GrassVS.hlsl",
      L"Assets/shaders/GrassPS.hlsl");

  auto *grassSpatialIndex = ctx.world.GetGlobal<GrassRenderSpatialIndex>();
  if (!grassSpatialIndex) {
    ctx.world.SetGlobal(GrassRenderSpatialIndex{});
    grassSpatialIndex = ctx.world.GetGlobal<GrassRenderSpatialIndex>();
  }
  if (!grassSpatialIndex) {
    return;
  }
  grassSpatialIndex->chunkSize = kGrassStreamChunkSize;
  enum class GrassSurfaceGroup {
    Rough,
    SemiRough,
    Fairway,
    Green,
  };
  struct GrassBatchKey {
    int chunkX = 0;
    int chunkZ = 0;
    int variantIndex = 0;
    GrassSurfaceGroup surface = GrassSurfaceGroup::Rough;

    bool operator==(const GrassBatchKey &other) const {
      return chunkX == other.chunkX && chunkZ == other.chunkZ &&
             variantIndex == other.variantIndex && surface == other.surface;
    }
  };
  struct GrassBatchKeyHash {
    size_t operator()(const GrassBatchKey &key) const {
      size_t hash = static_cast<size_t>(key.chunkX);
      hash = hash * 31u + static_cast<size_t>(key.chunkZ);
      hash = hash * 31u + static_cast<size_t>(key.variantIndex);
      hash = hash * 31u + static_cast<size_t>(key.surface);
      return hash;
    }
  };

  std::unordered_map<GrassBatchKey, ecs::Entity, GrassBatchKeyHash>
      grassBatches;
  auto appendGrassInstance =
      [&](float x, float y, float z, float horizontalScale,
          float heightScale, const XMFLOAT4 &rotation, const XMFLOAT4 &color,
          resources::MeshHandle mesh, resources::MeshHandle lodMesh,
          int variantIndex, GrassSurfaceGroup surface, float lodSwitchDistance,
          float maxDrawDistance, bool twoSided) {
        // GrassVSの距離ディザーが完了する前にCPU側でインスタンスを
        // 丸ごと落とすと、残っていた葉がパッチ形状のまま瞬時に消える。
        // 最遠頂点までフェード終了距離へ到達できる余白を確保する。
        const float shaderFadeEnd = 14.0f + color.w * 50.0f;
        const float effectiveMaxDrawDistance =
            std::max(maxDrawDistance, shaderFadeEnd + horizontalScale);

        GrassBatchKey key;
        key.chunkX =
            static_cast<int>(std::floor(x / kGrassStreamChunkSize));
        key.chunkZ =
            static_cast<int>(std::floor(z / kGrassStreamChunkSize));
        key.variantIndex = variantIndex;
        key.surface = surface;

        ecs::Entity batchEntity = 0xFFFFFFFF;
        const auto batchIt = grassBatches.find(key);
        if (batchIt == grassBatches.end()) {
          batchEntity = ctx.world.CreateEntity();
          auto &newBatch = ctx.world.Add<GrassRenderBatch>(batchEntity);
          newBatch.mesh = mesh;
          newBatch.lodMesh = lodMesh;
          newBatch.shader = grassShader;
          newBatch.lodSwitchDistance = lodSwitchDistance;
          newBatch.maxDrawDistance = effectiveMaxDrawDistance;
          newBatch.maxThreeDOverheadRatio = 1.1f;
          if (surface == GrassSurfaceGroup::Fairway ||
              surface == GrassSurfaceGroup::Green) {
            newBatch.maxThreeDOverheadRatio = 0.75f;
          }
          newBatch.twoSided = twoSided;
          grassBatches.emplace(key, batchEntity);
          grassSpatialIndex
              ->batchesByChunk[GrassRenderSpatialIndex::MakeKey(
                  key.chunkX, key.chunkZ)]
              .push_back(batchEntity);
          m_entities.push_back(batchEntity);
          m_surfaceGrassEntities.push_back(batchEntity);
          ownedEntities.push_back(batchEntity);
          ctx.world.Add<TerrainObject>(batchEntity);
        } else {
          batchEntity = batchIt->second;
        }

        auto *batch = ctx.world.Get<GrassRenderBatch>(batchEntity);
        if (!batch) {
          return;
        }
        batch->maxDrawDistance =
            std::max(batch->maxDrawDistance, effectiveMaxDrawDistance);
        grassSpatialIndex->maxDrawDistance = std::max(
            grassSpatialIndex->maxDrawDistance, batch->maxDrawDistance);
        grassSpatialIndex->maxHorizontalExtent = std::max(
            grassSpatialIndex->maxHorizontalExtent, horizontalScale);

        Transform transform;
        transform.position = {x, y, z};
        transform.scale = {horizontalScale, heightScale, horizontalScale};
        transform.rotation = rotation;

        GrassRenderInstance instance;
        XMStoreFloat4x4(&instance.world, transform.GetWorldMatrix());
        instance.color = color;
        instance.position = transform.position;

        const size_t instanceIndex = batch->instances.size();
        batch->instances.push_back(instance);

        const float horizontalExtent = horizontalScale;
        const float verticalExtent = std::max(0.08f, heightScale * 1.35f);
        batch->boundsMin.x =
            std::min(batch->boundsMin.x, x - horizontalExtent);
        batch->boundsMin.y =
            std::min(batch->boundsMin.y, y - verticalExtent);
        batch->boundsMin.z =
            std::min(batch->boundsMin.z, z - horizontalExtent);
        batch->boundsMax.x =
            std::max(batch->boundsMax.x, x + horizontalExtent);
        batch->boundsMax.y =
            std::max(batch->boundsMax.y, y + verticalExtent);
        batch->boundsMax.z =
            std::max(batch->boundsMax.z, z + horizontalExtent);

        GrassPatch grass;
        grass.entity = batchEntity;
        grass.instanceIndex = instanceIndex;
        grass.position = transform.position;
        grass.halfExtent = horizontalScale * 0.72f;
        m_grassPatches.push_back(grass);
      };

  const float startX = -0.5f * static_cast<float>(columns - 1) * spacing;
  const float startZ = -0.5f * static_cast<float>(rows - 1) * spacing;
  // パッチ数（＝間隔spacing）を増やすと描画インスタンスが増えて重くなる
  // ため、パッチ1枚自体を大きく描画して隣接パッチへの重なりを広く取る
  // ことで、追加コストなしに隙間を埋める。1.08倍程度の重なりだと共有幅が
  // 数%しかなく、株の塊同士の間に隙間ができて四角く点在して見えていた。
  const float horizontalScale = spacing * 1.7f;
  const float roughJitterRadius = spacing * 0.30f;
  const int firstRow = std::clamp(
      static_cast<int>(std::floor(
          (streamingBounds.minZ - startZ - roughJitterRadius) / spacing)),
      0, rows - 1);
  const int lastRow = std::clamp(
      static_cast<int>(std::ceil(
          (streamingBounds.maxZ - startZ + roughJitterRadius) / spacing)),
      0, rows - 1);

  for (int row = firstRow; row <= lastRow; ++row) {
    // 一行ごとに半間隔ずらす千鳥配置で、格子模様の集合体に見えるのを防ぐ。
    float rowStagger = spacing * 0.5f;
    if (row % 2 == 0) {
      rowStagger = 0.0f;
    }
    const int firstColumn = std::clamp(
        static_cast<int>(std::floor(
            (streamingBounds.minX - startX - rowStagger -
             roughJitterRadius) /
            spacing)),
        0, columns - 1);
    const int lastColumn = std::clamp(
        static_cast<int>(std::ceil(
            (streamingBounds.maxX - startX - rowStagger +
             roughJitterRadius) /
            spacing)),
        0, columns - 1);
    for (int column = firstColumn; column <= lastColumn; ++column) {
      const unsigned cellSeed = MakeGrassCellSeed(grassSeed, row, column);
      const float jitterX =
          (GrassRandom01(cellSeed, 0) * 2.0f - 1.0f) * roughJitterRadius;
      const float jitterZ =
          (GrassRandom01(cellSeed, 1) * 2.0f - 1.0f) * roughJitterRadius;
      const float x = startX + static_cast<float>(column) * spacing +
                      rowStagger + jitterX;
      const float z = startZ + static_cast<float>(row) * spacing +
                      jitterZ;
      if (x < streamingBounds.minX || x >= streamingBounds.maxX ||
          z < streamingBounds.minZ || z >= streamingBounds.maxZ) {
        continue;
      }
      const TerrainMaterial material = materialAt(x, z);
      const float edgeSample = std::max(0.8f, spacing * 1.4f);
      const bool bordersRough =
          material == TerrainMaterial::Fairway &&
          (materialAt(x - edgeSample, z) == TerrainMaterial::Rough ||
           materialAt(x + edgeSample, z) == TerrainMaterial::Rough ||
           materialAt(x, z - edgeSample) == TerrainMaterial::Rough ||
           materialAt(x, z + edgeSample) == TerrainMaterial::Rough);
      const bool isRough = material == TerrainMaterial::Rough;
      const bool isSemiRough =
          bordersRough && GrassRandom01(cellSeed, 2) < 0.62f;
      if (!isRough && !isSemiRough) {
        continue;
      }
      const float edgeCoverage =
          computeEdgeCoverage(x, z, horizontalScale * 0.5f);
      if (edgeCoverage <= 0.0f) {
        continue;
      }
      if (edgeCoverage < 1.0f &&
          GrassRandom01(cellSeed, 3) > edgeCoverage) {
        continue;
      }

      const float variation = GrassRandom01(cellSeed, 4);
      float heightScale = 0.145f + variation * 0.045f;
      XMFLOAT4 color{0.38f + variation * 0.035f,
                     0.58f + variation * 0.045f,
                     0.21f + variation * 0.025f, 0.88f};
      if (isSemiRough) {
        heightScale = 0.085f + variation * 0.025f;
        color = {0.46f + variation * 0.025f,
                 0.66f + variation * 0.035f,
                 0.27f + variation * 0.020f, 0.58f};
      }
      const float yaw = GrassRandom01(cellSeed, 5) * XM_2PI;
      const float terrainHeight = GetHeight(x, z);
      float slopeFootprintScale = 1.0f;
      const XMVECTOR alignQuat =
          computeSlopeAlignQuat(x, z, &slopeFootprintScale);
      const XMVECTOR yawQuat = XMQuaternionRotationRollPitchYaw(0.0f, yaw, 0.0f);

      XMFLOAT4 rotation;
      XMStoreFloat4(&rotation, XMQuaternionMultiply(yawQuat, alignQuat));

      // 格子座標から決定的にバリアントを選び、隣接パッチが同じ葉配置に
      // ならないようにする（乱数列は消費せず配置を安定させる）。
      const unsigned variantHash = static_cast<unsigned>(row) * 374761393u ^
                                   static_cast<unsigned>(column) * 668265263u;
      const int variantIndex =
          static_cast<int>((variantHash >> 13) % kGrassVariantCount);

      GrassSurfaceGroup surface = GrassSurfaceGroup::Rough;
      if (isSemiRough) {
        surface = GrassSurfaceGroup::SemiRough;
      }
      float drawDistance = quality.roughDrawDistance;
      if (isSemiRough) {
        drawDistance = quality.semiRoughDrawDistance;
      }
      appendGrassInstance(
          x, terrainHeight + 0.003f, z,
          horizontalScale * slopeFootprintScale, heightScale,
          rotation, color, grassMeshVariants[variantIndex],
          resources::MeshHandle::Invalid(), variantIndex, surface, 0.0f,
          drawDistance, false);
    }
  }

  // Fairway / Greenは同寸法のセルを隙間なく並べ、近距離の3D葉から
  // 中遠距離の地表シェーダーへディザーフェードで連続させる。
  const float turfSpacing = quality.turfSpacing;
  const int turfColumns = std::max(
      1, static_cast<int>(std::ceil(fieldWidth * 0.96f / turfSpacing)));
  const int turfRows = std::max(
      1, static_cast<int>(std::ceil(fieldDepth * 0.96f / turfSpacing)));
  constexpr int kTurfVariantCount = 4;
  resources::MeshHandle baseTurfMeshVariants[kTurfVariantCount] = {
      ctx.resource.LoadMesh("builtin/turf_patch_0"),
      ctx.resource.LoadMesh("builtin/turf_patch_1"),
      ctx.resource.LoadMesh("builtin/turf_patch_2"),
      ctx.resource.LoadMesh("builtin/turf_patch_3"),
  };
  const resources::MeshHandle invalid = resources::MeshHandle::Invalid();
  resources::MeshHandle denseTurfMeshVariants[kTurfVariantCount] = {
      invalid, invalid, invalid, invalid,
  };
  resources::MeshHandle ultraDenseTurfMeshVariants[kTurfVariantCount] = {
      invalid, invalid, invalid, invalid,
  };
  resources::MeshHandle denseFairwayMeshVariants[kTurfVariantCount] = {
      invalid, invalid, invalid, invalid,
  };
  resources::MeshHandle ultraDenseFairwayMeshVariants[kTurfVariantCount] = {
      invalid, invalid, invalid, invalid,
  };

  if (graphicsPreset != core::GraphicsPreset::Low) {
    for (int i = 0; i < kTurfVariantCount; ++i) {
      denseTurfMeshVariants[i] = ctx.resource.LoadMesh(
          "builtin/turf_patch_dense_" + std::to_string(i));
    }
  }
  if (graphicsPreset == core::GraphicsPreset::High ||
      graphicsPreset == core::GraphicsPreset::ExHigh ||
      graphicsPreset == core::GraphicsPreset::Ultra) {
    for (int i = 0; i < kTurfVariantCount; ++i) {
      denseFairwayMeshVariants[i] = ctx.resource.LoadMesh(
          "builtin/fairway_turf_patch_dense_" + std::to_string(i));
    }
  }
  if (graphicsPreset == core::GraphicsPreset::ExHigh ||
      graphicsPreset == core::GraphicsPreset::Ultra) {
    for (int i = 0; i < kTurfVariantCount; ++i) {
      ultraDenseTurfMeshVariants[i] = ctx.resource.LoadMesh(
          "builtin/turf_patch_ultra_" + std::to_string(i));
      ultraDenseFairwayMeshVariants[i] = ctx.resource.LoadMesh(
          "builtin/fairway_turf_patch_ultra_" + std::to_string(i));
    }
  }

  const float turfHorizontalScale = turfSpacing;
  const float turfStartX =
      -0.5f * static_cast<float>(turfColumns - 1) * turfSpacing;
  const float turfStartZ =
      -0.5f * static_cast<float>(turfRows - 1) * turfSpacing;
  const int firstTurfRow = std::clamp(
      static_cast<int>(std::floor(
          (streamingBounds.minZ - turfStartZ) / turfSpacing)),
      0, turfRows - 1);
  const int lastTurfRow = std::clamp(
      static_cast<int>(std::ceil(
          (streamingBounds.maxZ - turfStartZ) / turfSpacing)),
      0, turfRows - 1);
  const int firstTurfColumn = std::clamp(
      static_cast<int>(std::floor(
          (streamingBounds.minX - turfStartX) / turfSpacing)),
      0, turfColumns - 1);
  const int lastTurfColumn = std::clamp(
      static_cast<int>(std::ceil(
          (streamingBounds.maxX - turfStartX) / turfSpacing)),
      0, turfColumns - 1);

  for (int row = firstTurfRow; row <= lastTurfRow; ++row) {
    for (int column = firstTurfColumn; column <= lastTurfColumn; ++column) {
      const float x =
          turfStartX + static_cast<float>(column) * turfSpacing;
      const float z = turfStartZ + static_cast<float>(row) * turfSpacing;
      if (x < streamingBounds.minX || x >= streamingBounds.maxX ||
          z < streamingBounds.minZ || z >= streamingBounds.maxZ) {
        continue;
      }
      const TerrainMaterial material = materialAt(x, z);
      if (material != TerrainMaterial::Fairway &&
          material != TerrainMaterial::Green) {
        continue;
      }
      // ターフは平たいカード状メッシュで隙間なく敷き詰める設計のため、
      // 間引いたり縮小したりすると孤立したカード1枚がそのまま正方形の
      // シルエットとして浮いて見える。境界に一部でもかかるカードは
      // 確率で残さず一律に置かず、地形シェーダー側のなめらかな短芝表現
      // （TerrainPS.hlslのTurfFibers）へ委ねる。
      const float edgeCoverage =
          computeEdgeCoverage(x, z, turfHorizontalScale * 0.5f);
      if (edgeCoverage < 1.0f) {
        continue;
      }

      const bool isGreen = material == TerrainMaterial::Green;
      float heightScale = 0.0375f;
      XMFLOAT4 color{0.46f, 0.66f, 0.27f, 0.14f};
      float bandWidth = 3.2f;
      float bandCoordinate = x;
      if (isGreen) {
        heightScale = 0.012f;
        color = {0.53f, 0.71f, 0.32f, 0.06f};
        bandWidth = 1.8f;
        bandCoordinate = z;
      }
      const int bandIndex =
          static_cast<int>(std::floor(bandCoordinate / bandWidth));
      float reverseYaw = XM_PI;
      if (bandIndex % 2 == 0) {
        reverseYaw = 0.0f;
      }
      float baseYaw = 0.0f;
      if (isGreen) {
        baseYaw = XM_PIDIV2;
      }
      const float yaw = baseYaw + reverseYaw;

      const float terrainHeight = GetHeight(x, z);
      float slopeFootprintScale = 1.0f;
      const XMVECTOR alignQuat =
          computeSlopeAlignQuat(x, z, &slopeFootprintScale);
      const XMVECTOR yawQuat =
          XMQuaternionRotationRollPitchYaw(0.0f, yaw, 0.0f);

      XMFLOAT4 rotation;
      XMStoreFloat4(&rotation, XMQuaternionMultiply(yawQuat, alignQuat));

      const unsigned variantHash = static_cast<unsigned>(row) * 2246822519u ^
                                   static_cast<unsigned>(column) * 3266489917u;
      const int variantIndex =
          static_cast<int>((variantHash >> 15) % kTurfVariantCount);

      GrassSurfaceGroup surface = GrassSurfaceGroup::Fairway;
      if (isGreen) {
        surface = GrassSurfaceGroup::Green;
      }
      resources::MeshHandle nearTurfMesh = baseTurfMeshVariants[variantIndex];
      resources::MeshHandle midTurfMesh = resources::MeshHandle::Invalid();
      if (graphicsPreset == core::GraphicsPreset::Medium) {
        nearTurfMesh = denseTurfMeshVariants[variantIndex];
        midTurfMesh = baseTurfMeshVariants[variantIndex];
      } else if (graphicsPreset == core::GraphicsPreset::High) {
        nearTurfMesh = denseFairwayMeshVariants[variantIndex];
        if (isGreen) {
          nearTurfMesh = denseTurfMeshVariants[variantIndex];
        }
        midTurfMesh = baseTurfMeshVariants[variantIndex];
      } else if (graphicsPreset == core::GraphicsPreset::ExHigh ||
                 graphicsPreset == core::GraphicsPreset::Ultra) {
        nearTurfMesh = ultraDenseFairwayMeshVariants[variantIndex];
        midTurfMesh = denseFairwayMeshVariants[variantIndex];
        if (isGreen) {
          nearTurfMesh = ultraDenseTurfMeshVariants[variantIndex];
          midTurfMesh = denseTurfMeshVariants[variantIndex];
        }
      }
      float lodDistance = quality.fairwayLodDistance;
      float drawDistance = quality.fairwayDrawDistance;
      if (isGreen) {
        lodDistance = quality.greenLodDistance;
        drawDistance = quality.greenDrawDistance;
      }
      appendGrassInstance(
          x, terrainHeight + 0.0015f, z,
          turfHorizontalScale * slopeFootprintScale, heightScale,
          rotation, color, nearTurfMesh, midTurfMesh, variantIndex, surface,
          lodDistance, drawDistance, true);
    }
  }

}

void WikiTerrainSystem::ClearSurfaceGrass(core::GameContext &ctx) {
  for (ecs::Entity entity : m_surfaceGrassEntities) {
    if (ctx.world.IsAlive(entity)) {
      ctx.world.DestroyEntity(entity);
    }
  }
  m_entities.erase(
      std::remove_if(m_entities.begin(), m_entities.end(),
                     [&](ecs::Entity entity) {
                       return std::find(m_surfaceGrassEntities.begin(),
                                        m_surfaceGrassEntities.end(),
                                        entity) != m_surfaceGrassEntities.end();
                     }),
      m_entities.end());
  m_surfaceGrassEntities.clear();
  m_surfaceGrassEntitiesByChunk.clear();
  m_pendingSurfaceGrassChunks.clear();
  m_grassPatches.clear();
  m_grassViewChunkX = (std::numeric_limits<int>::max)();
  m_grassViewChunkZ = (std::numeric_limits<int>::max)();
  ctx.world.SetGlobal(GrassRenderSpatialIndex{});
}

void WikiTerrainSystem::RemoveSurfaceGrassChunks(
    core::GameContext &ctx, const std::vector<uint64_t> &chunkKeys) {
  std::unordered_set<ecs::Entity> removedEntities;
  for (uint64_t chunkKey : chunkKeys) {
    const auto chunkIt = m_surfaceGrassEntitiesByChunk.find(chunkKey);
    if (chunkIt == m_surfaceGrassEntitiesByChunk.end()) {
      continue;
    }
    removedEntities.insert(chunkIt->second.begin(), chunkIt->second.end());
  }
  if (removedEntities.empty()) {
    for (uint64_t chunkKey : chunkKeys) {
      m_surfaceGrassEntitiesByChunk.erase(chunkKey);
    }
    return;
  }
  for (ecs::Entity entity : removedEntities) {
    if (ctx.world.IsAlive(entity)) {
      ctx.world.DestroyEntity(entity);
    }
  }
  auto wasRemoved = [&](ecs::Entity entity) {
    return removedEntities.find(entity) != removedEntities.end();
  };
  m_entities.erase(
      std::remove_if(m_entities.begin(), m_entities.end(), wasRemoved),
      m_entities.end());
  m_surfaceGrassEntities.erase(
      std::remove_if(m_surfaceGrassEntities.begin(),
                     m_surfaceGrassEntities.end(), wasRemoved),
      m_surfaceGrassEntities.end());
  m_grassPatches.erase(
      std::remove_if(m_grassPatches.begin(), m_grassPatches.end(),
                     [&](const GrassPatch &grass) {
                       return wasRemoved(grass.entity);
                     }),
      m_grassPatches.end());

  auto *spatialIndex = ctx.world.GetGlobal<GrassRenderSpatialIndex>();
  if (spatialIndex) {
    for (auto batchIt = spatialIndex->batchesByChunk.begin();
         batchIt != spatialIndex->batchesByChunk.end();) {
      auto &entities = batchIt->second;
      entities.erase(
          std::remove_if(entities.begin(), entities.end(), wasRemoved),
          entities.end());
      if (entities.empty()) {
        batchIt = spatialIndex->batchesByChunk.erase(batchIt);
      } else {
        ++batchIt;
      }
    }
  }
  for (uint64_t chunkKey : chunkKeys) {
    m_surfaceGrassEntitiesByChunk.erase(chunkKey);
  }
}

void WikiTerrainSystem::UpdateSurfaceGrassChunks(
    core::GameContext &ctx, float centerX, float centerZ,
    size_t generationBudget) {
  core::GraphicsPreset graphicsPreset = core::GraphicsPreset::High;
  if (ctx.displaySettings) {
    graphicsPreset = ctx.displaySettings->GetEffectiveGraphicsPreset();
  }
  if (graphicsPreset == core::GraphicsPreset::Low) {
    if (!m_surfaceGrassEntitiesByChunk.empty()) {
      ClearSurfaceGrass(ctx);
    }
    return;
  }

  const int viewChunkX =
      static_cast<int>(std::floor(centerX / kGrassStreamChunkSize));
  const int viewChunkZ =
      static_cast<int>(std::floor(centerZ / kGrassStreamChunkSize));
  const bool windowChanged = viewChunkX != m_grassViewChunkX ||
                             viewChunkZ != m_grassViewChunkZ;

  if (windowChanged) {
    const float stableCenterX =
        (static_cast<float>(viewChunkX) + 0.5f) * kGrassStreamChunkSize;
    const float stableCenterZ =
        (static_cast<float>(viewChunkZ) + 0.5f) * kGrassStreamChunkSize;
    const GrassStreamingBounds bounds = CalculateGrassStreamingBounds(
        m_grassFieldWidth, m_grassFieldDepth, stableCenterX, stableCenterZ,
        GrassStreamingDrawDistance(graphicsPreset), kGrassStreamingPadding);
    const float maxX = std::nextafter(
        bounds.maxX, -(std::numeric_limits<float>::infinity)());
    const float maxZ = std::nextafter(
        bounds.maxZ, -(std::numeric_limits<float>::infinity)());
    const int minChunkX =
        static_cast<int>(std::floor(bounds.minX / kGrassStreamChunkSize));
    const int maxChunkX =
        static_cast<int>(std::floor(maxX / kGrassStreamChunkSize));
    const int minChunkZ =
        static_cast<int>(std::floor(bounds.minZ / kGrassStreamChunkSize));
    const int maxChunkZ =
        static_cast<int>(std::floor(maxZ / kGrassStreamChunkSize));

    std::unordered_set<uint64_t> desiredChunks;
    for (int chunkZ = minChunkZ; chunkZ <= maxChunkZ; ++chunkZ) {
      for (int chunkX = minChunkX; chunkX <= maxChunkX; ++chunkX) {
        desiredChunks.insert(MakeGrassStreamChunkKey(chunkX, chunkZ));
      }
    }

    std::vector<uint64_t> chunksToRemove;
    for (const auto &[chunkKey, entities] :
         m_surfaceGrassEntitiesByChunk) {
      if (desiredChunks.find(chunkKey) == desiredChunks.end()) {
        chunksToRemove.push_back(chunkKey);
      }
    }
    RemoveSurfaceGrassChunks(ctx, chunksToRemove);

    struct PendingChunk {
      uint64_t key = 0;
      int distanceSquared = 0;
    };
    std::vector<PendingChunk> pendingChunks;
    for (uint64_t chunkKey : desiredChunks) {
      if (m_surfaceGrassEntitiesByChunk.find(chunkKey) !=
          m_surfaceGrassEntitiesByChunk.end()) {
        continue;
      }
      const int dx = GrassStreamChunkX(chunkKey) - viewChunkX;
      const int dz = GrassStreamChunkZ(chunkKey) - viewChunkZ;
      pendingChunks.push_back({chunkKey, dx * dx + dz * dz});
    }
    std::sort(pendingChunks.begin(), pendingChunks.end(),
              [](const PendingChunk &a, const PendingChunk &b) {
                return a.distanceSquared < b.distanceSquared;
              });
    m_pendingSurfaceGrassChunks.clear();
    for (const PendingChunk &pending : pendingChunks) {
      m_pendingSurfaceGrassChunks.push_back(pending.key);
    }
    m_grassViewChunkX = viewChunkX;
    m_grassViewChunkZ = viewChunkZ;
  }

  size_t generatedChunks = 0;
  while (!m_pendingSurfaceGrassChunks.empty() &&
         generatedChunks < generationBudget) {
    const uint64_t chunkKey = m_pendingSurfaceGrassChunks.front();
    m_pendingSurfaceGrassChunks.pop_front();
    if (m_surfaceGrassEntitiesByChunk.find(chunkKey) ==
        m_surfaceGrassEntitiesByChunk.end()) {
      GenerateSurfaceGrassChunk(ctx, GrassStreamChunkX(chunkKey),
                                GrassStreamChunkZ(chunkKey));
    }
    ++generatedChunks;
  }
  auto &profiler = core::Profiler::Instance();
  profiler.SetCounter("GrassStreaming.ActiveChunks",
                      static_cast<double>(
                          m_surfaceGrassEntitiesByChunk.size()));
  profiler.SetCounter("GrassStreaming.PendingChunks",
                      static_cast<double>(m_pendingSurfaceGrassChunks.size()));
  profiler.SetCounter("GrassStreaming.GeneratedChunks",
                      static_cast<double>(generatedChunks));
}

void WikiTerrainSystem::UpdateSurfaceGrass(core::GameContext &ctx,
                                           ecs::Entity cameraEntity) {
  if (!m_terrainData || m_grassFieldWidth <= 0.0f ||
      m_grassFieldDepth <= 0.0f || !ctx.world.IsAlive(cameraEntity)) {
    return;
  }
  const auto *cameraTransform = ctx.world.Get<Transform>(cameraEntity);
  if (!cameraTransform) {
    return;
  }

  UpdateSurfaceGrassChunks(ctx, cameraTransform->position.x,
                           cameraTransform->position.z,
                           kGrassChunksGeneratedPerFrame);
}

} // namespace game::systems

