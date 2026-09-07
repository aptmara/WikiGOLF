#include "TerrainGeneratorInternals.h"
#include "TerrainGenerator.h"
#include "../../core/Logger.h"
#include "../../graphics/TangentGenerator.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace game::systems {

using namespace DirectX;

void TerrainGenerator::ApplySmoothing(TerrainData &data, int iterations) {
  int resX = data.config.resolutionX;
  int resZ = data.config.resolutionZ;
  std::vector<float> tempMap = data.heightMap;

  for (int iter = 0; iter < iterations; ++iter) {
    for (int z = 1; z < resZ - 1; ++z) {
      for (int x = 1; x < resX - 1; ++x) {
        // 3x3 平均
        float sum = 0.0f;
        sum += GetHeight(data, x - 1, z - 1);
        sum += GetHeight(data, x, z - 1);
        sum += GetHeight(data, x + 1, z - 1);

        sum += GetHeight(data, x - 1, z);
        sum += GetHeight(data, x, z);
        sum += GetHeight(data, x + 1, z);

        sum += GetHeight(data, x - 1, z + 1);
        sum += GetHeight(data, x, z + 1);
        sum += GetHeight(data, x + 1, z + 1);

        tempMap[z * resX + x] = sum / 9.0f;
      }
    }
    data.heightMap = tempMap;
  }
}

/**
 * @brief 小さな孤立地形を周囲へなじませます。
 */
void TerrainGenerator::ApplyMaterialCleanup(TerrainData &data) {
  const int resX = data.config.resolutionX;
  const int resZ = data.config.resolutionZ;
  if (resX < 3 || resZ < 3 || data.materialMap.empty()) {
    return;
  }

  for (int pass = 0; pass < 3; ++pass) {
    std::vector<uint8_t> cleaned = data.materialMap;
    for (int z = 1; z < resZ - 1; ++z) {
      for (int x = 1; x < resX - 1; ++x) {
        const int idx = z * resX + x;
        const uint8_t mat = data.materialMap[idx];
        if (mat == 3) {
          continue;
        }

        int counts[8] = {};
        for (int dz = -1; dz <= 1; ++dz) {
          for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dz == 0) {
              continue;
            }
            uint8_t other = data.materialMap[(z + dz) * resX + (x + dx)];
            if (other < 8) {
              ++counts[other];
            }
          }
        }

        int majorityCount = 0;
        uint8_t majority = mat;
        for (uint8_t candidate = 0; candidate < 8; ++candidate) {
          if (counts[candidate] > majorityCount) {
            majorityCount = counts[candidate];
            majority = candidate;
          }
        }

        int sameCount = 0;
        if (mat < 8) {
          sameCount = counts[mat];
        }
        const bool isolated = sameCount <= 1 && majorityCount >= 4;
        const bool narrowHazard = mat >= 2 && mat != 3 && sameCount <= 2 &&
                                  majorityCount >= 5;
        if (isolated || narrowHazard) {
          cleaned[idx] = majority;
        }
      }
    }
    data.materialMap = std::move(cleaned);
  }
}

void TerrainGenerator::GenerateVisualMaterialColors(TerrainData &data) {
  const int resX = data.config.resolutionX;
  const int resZ = data.config.resolutionZ;
  data.visualMaterialColors.resize(data.materialMap.size());
  if (resX <= 0 || resZ <= 0 || data.materialMap.empty()) {
    return;
  }

  constexpr int radius = 3;
  constexpr float sigma = 1.35f;
  constexpr float denominator = 2.0f * sigma * sigma;

  for (int z = 0; z < resZ; ++z) {
    for (int x = 0; x < resX; ++x) {
      XMFLOAT3 blended = {0.0f, 0.0f, 0.0f};
      float totalWeight = 0.0f;
      for (int dz = -radius; dz <= radius; ++dz) {
        int sampleZ = std::clamp(z + dz, 0, resZ - 1);
        for (int dx = -radius; dx <= radius; ++dx) {
          int sampleX = std::clamp(x + dx, 0, resX - 1);
          float distanceSq = static_cast<float>(dx * dx + dz * dz);
          float weight = std::exp(-distanceSq / denominator);
          XMFLOAT3 color = TerrainMaterialColor(
              data.materialMap[sampleZ * resX + sampleX]);
          blended.x += color.x * weight;
          blended.y += color.y * weight;
          blended.z += color.z * weight;
          totalWeight += weight;
        }
      }

      blended.x /= totalWeight;
      blended.y /= totalWeight;
      blended.z /= totalWeight;
      data.visualMaterialColors[z * resX + x] = blended;
    }
  }
}

/**
 * @brief 地形の法線を計算します、（
 */
void TerrainGenerator::CalculateNormals(TerrainData &data) {
  int resX = data.config.resolutionX;
  int resZ = data.config.resolutionZ;
  float cellW = data.config.worldWidth / (resX - 1);
  float cellD = data.config.worldDepth / (resZ - 1);

  data.normals.resize(data.heightMap.size());

  for (int z = 0; z < resZ; ++z) {
    for (int x = 0; x < resX; ++x) {
      // 勾配計算用の隣接セルインデックス指定

      float hL = GetHeight(data, x, z);
      if (x > 0) hL = GetHeight(data, x - 1, z);
      
      float hR = GetHeight(data, x, z);
      if (x < resX - 1) hR = GetHeight(data, x + 1, z);
      
      float hD = GetHeight(data, x, z);
      if (z > 0) hD = GetHeight(data, x, z - 1);
      
      float hU = GetHeight(data, x, z);
      if (z < resZ - 1) hU = GetHeight(data, x, z + 1);

      // 接線ベクトル
      XMVECTOR tangentX = XMVectorSet(2.0f * cellW, hR - hL, 0.0f, 0.0f);
      XMVECTOR tangentZ = XMVectorSet(0.0f, hU - hD, -2.0f * cellD, 0.0f);

      // 法線 = Cross(X, Z)  (左手座標系 Y-up)。順序を誤ると下向きになる。
      XMVECTOR normal = XMVector3Cross(tangentX, tangentZ);
      normal = XMVector3Normalize(normal);

      XMStoreFloat3(&data.normals[z * resX + x], normal);
    }
  }
}

/**
 * @brief 地形メチ（��ュを生成します、（
 */

} // namespace game::systems
