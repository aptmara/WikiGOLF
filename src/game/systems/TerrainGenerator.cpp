/**
 * @file TerrainGenerator.cpp
 * @brief TerrainGenerator の実装
*/

#include "TerrainGeneratorInternals.h"
#include "TerrainGenerator.h"
#include "../../core/Logger.h"
#include "../../graphics/TangentGenerator.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace game::systems {

using namespace DirectX;

TerrainData TerrainGenerator::GenerateTerrain(
    const std::string &articleText,
    const std::vector<DirectX::XMFLOAT2> &holePositions,
    const TerrainConfig &config) {
  TerrainData data;
  data.config = config;

  // ハイトマップ初期化
  int totalVerts = config.resolutionX * config.resolutionZ;
  data.heightMap.resize(totalVerts, 0.0f);
  data.materialMap.resize(totalVerts, 0); // 0: Fairway

  if (config.htmlCourse) {
    std::vector<HtmlTerrainPoint> holes;
    for (const auto& p : holePositions) holes.push_back({p.x, p.y});
    BuildHtmlTerrain(config.resolutionX, config.resolutionZ, config.worldWidth,
        config.worldDepth, config.htmlRegions, holes, data.heightMap, data.materialMap,
        config.heightScale);
    // 非HTML経路と同様に、孤立したマテリアルの整理と起伏の平滑化を行う。
    // HTMLコースはフェザリング幅がグリッド解像度に依存するため、これを
    // 省くと解像度によっては段差やマテリアルのノイズが目立ちやすい。
    ApplyMaterialCleanup(data);
    GenerateVisualMaterialColors(data);
    ApplySmoothing(data, 2);
    CalculateNormals(data);
    GenerateMesh(data, holePositions);
    return data;
  }

  // 基本形状の生成
  GenerateBaseHeightMap(data, articleText);

  // リンク位置に基づくプラットフォームの生成
  CreatePlatforms(data, holePositions);

  // プレイヤーが狙いを読めるよう、小さな孤立地形を整理
  ApplyMaterialCleanup(data);

  // 物理材質は離散値のまま保ち、描画境界だけを連続化する。
  GenerateVisualMaterialColors(data);

  // スムージング処理
  ApplySmoothing(data, 3); // 3回スムージング

  // メッシュの生成
  CalculateNormals(data);
  GenerateMesh(data, holePositions); // ホール位置を渡す

  return data;
}

TerrainData TerrainGenerator::GenerateTutorialTerrain(
    const TerrainConfig &config,
    const std::vector<DirectX::XMFLOAT2> &holePositions) {
  TerrainData data;
  data.config = config;

  const int resX = config.resolutionX;
  const int resZ = config.resolutionZ;
  const int totalVerts = resX * resZ;
  data.heightMap.resize(totalVerts, config.baseHeight);
  data.materialMap.resize(totalVerts, 1); // Rough

  auto worldXAt = [&](int x) {
    const float u = static_cast<float>(x) / static_cast<float>(resX - 1);
    return (u - 0.5f) * config.worldWidth;
  };
  auto worldZAt = [&](int z) {
    const float v = static_cast<float>(z) / static_cast<float>(resZ - 1);
    return (0.5f - v) * config.worldDepth;
  };
  auto distSq = [](float ax, float az, float bx, float bz) {
    const float dx = ax - bx;
    const float dz = az - bz;
    return dx * dx + dz * dz;
  };

  for (int z = 0; z < resZ; ++z) {
    const float wz = worldZAt(z);
    for (int x = 0; x < resX; ++x) {
      const float wx = worldXAt(x);
      const int idx = z * resX + x;

      float fairwayCenterX = 0.0f;
      if (wz > -20.0f && wz < 35.0f) {
        fairwayCenterX = std::sin((wz + 20.0f) * 0.055f) * 7.0f;
      }

      float fairwayHalfWidth = 10.0f;
      if (wz > 25.0f) {
        fairwayHalfWidth = 14.0f;
      }
      const bool inMainFairway =
          wz > -config.worldDepth * 0.42f && wz < 42.0f &&
          std::abs(wx - fairwayCenterX) < fairwayHalfWidth;
      const bool inTee =
          distSq(wx, wz, 0.0f, -config.worldDepth * 0.40f) < 13.0f * 13.0f;
      const bool inGreen = distSq(wx, wz, 0.0f, 52.0f) < 15.0f * 15.0f;
      const bool inBunker = distSq(wx, wz, 24.0f, -3.0f) < 10.0f * 10.0f;
      const bool inWater = distSq(wx, wz, -24.0f, 20.0f) < 13.0f * 13.0f;

      float height =
          0.10f * std::sin(wx * 0.08f) + 0.08f * std::cos(wz * 0.06f);
      uint8_t material = 1; // Rough

      if (inMainFairway || inTee) {
        material = 0; // Fairway
        height *= 0.35f;
      }
      if (inBunker) {
        material = 2; // Bunker
        const float d = std::sqrt(distSq(wx, wz, 24.0f, -3.0f)) / 10.0f;
        height = -0.22f * (1.0f - std::clamp(d, 0.0f, 1.0f));
      }
      if (inWater) {
        material = 5; // Water
        height = -0.35f;
      }
      if (inGreen) {
        material = 3; // Green
        height = 0.02f;
      }

      data.materialMap[idx] = material;
      data.heightMap[idx] = height;
    }
  }

  for (const auto &pos : holePositions) {
    const float u = pos.x / config.worldWidth + 0.5f;
    const float v = 0.5f - pos.y / config.worldDepth;
    const int cx = std::clamp(static_cast<int>(u * (resX - 1)), 0, resX - 1);
    const int cz = std::clamp(static_cast<int>(v * (resZ - 1)), 0, resZ - 1);
    const int radius = std::max(3, resX / 16);
    const float baseH = GetHeight(data, cx, cz);

    for (int dz = -radius; dz <= radius; ++dz) {
      for (int dx = -radius; dx <= radius; ++dx) {
        const int gx = cx + dx;
        const int gz = cz + dz;
        if (gx < 0 || gx >= resX || gz < 0 || gz >= resZ) {
          continue;
        }
        const float d = std::sqrt(static_cast<float>(dx * dx + dz * dz));
        if (d > radius) {
          continue;
        }
        const float t = SmoothStep(static_cast<float>(radius), 0.0f, d);
        const int idx = gz * resX + gx;
        data.heightMap[idx] = Lerp(data.heightMap[idx], baseH + 0.02f, t);
        if (data.materialMap[idx] != 5 && data.materialMap[idx] != 2) {
          data.materialMap[idx] = 0;
          if (pos.y > 35.0f) {
            data.materialMap[idx] = 3;
          }
        }
      }
    }
  }

  GenerateVisualMaterialColors(data);
  ApplySmoothing(data, 1);
  CalculateNormals(data);
  GenerateMesh(data, holePositions);

  return data;
}

/**
 * @brief 基準ハイト�（チ（�（を生成します、（
*/

} // namespace game::systems
