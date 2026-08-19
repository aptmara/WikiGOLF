#include "TerrainGenerator.h"
#include "../../core/Logger.h"
#include "../../graphics/TangentGenerator.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace game::systems {

using namespace DirectX;

// ヘルパー：線形補間
static float Lerp(float a, float b, float t) { return a + (b - a) * t; }

static float CatmullRom(float p0, float p1, float p2, float p3, float t) {
  float t2 = t * t;
  float t3 = t2 * t;
  return 0.5f * ((2.0f * p1) + (-p0 + p2) * t +
                 (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                 (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

// ヘルパー：スムースステップ
static float SmoothStep(float edge0, float edge1, float x) {
  x = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
  return x * x * (3 - 2 * x);
}

static uint32_t HashCoordinates(int x, int z, uint32_t seed) {
  uint32_t h = seed ^ (static_cast<uint32_t>(x) * 0x9e3779b9u) ^
               (static_cast<uint32_t>(z) * 0x85ebca6bu);
  h ^= h >> 16;
  h *= 0x7feb352du;
  h ^= h >> 15;
  h *= 0x846ca68bu;
  h ^= h >> 16;
  return h;
}

static float HashNoise(int x, int z, uint32_t seed) {
  return static_cast<float>(HashCoordinates(x, z, seed) & 0x00ffffffu) /
         static_cast<float>(0x00ffffffu);
}

static float ValueNoise(float x, float z, uint32_t seed) {
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

static float FractalNoise(float x, float z, uint32_t seed) {
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

static XMFLOAT3 TerrainMaterialColor(uint8_t material) {
  switch (material) {
  case 0:
    return {0.35f, 0.55f, 0.25f};
  case 1:
    return {0.25f, 0.45f, 0.20f};
  case 2:
    return {0.90f, 0.85f, 0.70f};
  case 3:
    return {0.40f, 0.75f, 0.30f};
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

static float DistanceToSegment(float px, float pz, float ax, float az, float bx,
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

static uint8_t HazardMaterialForBiome(int biome, float roll) {
  switch (biome) {
  case 1:
    return roll < 0.65f ? 2 : 7; // Bunker / Stone
  case 2:
    return roll < 0.68f ? 4 : 5; // Ice / Water(OB)
  case 3:
    return roll < 0.58f ? 7 : 6; // Stone / Lava(OB)
  default:
    return 2; // Bunker
  }
}

/**
 * @brief 地形チ（�（タを生成します、（
 */
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

      const float fairwayHalfWidth = (wz > 25.0f) ? 14.0f : 10.0f;
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
          data.materialMap[idx] = (pos.y > 35.0f) ? 3 : 0;
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
  float doglegDir = dist(rng) < 0.5f ? -1.0f : 1.0f;
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
    switch (courseStyle) {
    case 1:
      targetU = Lerp(startU, doglegTarget, SmoothStep(0.25f, 0.75f, t));
      targetU += (dist(rng) - 0.5f) * 0.06f;
      break;
    case 2:
      targetU = startU + std::sin(t * 6.28318f) * (0.16f + dist(rng) * 0.05f);
      break;
    case 3:
      targetU += ((i % 2) == 0 ? 0.12f : -0.12f) * doglegDir;
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
        size_t previous = seg > 0 ? seg - 1 : seg;
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
      switch (courseStyle) {
      case 1:
        widthPulse = 0.80f + 0.35f * SmoothStep(0.35f, 0.65f, v);
        break;
      case 2:
        widthPulse = 0.85f + 0.25f * std::sin(v * 18.84954f);
        break;
      case 3:
        widthPulse = 0.70f + 0.45f * (std::sin(v * 25.13272f) > 0.0f ? 1.0f : 0.0f);
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
          data.materialMap[idx] = detailNoise > 0.48f ? 7 : 2;
        } else {
          data.materialMap[idx] = 1;
        }
      } else if (corridor < 0.45f) {
        data.materialMap[idx] = 0;
      } else if (shortcut) {
        data.materialMap[idx] = (biome == 2 && materialNoise > 0.3f) ? 4 : 0;
      } else if (corridor < 0.88f) {
        data.materialMap[idx] = materialNoise < 0.28f ? 0 : 1;
      } else if (corridor < 1.45f) {
        data.materialMap[idx] = 1;
      } else if (biome == 1) {
        float sandEdge = 1.28f + materialNoise * 0.16f;
        if (corridor > sandEdge) {
          data.materialMap[idx] = detailNoise > 0.48f ? 7 : 2;
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
void TerrainGenerator::CreatePlatforms(
    TerrainData &data, const std::vector<DirectX::XMFLOAT2> &holePositions) {
  const int resX = data.config.resolutionX;
  const int resZ = data.config.resolutionZ;
  const float worldW = data.config.worldWidth;
  const float worldD = data.config.worldDepth;

  struct BunkerPatch {
    float centerX;
    float centerZ;
    float radiusX;
    float radiusZ;
    float cosAngle;
    float sinAngle;
  };

  for (const auto &pos : holePositions) {
    float u = pos.x / worldW + 0.5f;
    float v = 0.5f - pos.y / worldD;
    int cx = std::clamp(static_cast<int>(u * (resX - 1)), 0, resX - 1);
    int cz = std::clamp(static_cast<int>(v * (resZ - 1)), 0, resZ - 1);

    std::mt19937 tempRng(cx * 73856093u ^ cz * 19349663u ^
                         data.config.biome * 83492791u);
    std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
    const uint32_t patchSeed = tempRng();

    const int radius =
        std::max(4, resX / 12 + static_cast<int>(dist01(tempRng) * 4.0f));
    const float targetHeight = GetHeight(data, cx, cz) + 0.05f;
    const float bowlDepth = 0.2f;
    const float greenRadiusX = radius * (0.88f + dist01(tempRng) * 0.22f);
    const float greenRadiusZ = radius * (0.78f + dist01(tempRng) * 0.28f);
    const float greenAngle = (dist01(tempRng) - 0.5f) * 0.8f;
    const float greenCos = std::cos(greenAngle);
    const float greenSin = std::sin(greenAngle);

    int bunkerCount = 1 + static_cast<int>(dist01(tempRng) * 2.0f);
    if (data.config.biome == 2) {
      bunkerCount = std::max(1, bunkerCount - 1);
    }

    std::vector<BunkerPatch> bunkers;
    bunkers.reserve(bunkerCount);
    const float bunkerStartAngle = dist01(tempRng) * 6.28318f;
    for (int b = 0; b < bunkerCount; ++b) {
      const float angle = bunkerStartAngle +
                          (6.28318f / static_cast<float>(bunkerCount)) * b +
                          (dist01(tempRng) - 0.5f) * 0.7f;
      const float distance = radius * (1.45f + dist01(tempRng) * 0.70f);
      const float patchAngle = angle + (dist01(tempRng) - 0.5f) * 1.0f;
      BunkerPatch patch;
      patch.centerX = std::cos(angle) * distance;
      patch.centerZ = std::sin(angle) * distance;
      patch.radiusX = radius * (0.32f + dist01(tempRng) * 0.24f);
      patch.radiusZ = radius * (0.20f + dist01(tempRng) * 0.18f);
      patch.cosAngle = std::cos(patchAngle);
      patch.sinAngle = std::sin(patchAngle);
      bunkers.push_back(patch);
    }

    for (int z = cz - radius * 3; z <= cz + radius * 3; ++z) {
      for (int x = cx - radius * 3; x <= cx + radius * 3; ++x) {
        if (x < 0 || x >= resX || z < 0 || z >= resZ) {
          continue;
        }

        const float dx = static_cast<float>(x - cx);
        const float dz = static_cast<float>(z - cz);
        const float cupDistance = std::sqrt(dx * dx + dz * dz);
        const int idx = z * resX + x;
        const float greenLocalX = greenCos * dx + greenSin * dz;
        const float greenLocalZ = -greenSin * dx + greenCos * dz;
        const float greenDistance =
            std::sqrt((greenLocalX * greenLocalX) /
                          (greenRadiusX * greenRadiusX) +
                      (greenLocalZ * greenLocalZ) /
                          (greenRadiusZ * greenRadiusZ));
        const float boundaryNoise =
            FractalNoise(static_cast<float>(x) * 0.16f,
                         static_cast<float>(z) * 0.16f, patchSeed) *
            0.07f;

        if (greenDistance < 1.0f + boundaryNoise) {
          data.materialMap[idx] = 3;
          if (cupDistance < 3.0f) {
            SetHeight(data, x, z, targetHeight - bowlDepth);
          } else {
            SetHeight(data, x, z, targetHeight);
          }
          continue;
        }

        if (greenDistance < 1.18f + boundaryNoise) {
          if (data.materialMap[idx] == 0 || data.materialMap[idx] == 1) {
            data.materialMap[idx] = 0;
          }
          float t = SmoothStep(1.0f, 1.18f, greenDistance - boundaryNoise);
          const float currentH = GetHeight(data, x, z);
          SetHeight(data, x, z, Lerp(targetHeight, currentH, t));
          continue;
        }

        if (data.materialMap[idx] == 3) {
          continue;
        }

        for (const auto &bunker : bunkers) {
          const float bx = dx - bunker.centerX;
          const float bz = dz - bunker.centerZ;
          const float localX = bunker.cosAngle * bx + bunker.sinAngle * bz;
          const float localZ = -bunker.sinAngle * bx + bunker.cosAngle * bz;
          float patchDistance =
              std::sqrt((localX * localX) /
                            (bunker.radiusX * bunker.radiusX) +
                        (localZ * localZ) /
                            (bunker.radiusZ * bunker.radiusZ));
          patchDistance -= boundaryNoise * 0.8f;
          if (patchDistance < 1.0f) {
            data.materialMap[idx] = 2;
            float depression = SmoothStep(1.0f, 0.0f, patchDistance);
            SetHeight(data, x, z,
                      GetHeight(data, x, z) - 0.18f * depression);
            break;
          }
        }
      }
    }
  }
}

/**
 * @brief ハイト�（チ（�（にスムージング処理��適用します、（
 */
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

        const int sameCount = mat < 8 ? counts[mat] : 0;
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
void TerrainGenerator::GenerateMesh(
    TerrainData &data, const std::vector<DirectX::XMFLOAT2> &holePositions) {
  int resX = data.config.resolutionX;
  int resZ = data.config.resolutionZ;
  float width = data.config.worldWidth;
  float depth = data.config.worldDepth;

  std::vector<graphics::Vertex> vertices;
  std::vector<uint32_t> indices;

  vertices.reserve(resX * resZ);
  indices.reserve((resX - 1) * (resZ - 1) * 6);

  // 頂点生成
  for (int z = 0; z < resZ; ++z) {
    for (int x = 0; x < resX; ++x) {
      float u = (float)x / (resX - 1);
      float v = (float)z / (resZ - 1); // 1.0 - ... にするかはUV座標系による

      float px = (u - 0.5f) * width;
      float pz =
          (0.5f - v) * depth; // Z軸反転注意。ここでは手前が-ZとするならこれでOK
      float py = GetHeight(data, x, z);

      graphics::Vertex vert;
      vert.position = {px, py, pz};
      vert.normal = data.normals[z * resX + x];
      vert.texCoord = {u, v};

      // デフォルト色 (マテリアルマップに基づく)
      int idx = z * resX + x;
      uint8_t mat = data.materialMap[idx];
      float matAlpha = (static_cast<float>(mat) + 0.5f) / 255.0f;
      const XMFLOAT3 &visualColor = data.visualMaterialColors[idx];
      vert.color = {visualColor.x, visualColor.y, visualColor.z, matAlpha};

      vertices.push_back(vert);
    }
  }

  // インデックス生成 (Triangle List)
  for (int z = 0; z < resZ - 1; ++z) {
    for (int x = 0; x < resX - 1; ++x) {
      // 三角形インデックスの設定（0-1-2および2-1-3）
      // Tri 1: 0-1-2
      // Tri 2: 2-1-3

      uint32_t i0 = z * resX + x;
      uint32_t i1 = z * resX + (x + 1);
      uint32_t i2 = (z + 1) * resX + x;
      uint32_t i3 = (z + 1) * resX + (x + 1);

      // Tri 1
      indices.push_back(i0);
      indices.push_back(i1);
      indices.push_back(i2);

      // Tri 2
      indices.push_back(i2);
      indices.push_back(i1);
      indices.push_back(i3);
    }
  }

  graphics::ComputeTangents(vertices, indices);

  // データ格納
  data.vertices = std::move(vertices);
  data.indices = std::move(indices);
}

/**
 * @brief 指定��した格子座標�（地形高さを取得します、（
 */
float TerrainGenerator::GetHeight(const TerrainData &data, int x, int z) {
  if (x < 0 || x >= data.config.resolutionX || z < 0 ||
      z >= data.config.resolutionZ)
    return 0.0f;
  return data.heightMap[z * data.config.resolutionX + x];
}

/**
 * @brief 指定��した格子座標의地形高さを設定します、（
 */
void TerrainGenerator::SetHeight(TerrainData &data, int x, int z, float h) {
  if (x < 0 || x >= data.config.resolutionX || z < 0 ||
      z >= data.config.resolutionZ)
    return;
  data.heightMap[z * data.config.resolutionX + x] = h;
}

} // namespace game::systems
