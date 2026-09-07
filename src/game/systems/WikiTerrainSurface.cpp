/**
 * @file WikiTerrainSurface.cpp
 * @brief 地形表面の芝生成を実装します。
*/

#include "WikiTerrainSystem.h"
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
#include <random>

namespace game::systems {

using namespace DirectX;
using namespace game::components;

void WikiTerrainSystem::CreateSurfaceGrass(core::GameContext &ctx,
                                           float fieldWidth,
                                           float fieldDepth) {
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
    m_grassPatches.clear();
    ctx.world.SetGlobal(GrassRenderSpatialIndex{});
    LOG_INFO("WikiTerrain",
             "Surface grass disabled for LOW graphics preset");
    return;
  }

  struct VegetationQuality {
    float roughSpacing;
    float maximumRoughPatches;
    float semiRoughDrawDistance;
    float roughDrawDistance;
    float turfSpacing;
    float maximumTurfPatches;
    float greenLodDistance;
    float fairwayLodDistance;
    float greenDrawDistance;
    float fairwayDrawDistance;
  } quality{};
  switch (graphicsPreset) {
  case core::GraphicsPreset::Medium:
    quality = {0.85f, 22000.0f, 40.0f, 52.0f, 1.20f, 11000.0f,
               4.0f, 5.0f, 16.0f, 19.0f};
    break;
  case core::GraphicsPreset::ExHigh:
    quality = {0.72f, 28000.0f, 46.0f, 60.0f, 1.05f, 14000.0f,
               6.0f, 8.0f, 19.0f, 22.0f};
    break;
  case core::GraphicsPreset::Ultra:
    quality = {0.50f, 60000.0f, 70.0f, 90.0f, 0.70f, 32000.0f,
               10.0f, 13.0f, 26.0f, 32.0f};
    break;
  case core::GraphicsPreset::High:
  default:
    quality = {0.72f, 28000.0f, 46.0f, 60.0f, 1.05f, 14000.0f,
               5.0f, 6.0f, 18.0f, 20.0f};
    break;
  }

  // 1パッチ内を高密度の芝床として生成し、パッチ自体は適度に広げて重ねる。
  // 小さな草株を大量に並べる方式より、連続面としてのラフを保ちやすい。
  const float fieldArea = fieldWidth * fieldDepth;
  const float spacing =
      std::max(quality.roughSpacing,
               std::sqrt(fieldArea / quality.maximumRoughPatches));
  const int columns =
      std::max(1, static_cast<int>(std::ceil(fieldWidth * 0.96f / spacing)));
  const int rows =
      std::max(1, static_cast<int>(std::ceil(fieldDepth * 0.96f / spacing)));
  std::mt19937 rng(static_cast<unsigned>(
      resX * 73856093u ^ resZ * 19349663u ^ (m_biome + 1) * 83492791u));
  std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
  std::uniform_real_distribution<float> distYaw(0.0f, XM_2PI);
  std::uniform_real_distribution<float> distJitter(-spacing * 0.30f,
                                                    spacing * 0.30f);

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

  constexpr float kGrassChunkSize = 12.0f;
  GrassRenderSpatialIndex grassSpatialIndex;
  grassSpatialIndex.chunkSize = kGrassChunkSize;
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
  int batchCreated = 0;

  auto appendGrassInstance =
      [&](float x, float y, float z, float horizontalScale,
          float heightScale, const XMFLOAT4 &rotation, const XMFLOAT4 &color,
          resources::MeshHandle mesh, resources::MeshHandle lodMesh,
          int variantIndex, GrassSurfaceGroup surface, float lodSwitchDistance,
          float maxDrawDistance, bool twoSided) {
        GrassBatchKey key;
        key.chunkX = static_cast<int>(std::floor(x / kGrassChunkSize));
        key.chunkZ = static_cast<int>(std::floor(z / kGrassChunkSize));
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
          newBatch.maxDrawDistance = maxDrawDistance;
          newBatch.maxThreeDOverheadRatio = 1.1f;
          if (surface == GrassSurfaceGroup::Fairway ||
              surface == GrassSurfaceGroup::Green) {
            newBatch.maxThreeDOverheadRatio = 0.75f;
          }
          newBatch.twoSided = twoSided;
          grassBatches.emplace(key, batchEntity);
          grassSpatialIndex
              .batchesByChunk[GrassRenderSpatialIndex::MakeKey(
                  key.chunkX, key.chunkZ)]
              .push_back(batchEntity);
          m_entities.push_back(batchEntity);
          ctx.world.Add<TerrainObject>(batchEntity);
          ++batchCreated;
        } else {
          batchEntity = batchIt->second;
        }

        auto *batch = ctx.world.Get<GrassRenderBatch>(batchEntity);
        if (!batch) {
          return;
        }
        grassSpatialIndex.maxDrawDistance =
            std::max(grassSpatialIndex.maxDrawDistance, maxDrawDistance);
        grassSpatialIndex.maxHorizontalExtent =
            std::max(grassSpatialIndex.maxHorizontalExtent, horizontalScale);

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

  int created = 0;
  int roughCount = 0;
  int semiRoughCount = 0;
  const float startX = -0.5f * static_cast<float>(columns - 1) * spacing;
  const float startZ = -0.5f * static_cast<float>(rows - 1) * spacing;
  // パッチ数（＝間隔spacing）を増やすと描画インスタンスが増えて重くなる
  // ため、パッチ1枚自体を大きく描画して隣接パッチへの重なりを広く取る
  // ことで、追加コストなしに隙間を埋める。1.08倍程度の重なりだと共有幅が
  // 数%しかなく、株の塊同士の間に隙間ができて四角く点在して見えていた。
  const float horizontalScale = spacing * 1.7f;

  for (int row = 0; row < rows; ++row) {
    // 一行ごとに半間隔ずらす千鳥配置で、格子模様の集合体に見えるのを防ぐ。
    float rowStagger = spacing * 0.5f;
    if (row % 2 == 0) {
      rowStagger = 0.0f;
    }
    for (int column = 0; column < columns; ++column) {
      const float x = startX + static_cast<float>(column) * spacing +
                      rowStagger + distJitter(rng);
      const float z = startZ + static_cast<float>(row) * spacing +
                      distJitter(rng);
      const TerrainMaterial material = materialAt(x, z);
      const float edgeSample = std::max(0.8f, spacing * 1.4f);
      const bool bordersRough =
          material == TerrainMaterial::Fairway &&
          (materialAt(x - edgeSample, z) == TerrainMaterial::Rough ||
           materialAt(x + edgeSample, z) == TerrainMaterial::Rough ||
           materialAt(x, z - edgeSample) == TerrainMaterial::Rough ||
           materialAt(x, z + edgeSample) == TerrainMaterial::Rough);
      const bool isRough = material == TerrainMaterial::Rough;
      const bool isSemiRough = bordersRough && dist01(rng) < 0.62f;
      if (!isRough && !isSemiRough) {
        continue;
      }
      const float edgeCoverage =
          computeEdgeCoverage(x, z, horizontalScale * 0.5f);
      if (edgeCoverage <= 0.0f) {
        continue;
      }
      if (edgeCoverage < 1.0f && dist01(rng) > edgeCoverage) {
        continue;
      }

      const float variation = dist01(rng);
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
      if (isRough) {
        ++roughCount;
      } else {
        ++semiRoughCount;
      }

      const float yaw = distYaw(rng);
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
          drawDistance,
          false);
      ++created;
    }
  }

  // Fairway / Greenは同寸法のセルを隙間なく並べ、近距離の3D葉から
  // 中遠距離の地表シェーダーへディザーフェードで連続させる。
  const float turfSpacing =
      std::max(quality.turfSpacing,
               std::sqrt(fieldArea / quality.maximumTurfPatches));
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
  int turfCreated = 0;
  int fairwayCount = 0;
  int greenCount = 0;

  for (int row = 0; row < turfRows; ++row) {
    for (int column = 0; column < turfColumns; ++column) {
      const float x =
          turfStartX + static_cast<float>(column) * turfSpacing;
      const float z = turfStartZ + static_cast<float>(row) * turfSpacing;
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
          lodDistance, drawDistance,
          true);
      ++turfCreated;
      if (isGreen) {
        ++greenCount;
      } else {
        ++fairwayCount;
      }
    }
  }

  LOG_INFO("WikiTerrain",
           "Created {} managed rough and transition grass patches "
           "(rough={}, semiRough={}, spacing={:.2f})",
           created, roughCount, semiRoughCount, spacing);
  LOG_INFO("WikiTerrain",
           "Created {} seamless mown turf cells "
           "(fairway={}, green={}, cellSize={:.2f})",
           turfCreated, fairwayCount, greenCount, turfSpacing);
  LOG_INFO("WikiTerrain",
           "Packed {} grass patches into {} spatial GPU instance batches "
           "(chunkSize={:.1f})",
           created + turfCreated, batchCreated, kGrassChunkSize);
  ctx.world.SetGlobal(std::move(grassSpatialIndex));
}

} // namespace game::systems

