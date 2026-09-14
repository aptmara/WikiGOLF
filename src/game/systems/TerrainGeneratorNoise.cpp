/**
 * @file TerrainGeneratorNoise.cpp
 * @brief TerrainGeneratorNoise の実装
*/

#include "TerrainGeneratorInternals.h"
#include "TerrainMaterialAssets.h"
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
  return TerrainMaterialMapColor(material);
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
void TerrainGenerator::GenerateBaseHeightMap(
    TerrainData &data, const std::string &text,
    const std::vector<DirectX::XMFLOAT2> &holePositions) {
  std::seed_seq seed(text.begin(), text.end());
  std::mt19937 rng(seed);
  std::uniform_real_distribution<float> dist(0.0f, 1.0f);

  int resX = data.config.resolutionX;
  int resZ = data.config.resolutionZ;
  int biome = data.config.biome;
  float hScale = data.config.heightScale;
  uint32_t terrainSeed = rng();

  // 地表の模様の大きさ・向き・伸び方を記事ごとに変え、どの記事も同じ模様に見えないようにする。
  // 乱数列を変えないよう、専用の乱数から取る。
  std::mt19937 patternRng(terrainSeed ^ 0x3b9fu);
  std::uniform_real_distribution<float> patternDist(0.0f, 1.0f);
  const float patternScale = 0.55f + patternDist(patternRng) * 1.25f;
  const float patternAngle = patternDist(patternRng) * 3.14159f;
  const float patternStretch = 1.0f + patternDist(patternRng) * 1.8f;
  const float patternCos = std::cos(patternAngle);
  const float patternSin = std::sin(patternAngle);
  auto patternNoise = [&](float x, float z, float sizeX, float sizeZ,
                          uint32_t salt) {
    const float u = (patternCos * x - patternSin * z) / (patternScale * patternStretch);
    const float w = (patternSin * x + patternCos * z) / patternScale;
    return FractalNoise(u / sizeX, w / sizeZ, terrainSeed ^ salt);
  };

  // 全体をデフォルトで初期化
  for (int i = 0; i < resX * resZ; ++i) {
    data.materialMap[i] = 1;
    data.heightMap[i] = data.config.baseHeight;
  }

  float startU = 0.5f;
  float startV = 0.85f;
  int courseStyle = static_cast<int>(dist(rng) * 5.0f) % 5;
  // フェアウェイの幅はコース幅に対する割合で持つ（生成範囲の格子数に依存させない）。
  float fairwayWidthBase = 0.10f + dist(rng) * 0.06f;

  // ルート生成
  std::vector<std::pair<float, float>> routePoints;
  routePoints.push_back({startU, startV});

  // コースを基準にした寸法。延長地形では生成範囲がコースより広い。
  const CourseFrame frame = CourseFrameOf(data.config);
  const float worldW = std::max(data.config.worldWidth, 1.0f);
  const float worldD = std::max(data.config.worldDepth, 1.0f);
  const float courseW = std::max(frame.width, 1.0f);
  const float courseD = std::max(frame.depth, 1.0f);
  // 起伏の大きさはメートル基準にし、記事の長さで地形が引き伸ばされないようにする。
  // 長いフィールドでは区間を増やして、およそ 60m ごとに上り下りを付ける。
  int numSegments = 5 + (int)(dist(rng) * 4);
  numSegments = std::max(numSegments,
                         static_cast<int>(courseD * 0.7f / 60.0f));
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
  }


  // 外周の土手。カップの縁は平らにする必要があり、斜面にカップが並ぶと
  // 隣同士で段差になるため、土手はホールが存在する範囲の外側にだけ作る。
  float holeExtentX = 0.0f;
  float holeExtentZ = 0.0f;
  for (const auto &pos : holePositions) {
    holeExtentX = std::max(holeExtentX, std::abs(pos.x));
    holeExtentZ = std::max(holeExtentZ, std::abs(pos.y));
  }
  const float defaultBandX = std::clamp(courseW * 0.14f, 8.0f, 14.0f);
  const float defaultBandZ = std::clamp(courseD * 0.14f, 8.0f, 14.0f);
  float edgeBandX = defaultBandX;
  float edgeBandZ = defaultBandZ;
  if (!holePositions.empty()) {
    edgeBandX = std::clamp(courseW * 0.5f - holeExtentX - 4.0f, 2.0f, defaultBandX);
    edgeBandZ = std::clamp(courseD * 0.5f - holeExtentZ - 4.0f, 2.0f, defaultBandZ);
  }
  // 帯が狭いときは土手を低くし、傾斜が 25 度程度を超えないようにする。
  const float bankHeight = std::min({2.2f, edgeBandX * 0.3f, edgeBandZ * 0.3f});

  // 高さ計算
  for (int z = 0; z < resZ; ++z) {
    // v はコースの奥端を 0、手前端を 1 とする座標（コースの外では範囲外になる）。
    const float wz = static_cast<float>(z) / (resZ - 1) * worldD - frame.marginZ;
    const float v = wz / courseD;
    float routeU = startU;
    // ルート終端より先は終端の値を保ち、そこでフェアウェイが飛ばないようにする。
    if (v < routePoints.back().second) {
      routeU = std::clamp(routePoints.back().first, 0.12f, 0.88f);
    }

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
        break;
      }
    }

    for (int x = 0; x < resX; ++x) {
      int idx = z * resX + x;
      // 模様や幅の変化はコースの左奥を原点にしたメートル座標で計算する。
      // 延長地形でもコースと同じ座標になるので、模様がそのまま外へ続く。
      const float wx = static_cast<float>(x) / (resX - 1) * worldW - frame.marginX;
      const float u = wx / courseW;
      float distFromRoute = std::abs(u - routeU);
      float distPixels = distFromRoute;
      float fairwayWidth =
          fairwayWidthBase * (0.8f + 0.4f * std::sin(wz * 0.105f));
      float widthPulse = 1.0f;
      float pulseSign = -1.0f;
      switch (courseStyle) {
      case 1:
        widthPulse = 0.80f + 0.35f * SmoothStep(0.35f, 0.65f, v);
        break;
      case 2:
        widthPulse = 0.85f + 0.25f * std::sin(wz * 0.157f);
        break;
      case 3:
        if (std::sin(wz * 0.209f) > 0.0f) {
          pulseSign = 1.0f;
        }
        widthPulse = 0.70f + 0.45f * pulseSign;
        break;
      case 4:
        widthPulse = 0.65f + 0.55f * std::pow(std::sin(wz * 0.131f), 2.0f);
        break;
      default:
        break;
      }
      fairwayWidth *= std::clamp(widthPulse, 0.55f, 1.35f);

      // 大きな起伏は ApplyLandforms で章ごとに付ける。ここでは外周と微小な凹凸のみ。
      float h = data.config.baseHeight;

      // 外周の土手。コースの縁を頂にした、なだらかな高まりにする（コースの外でも下る）。
      const float edgeDistanceX = std::abs(std::min(wx, courseW - wx));
      const float edgeDistanceZ = std::abs(std::min(wz, courseD - wz));
      float wallFactor = std::max(SmoothStep(edgeBandX, 0.0f, edgeDistanceX),
                                  SmoothStep(edgeBandZ, 0.0f, edgeDistanceZ));
      h += wallFactor * bankHeight;

      // 微小ノイズ
      h += FractalNoise(wx / 4.5f, wz / 6.7f, terrainSeed ^ 0x69e5u) *
           0.012f * hScale;

      SetHeight(data, x, z, h);

      // マテリアル判定
      float borderNoise =
          patternNoise(wx, wz, 13.3f, 20.0f, 0x7af3u) * 0.16f;
      float corridor = distPixels / std::max(fairwayWidth, 0.001f) + borderNoise;
      float materialNoise =
          patternNoise(wx, wz, 11.4f, 17.1f, 0x81bdu);
      float detailNoise =
          patternNoise(wx, wz, 22.9f, 34.3f, 0x92c7u);
      bool shortcut = false;
      if (courseStyle >= 2 && routePoints.size() > 2) {
        auto first = routePoints.front();
        auto last = routePoints.back();
        float shortcutDist =
            DistanceToSegment(u, v, first.first, first.second, last.first, last.second);
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
          float hazardType = patternNoise(wx, wz, 40.0f, 60.0f, 0xa391u) *
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
