/**
 * @file TerrainGeneratorNoise.cpp
 * @brief TerrainGeneratorNoise の実装
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



using namespace DirectX;

// ヘルパー：線形補間
float Lerp(float a, float b, float t) { return a + (b - a) * t; }

float CatmullRom(float p0, float p1, float p2, float p3, float t) {
  float t2 = t * t;
  float t3 = t2 * t;
  return 0.5f * ((2.0f * p1) + (-p0 + p2) * t +
                 (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                 (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

// ヘルパー：スムースステップ
float SmoothStep(float edge0, float edge1, float x) {
  x = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
  return x * x * (3 - 2 * x);
}

uint32_t HashCoordinates(int x, int z, uint32_t seed) {
  uint32_t h = seed ^ (static_cast<uint32_t>(x) * 0x9e3779b9u) ^
               (static_cast<uint32_t>(z) * 0x85ebca6bu);
  h ^= h >> 16;
  h *= 0x7feb352du;
  h ^= h >> 15;
  h *= 0x846ca68bu;
  h ^= h >> 16;
  return h;
}

float HashNoise(int x, int z, uint32_t seed) {
  return static_cast<float>(HashCoordinates(x, z, seed) & 0x00ffffffu) /
         static_cast<float>(0x00ffffffu);
}

float ValueNoise(float x, float z, uint32_t seed) {
  int x0 = static_cast<int>(std::floor(x));
  int z0 = static_cast<int>(std::floor(z));
  int x1 = x0 + 1;
  int z1 = z0 + 1;
  float tx = SmoothStep(0.0f, 1.0f, x - static_cast<float>(x0));
  float tz = SmoothStep(0.0f, 1.0f, z - static_cast<float>(z0));

  float n0 = Lerp(HashNoise(x0, z0, seed), HashNoise(x1, z0, seed), tx);
  float n1 = Lerp(HashNoise(x0, z1, seed), HashNoise(x1, z1, seed), tx);
  return Lerp(n0, n1, tz) * 2.0f - 1.0f;
}

float FractalNoise(float x, float z, uint32_t seed) {
  float total = 0.0f;
  float amplitude = 0.5f;
  float amplitudeSum = 0.0f;
  for (int octave = 0; octave < 4; ++octave) {
    total += ValueNoise(x, z, seed + static_cast<uint32_t>(octave) * 1013u) *
             amplitude;
    amplitudeSum += amplitude;
    x *= 2.03f;
    z *= 2.03f;
    amplitude *= 0.5f;
  }
  return total / amplitudeSum;
}

XMFLOAT3 TerrainMaterialColor(uint8_t material) {
  switch (material) {
  case 0:
    return {0.25f, 0.43f, 0.16f};
  case 1:
    return {0.18f, 0.32f, 0.11f};
  case 2:
    return {0.90f, 0.85f, 0.70f};
  case 3:
    return {0.30f, 0.52f, 0.19f};
  case 4:
    return {0.70f, 0.88f, 0.98f};
  case 5:
    return {0.20f, 0.45f, 0.85f};
  case 6:
    return {0.95f, 0.35f, 0.12f};
  case 7:
    return {0.50f, 0.48f, 0.52f};
  default:
    return {1.0f, 1.0f, 1.0f};
  }
}

float DistanceToSegment(float px, float pz, float ax, float az, float bx,
                               float bz) {
  float vx = bx - ax;
  float vz = bz - az;
  float wx = px - ax;
  float wz = pz - az;
  float lenSq = vx * vx + vz * vz;
  if (lenSq <= 0.000001f) {
    float dx = px - ax;
    float dz = pz - az;
    return std::sqrt(dx * dx + dz * dz);
  }

  float t = std::clamp((wx * vx + wz * vz) / lenSq, 0.0f, 1.0f);
  float cx = ax + vx * t;
  float cz = az + vz * t;
  float dx = px - cx;
  float dz = pz - cz;
  return std::sqrt(dx * dx + dz * dz);
}

uint8_t HazardMaterialForBiome(int biome, float roll) {
  switch (biome) {
  case 1:
    if (roll < 0.65f) {
      return 2;
    }
    return 7; // Bunker / Stone
  case 2:
    if (roll < 0.68f) {
      return 4;
    }
    return 5; // Ice / Water(OB)
  case 3:
    if (roll < 0.58f) {
      return 7;
    }
    return 6; // Stone / Lava(OB)
  default:
    return 2; // Bunker
  }
}

/**
 * @brief 地形チ（�（タを生成します、（
*/
void TerrainGenerator::GenerateBaseHeightMap(TerrainData &data,
                                             const std::string &text) {
  std::seed_seq seed(text.begin(), text.end());
  std::mt19937 rng(seed);
  std::uniform_real_distribution<float> dist(0.0f, 1.0f);

  int resX = data.config.resolutionX;
  int resZ = data.config.resolutionZ;
  int biome = data.config.biome;
  float hScale = data.config.heightScale;
  uint32_t terrainSeed = rng();

  // 全体をデフォルトで初期化
  for (int i = 0; i < resX * resZ; ++i) {
    data.materialMap[i] = 1;
    data.heightMap[i] = data.config.baseHeight;
  }

  float startU = 0.5f;
  float startV = 0.85f;
  int courseStyle = static_cast<int>(dist(rng) * 5.0f) % 5;
  float fairwayWidthBase = resX * (0.10f + dist(rng) * 0.06f);

  // ルート生成
  std::vector<std::pair<float, float>> routePoints;
  routePoints.push_back({startU, startV});

  int numSegments = 5 + (int)(dist(rng) * 4);
  if (courseStyle == 1 || courseStyle == 4) {
    ++numSegments;
  }
  float currentU = startU;
  float currentV = startV;
  float doglegDir = 1.0f;
  if (dist(rng) < 0.5f) {
    doglegDir = -1.0f;
  }
  float doglegTarget = std::clamp(startU + doglegDir * (0.22f + dist(rng) * 0.18f),
                                  0.18f, 0.82f);

  std::vector<float> segmentHeights;
  segmentHeights.push_back(0.0f);

  for (int i = 1; i <= numSegments; ++i) {
    float t = (float)i / numSegments;
    float targetV = startV - t * 0.7f;

    float swerveAmount = 0.15f;
    if (biome == 2)
      swerveAmount = 0.08f;
    if (biome == 3)
      swerveAmount = 0.2f;

    float targetU = currentU + (dist(rng) - 0.5f) * 2.0f * swerveAmount;
    float alternatingOffset = -0.12f;
    switch (courseStyle) {
    case 1:
      targetU = Lerp(startU, doglegTarget, SmoothStep(0.25f, 0.75f, t));
      targetU += (dist(rng) - 0.5f) * 0.06f;
      break;
    case 2:
      targetU = startU + std::sin(t * 6.28318f) * (0.16f + dist(rng) * 0.05f);
      break;
    case 3:
      if ((i % 2) == 0) {
        alternatingOffset = 0.12f;
      }
      targetU += alternatingOffset * doglegDir;
      break;
    case 4:
      targetU = Lerp(startU, doglegTarget, t) +
                std::sin(t * 12.56636f) * 0.06f;
      break;
    default:
      break;
    }
    currentU = std::clamp(targetU, 0.15f, 0.85f);
    currentV = targetV;
    routePoints.push_back({currentU, currentV});

    float heightChange = (dist(rng) - 0.5f) * 1.5f * hScale;
    float prevHeight = segmentHeights.back();
    segmentHeights.push_back(
        std::clamp(prevHeight + heightChange, -1.0f * hScale, 2.0f * hScale));
  }

  // マウンド生成
  struct Mound {
    float u, v, radius, height;
  };
  std::vector<Mound> mounds;
  int numMounds = 3 + (int)(dist(rng) * 4);
  if (biome == 2)
    numMounds = 1;
  if (biome == 3)
    numMounds = 8;

  for (int i = 0; i < numMounds; ++i) {
    Mound m;
    m.u = 0.1f + dist(rng) * 0.8f;
    m.v = 0.1f + dist(rng) * 0.8f;
    m.radius = 0.05f + dist(rng) * 0.1f;
    m.height = 0.3f + dist(rng) * 0.7f;
    if (biome == 3)
      m.height *= 1.5f;
    mounds.push_back(m);
  }

  // 高さ計算
  for (int z = 0; z < resZ; ++z) {
    float v = (float)z / (resZ - 1);
    float routeU = startU;
    float routeHeight = 0.0f;

    for (size_t seg = 0; seg < routePoints.size() - 1; ++seg) {
      float v0 = routePoints[seg].second;
      float v1 = routePoints[seg + 1].second;
      if (v <= v0 && v >= v1) {
        float t = (v0 - v) / (v0 - v1 + 0.0001f);
        size_t previous = seg;
        if (seg > 0) {
          previous = seg - 1;
        }
        size_t next = seg + 1;
        size_t following =
            std::min(seg + 2, routePoints.size() - static_cast<size_t>(1));
        routeU = std::clamp(
            CatmullRom(routePoints[previous].first, routePoints[seg].first,
                       routePoints[next].first, routePoints[following].first,
                       t),
            0.12f, 0.88f);
        routeHeight = std::clamp(
            CatmullRom(segmentHeights[previous], segmentHeights[seg],
                       segmentHeights[next], segmentHeights[following], t),
            -hScale, 2.0f * hScale);
        break;
      }
    }

    for (int x = 0; x < resX; ++x) {
      float u = (float)x / (resX - 1);
      int idx = z * resX + x;
      float distFromRoute = std::abs(u - routeU);
      float distPixels = distFromRoute * resX;
      float fairwayWidth =
          fairwayWidthBase * (0.8f + 0.4f * std::sin(v * 6.28f * 2.0f));
      float widthPulse = 1.0f;
      float pulseSign = -1.0f;
      switch (courseStyle) {
      case 1:
        widthPulse = 0.80f + 0.35f * SmoothStep(0.35f, 0.65f, v);
        break;
      case 2:
        widthPulse = 0.85f + 0.25f * std::sin(v * 18.84954f);
        break;
      case 3:
        if (std::sin(v * 25.13272f) > 0.0f) {
          pulseSign = 1.0f;
        }
        widthPulse = 0.70f + 0.45f * pulseSign;
        break;
      case 4:
        widthPulse = 0.65f + 0.55f * std::pow(std::sin(v * 15.70795f), 2.0f);
        break;
      default:
        break;
      }
      fairwayWidth *= std::clamp(widthPulse, 0.55f, 1.35f);

      float h = data.config.baseHeight;

      // 進行方向の起伏
      float routeInfluence =
          1.0f - std::clamp(distFromRoute * 4.0f, 0.0f, 1.0f);
      h += routeHeight * routeInfluence;

      // サイドスロープ
      h += std::min(distFromRoute * 0.3f, 0.2f) * hScale;

      // マウンド
      for (const auto &m : mounds) {
        float du = u - m.u;
        float dv = v - m.v;
        float distToMound = std::sqrt(du * du + dv * dv);
        if (distToMound < m.radius) {
          float t = 1.0f - (distToMound / m.radius);
          h += t * t * m.height * hScale;
        }
      }

      // バイオーム別パターン
      switch (biome) {
      case 0:
        h += FractalNoise(u * 3.2f, v * 3.2f, terrainSeed ^ 0x18a3u) *
             0.10f * hScale;
        break;
      case 1:
        h += std::sin(u * 9.2f + v * 2.4f) * 0.14f * hScale +
             FractalNoise(u * 2.6f, v * 2.2f, terrainSeed ^ 0x2bd1u) *
                 0.12f * hScale;
        break;
      case 2:
        h += FractalNoise(u * 2.0f, v * 2.0f, terrainSeed ^ 0x3ce7u) *
             0.035f * hScale;
        break;
      case 3:
        h += FractalNoise(u * 5.0f, v * 5.0f, terrainSeed ^ 0x4df9u) *
                 0.30f * hScale +
             FractalNoise(u * 11.0f, v * 11.0f, terrainSeed ^ 0x58cbu) *
                 0.07f * hScale;
        break;
      }

      // ティーイングエリア
      if (v > 0.8f && distFromRoute < 0.1f) {
        h += SmoothStep(0.8f, 0.9f, v) * 0.2f * hScale;
      }

      // 外周壁
      float dx = u - 0.5f;
      float dz = v - 0.5f;
      float distFromCenter = std::sqrt(dx * dx + dz * dz) * 2.0f;
      if (distFromCenter > 0.85f) {
        h += SmoothStep(0.85f, 1.0f, distFromCenter) * 4.0f;
      }

      // 微小ノイズ
      h += FractalNoise(u * 18.0f, v * 18.0f, terrainSeed ^ 0x69e5u) *
           0.012f * hScale;

      SetHeight(data, x, z, h);

      // マテリアル判定
      float wallFactor = 0.0f;
      if (distFromCenter > 0.85f) {
        wallFactor = SmoothStep(0.85f, 1.0f, distFromCenter);
      }
      float borderNoise =
          FractalNoise(u * 6.0f, v * 6.0f, terrainSeed ^ 0x7af3u) * 0.16f;
      float corridor = distPixels / std::max(fairwayWidth, 0.001f) + borderNoise;
      float materialNoise =
          FractalNoise(u * 7.0f, v * 7.0f, terrainSeed ^ 0x81bdu);
      float detailNoise =
          FractalNoise(u * 3.5f, v * 3.5f, terrainSeed ^ 0x92c7u);
      bool shortcut = false;
      if (courseStyle >= 2 && routePoints.size() > 2) {
        auto first = routePoints.front();
        auto last = routePoints.back();
        float shortcutDist =
            DistanceToSegment(u, v, first.first, first.second, last.first, last.second) *
            resX;
        float shortcutWidth = fairwayWidthBase * (0.22f + 0.05f * courseStyle);
        shortcut = shortcutDist < shortcutWidth && v > 0.18f && v < 0.78f;
      }

      if (wallFactor > 0.3f) {
        if (biome == 1) {
          data.materialMap[idx] = 2;
          if (detailNoise > 0.48f) {
            data.materialMap[idx] = 7;
          }
        } else {
          data.materialMap[idx] = 1;
        }
      } else if (corridor < 0.45f) {
        data.materialMap[idx] = 0;
      } else if (shortcut) {
        data.materialMap[idx] = 0;
        if (biome == 2 && materialNoise > 0.3f) {
          data.materialMap[idx] = 4;
        }
      } else if (corridor < 0.88f) {
        data.materialMap[idx] = 1;
        if (materialNoise < 0.28f) {
          data.materialMap[idx] = 0;
        }
      } else if (corridor < 1.45f) {
        data.materialMap[idx] = 1;
      } else if (biome == 1) {
        float sandEdge = 1.28f + materialNoise * 0.16f;
        if (corridor > sandEdge) {
          data.materialMap[idx] = 2;
          if (detailNoise > 0.48f) {
            data.materialMap[idx] = 7;
          }
        } else {
          data.materialMap[idx] = 1;
        }
      } else {
        float hazardChance =
            std::clamp((corridor - 1.45f) * 0.28f, 0.0f, 0.62f);
        if (courseStyle == 4) {
          hazardChance += 0.08f;
        }
        float hazardField = detailNoise * 0.5f + 0.5f;
        if (hazardField > 1.0f - hazardChance) {
          float hazardType = FractalNoise(u * 2.0f, v * 2.0f,
                                          terrainSeed ^ 0xa391u) *
                                 0.5f +
                             0.5f;
          data.materialMap[idx] = HazardMaterialForBiome(biome, hazardType);
        } else {
          data.materialMap[idx] = 1;
        }
      }
    }
  }
}

/**
 * @brief ホ�（ル周辺に平らなプラチ（��フォームを作�（します、（
*/

} // namespace game::systems
