#include "TerrainGenerator.h"
#include "../../core/Logger.h"
#include "../../graphics/TangentGenerator.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace game::systems {

using namespace DirectX;

// 繝倥Ν繝代・・夂ｷ壼ｽ｢陬憺俣
static float Lerp(float a, float b, float t) { return a + (b - a) * t; }

// 繝倥Ν繝代・・壹せ繝繝ｼ繧ｹ繧ｹ繝・ャ繝・
static float SmoothStep(float edge0, float edge1, float x) {
  x = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
  return x * x * (3 - 2 * x);
}

/**
 * @brief 蝨ｰ蠖｢繝・・繧ｿ繧堤函謌舌＠縺ｾ縺吶・
 */
TerrainData TerrainGenerator::GenerateTerrain(
    const std::string &articleText,
    const std::vector<DirectX::XMFLOAT2> &holePositions,
    const TerrainConfig &config) {
  TerrainData data;
  data.config = config;

  // 繝上う繝医・繝・・蛻晄悄蛹・
  int totalVerts = config.resolutionX * config.resolutionZ;
  data.heightMap.resize(totalVerts, 0.0f);
  data.materialMap.resize(totalVerts, 0); // 0: Fairway

  // 蝓ｺ譛ｬ蠖｢迥ｶ縺ｮ逕滓・
  GenerateBaseHeightMap(data, articleText);

  // 繝ｪ繝ｳ繧ｯ菴咲ｽｮ縺ｫ蝓ｺ縺･縺上・繝ｩ繝・ヨ繝輔か繝ｼ繝縺ｮ逕滓・
  CreatePlatforms(data, holePositions);

  // 繧ｹ繝繝ｼ繧ｸ繝ｳ繧ｰ蜃ｦ逅・
  ApplySmoothing(data, 3); // 3蝗槭せ繝繝ｼ繧ｸ繝ｳ繧ｰ

  // 繝｡繝・す繝･縺ｮ逕滓・
  CalculateNormals(data);
  GenerateMesh(data, holePositions); // 繝帙・繝ｫ菴咲ｽｮ繧呈ｸ｡縺・

  return data;
}

/**
 * @brief 蝓ｺ貅悶ワ繧､繝医・繝・・繧堤函謌舌＠縺ｾ縺吶・
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

  // 蜈ｨ菴薙ｒ繝・ヵ繧ｩ繝ｫ繝医〒蛻晄悄蛹・
  for (int i = 0; i < resX * resZ; ++i) {
    data.materialMap[i] = 1;
    data.heightMap[i] = data.config.baseHeight;
  }

  float startU = 0.5f;
  float startV = 0.85f;
  float fairwayWidthBase = resX * 0.12f;

  // 繝ｫ繝ｼ繝育函謌・
  std::vector<std::pair<float, float>> routePoints;
  routePoints.push_back({startU, startV});

  int numSegments = 5 + (int)(dist(rng) * 3);
  float currentU = startU;
  float currentV = startV;

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

    float swerve = (dist(rng) - 0.5f) * 2.0f * swerveAmount;
    currentU = std::clamp(currentU + swerve, 0.15f, 0.85f);
    currentV = targetV;
    routePoints.push_back({currentU, currentV});

    float heightChange = (dist(rng) - 0.5f) * 1.5f * hScale;
    float prevHeight = segmentHeights.back();
    segmentHeights.push_back(
        std::clamp(prevHeight + heightChange, -1.0f * hScale, 2.0f * hScale));
  }

  // 繝槭え繝ｳ繝臥函謌・
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

  // 鬮倥＆險育ｮ・
  for (int z = 0; z < resZ; ++z) {
    float v = (float)z / (resZ - 1);
    float routeU = startU;
    float routeHeight = 0.0f;

    for (size_t seg = 0; seg < routePoints.size() - 1; ++seg) {
      float v0 = routePoints[seg].second;
      float v1 = routePoints[seg + 1].second;
      if (v <= v0 && v >= v1) {
        float t = (v0 - v) / (v0 - v1 + 0.0001f);
        routeU = Lerp(routePoints[seg].first, routePoints[seg + 1].first, t);
        routeHeight = Lerp(segmentHeights[seg], segmentHeights[seg + 1], t);
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

      float h = data.config.baseHeight;

      // 騾ｲ陦梧婿蜷代・襍ｷ莨・
      float routeInfluence =
          1.0f - std::clamp(distFromRoute * 4.0f, 0.0f, 1.0f);
      h += routeHeight * routeInfluence;

      // 繧ｵ繧､繝峨せ繝ｭ繝ｼ繝・
      h += std::min(distFromRoute * 0.3f, 0.2f) * hScale;

      // 繝槭え繝ｳ繝・
      for (const auto &m : mounds) {
        float du = u - m.u;
        float dv = v - m.v;
        float distToMound = std::sqrt(du * du + dv * dv);
        if (distToMound < m.radius) {
          float t = 1.0f - (distToMound / m.radius);
          h += t * t * m.height * hScale;
        }
      }

      // 繝舌う繧ｪ繝ｼ繝蛻･繝代ち繝ｼ繝ｳ
      switch (biome) {
      case 0:
        h += std::sin(u * 9.42f) * std::sin(v * 12.56f) * 0.1f * hScale;
        break;
      case 1:
        h += std::sin(u * 18.84f + v * 2.0f) * 0.2f * hScale +
             std::sin(v * 12.56f) * 0.15f * hScale;
        break;
      case 2:
        h += std::sin(u * 6.28f + v * 6.28f) * 0.03f * hScale;
        break;
      case 3:
        h += std::sin(u * 31.4f) * std::sin(v * 25.12f) * 0.25f * hScale +
             std::sin(u * 50.24f + v * 3.0f) * 0.1f * hScale;
        break;
      }

      // 繝・ぅ繝ｼ繧､繝ｳ繧ｰ繧ｨ繝ｪ繧｢
      if (v > 0.8f && distFromRoute < 0.1f) {
        h += SmoothStep(0.8f, 0.9f, v) * 0.2f * hScale;
      }

      // 螟門捉螢・
      float dx = u - 0.5f;
      float dz = v - 0.5f;
      float distFromCenter = std::sqrt(dx * dx + dz * dz) * 2.0f;
      if (distFromCenter > 0.85f) {
        h += SmoothStep(0.85f, 1.0f, distFromCenter) * 4.0f;
      }

      // 蠕ｮ蟆上ヮ繧､繧ｺ
      h += (dist(rng) - 0.5f) * 0.03f * hScale;

      SetHeight(data, x, z, h);

      // 繝槭ユ繝ｪ繧｢繝ｫ蛻､螳・
      float wallFactor = 0.0f;
      if (distFromCenter > 0.85f) {
        wallFactor = SmoothStep(0.85f, 1.0f, distFromCenter);
      }
      if (wallFactor > 0.3f) {
        data.materialMap[idx] = 1;
      } else if (distPixels < fairwayWidth * 0.5f) {
        data.materialMap[idx] = 0;
      } else if (distPixels < fairwayWidth * 0.9f) {
        if (dist(rng) < 0.7f) {
          data.materialMap[idx] = 0;
        } else {
          data.materialMap[idx] = 1;
        }
      } else if (distPixels < fairwayWidth * 1.5f) {
        data.materialMap[idx] = 1;
      } else {
        float hazardChance = std::clamp(
            (distPixels - fairwayWidth * 1.5f) / (resX * 0.1f), 0.0f, 0.5f);
        if (dist(rng) < hazardChance) {
          switch (biome) {
          case 0:
            if (dist(rng) < 0.3f) { data.materialMap[idx] = 2; } else { data.materialMap[idx] = 1; }
            break;
          case 1:
            if (dist(rng) < 0.6f) { data.materialMap[idx] = 2; } else { data.materialMap[idx] = 7; }
            break;
          case 2:
            if (dist(rng) < 0.5f) { data.materialMap[idx] = 4; } else { data.materialMap[idx] = 1; }
            break;
          case 3:
            if (dist(rng) < 0.4f) { data.materialMap[idx] = 7; } else { data.materialMap[idx] = 6; }
            break;
          default:
            data.materialMap[idx] = 1;
            break;
          }
        } else {
          data.materialMap[idx] = 1;
        }
      }
    }
  }
}

/**
 * @brief 繝帙・繝ｫ蜻ｨ霎ｺ縺ｫ蟷ｳ繧峨↑繝励Λ繝・ヨ繝輔か繝ｼ繝繧剃ｽ懈・縺励∪縺吶・
 */
void TerrainGenerator::CreatePlatforms(
    TerrainData &data, const std::vector<DirectX::XMFLOAT2> &holePositions) {
  int resX = data.config.resolutionX;
  int resZ = data.config.resolutionZ;
  float worldW = data.config.worldWidth;
  float worldD = data.config.worldDepth;

  // 繝ｪ繝ｳ繧ｯ菴咲ｽｮ縺ｫ蝓ｺ縺･縺・※繝励Λ繝・ヨ繝輔か繝ｼ繝繧剃ｽ懊ｋ
  for (const auto &pos : holePositions) {
    // 繝ｯ繝ｼ繝ｫ繝牙ｺｧ讓吶°繧峨げ繝ｪ繝・ラUV縲√う繝ｳ繝・ャ繧ｯ繧ｹ縺ｸ縺ｮ騾・ｮ怜､画鋤蠑・

    float u = pos.x / worldW + 0.5f;
    float v = 0.5f - pos.y / worldD; // pos.y is Z in world coords here (vector2
                                     // x, z passed as x, y)

    int cx = (int)(u * (resX - 1));
    int cz = (int)(v * (resZ - 1));

    // 繝励Λ繝・ヨ繝輔か繝ｼ繝蜊雁ｾ・(繧ｰ繝ｪ繝ｼ繝ｳ)
    int radius = resX / 10; // 縺ｻ縺ｩ繧医＞螟ｧ縺阪＆縺ｧ繝・く繧ｹ繝医ｒ驍ｪ鬲斐＠縺ｪ縺・

    // 繝励Λ繝・ヨ繝輔か繝ｼ繝縺ｮ鬮倥＆
    float currentCenterH = GetHeight(data, cx, cz);
    float targetHeight = currentCenterH + 0.05f; // 繧上★縺九↓謖√■荳翫￡縺ｦ蝓区ｲ｡繧帝亟縺・

    // 繧ｫ繝・・蠎暮Κ縺ｮ豺ｱ縺・
    float bowlDepth = 0.2f;

    // 繝舌Φ繧ｫ繝ｼ縺ｮ逕滓・蜃ｦ逅・
    std::mt19937 tempRng(cx + cz * resX);
    std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
    float bunkerAngle = dist01(tempRng) * 6.28f;
    float bunkerDist = radius * 1.8f;

    for (int z = cz - radius * 3; z <= cz + radius * 3; ++z) {
      for (int x = cx - radius * 3; x <= cx + radius * 3; ++x) {
        if (x < 0 || x >= resX || z < 0 || z >= resZ)
          continue;

        float dx = (float)(x - cx);
        float dz = (float)(z - cz);
        float dist = std::sqrt(dx * dx + dz * dz);
        int idx = z * resX + x;

        // 繧ｰ繝ｪ繝ｼ繝ｳ繧ｨ繝ｪ繧｢
        if (dist < radius) {
          data.materialMap[idx] = 3; // Green

          float cupRadius = 3.0f; // 繧ｫ繝・・蜊雁ｾ・ｼ医げ繝ｪ繝・ラ蜊倅ｽ搾ｼ・
          if (dist < cupRadius) {
            // 蟷ｳ蝮ｦ縺ｪ遨ｴ・医☆繧企欧縺ｧ縺ｯ縺ｪ縺丈ｸ螳壽ｷｱ縺包ｼ・
            float h = targetHeight - bowlDepth;
            SetHeight(data, x, z, h);
          } else {
            SetHeight(data, x, z, targetHeight);
          }
        } else if (dist < radius * 1.5f) {
          // 繧ｨ繝励Ο繝ｳ繧ｨ繝ｪ繧｢
          data.materialMap[idx] = 0;

          // 繝ｩ繝輔→縺ｮ繝悶Ξ繝ｳ繝画磁邯・
          float t = SmoothStep(radius, radius * 1.5f, dist);
          float currentH = GetHeight(data, x, z);
          SetHeight(data, x, z, Lerp(targetHeight, currentH, t));
        } else {
          // 繝舌Φ繧ｫ繝ｼ縺ｮ逕滓・
          float angle = std::atan2(dz, dx);
          float angleDiff = angle - bunkerAngle;
          while (angleDiff > 3.14159f)
            angleDiff -= 6.28318f;
          while (angleDiff < -3.14159f)
            angleDiff += 6.28318f;

          if (std::abs(angleDiff) < 0.6f) {
            float distFromBunkerCenter =
                std::sqrt(std::pow(dist - bunkerDist, 2.0f));
            float bunkerRadius = radius * 0.5f;
            if (distFromBunkerCenter < bunkerRadius) {
              data.materialMap[idx] = 2; // Bunker
              // 遯ｪ縺ｾ縺帙ｋ
              float currentH = GetHeight(data, x, z);
              SetHeight(data, x, z,
                        currentH - 0.25f * std::cos(distFromBunkerCenter /
                                                    bunkerRadius * 1.57f));
            }
          }
        }
      }
    }
  }
}

/**
 * @brief 繝上う繝医・繝・・縺ｫ繧ｹ繝繝ｼ繧ｸ繝ｳ繧ｰ蜃ｦ逅・ｒ驕ｩ逕ｨ縺励∪縺吶・
 */
void TerrainGenerator::ApplySmoothing(TerrainData &data, int iterations) {
  int resX = data.config.resolutionX;
  int resZ = data.config.resolutionZ;
  std::vector<float> tempMap = data.heightMap;

  for (int iter = 0; iter < iterations; ++iter) {
    for (int z = 1; z < resZ - 1; ++z) {
      for (int x = 1; x < resX - 1; ++x) {
        // 3x3 蟷ｳ蝮・
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
 * @brief 蝨ｰ蠖｢縺ｮ豕慕ｷ壹ｒ險育ｮ励＠縺ｾ縺吶・
 */
void TerrainGenerator::CalculateNormals(TerrainData &data) {
  int resX = data.config.resolutionX;
  int resZ = data.config.resolutionZ;
  float cellW = data.config.worldWidth / (resX - 1);
  float cellD = data.config.worldDepth / (resZ - 1);

  data.normals.resize(data.heightMap.size());

  for (int z = 0; z < resZ; ++z) {
    for (int x = 0; x < resX; ++x) {
      // 蜍ｾ驟崎ｨ育ｮ礼畑縺ｮ髫｣謗･繧ｻ繝ｫ繧､繝ｳ繝・ャ繧ｯ繧ｹ謖・ｮ・

      float hL = GetHeight(data, x, z);
      if (x > 0) hL = GetHeight(data, x - 1, z);
      
      float hR = GetHeight(data, x, z);
      if (x < resX - 1) hR = GetHeight(data, x + 1, z);
      
      float hD = GetHeight(data, x, z);
      if (z > 0) hD = GetHeight(data, x, z - 1);
      
      float hU = GetHeight(data, x, z);
      if (z < resZ - 1) hU = GetHeight(data, x, z + 1);

      // 謗･邱壹・繧ｯ繝医Ν
      XMVECTOR tangentX = XMVectorSet(2.0f * cellW, hR - hL, 0.0f, 0.0f);
      XMVECTOR tangentZ = XMVectorSet(0.0f, hU - hD, -2.0f * cellD, 0.0f);

      // 豕慕ｷ・= Cross(X, Z)  (蟾ｦ謇句ｺｧ讓咏ｳｻ Y-up)縲る・ｺ上ｒ隱､繧九→荳句髄縺阪↓縺ｪ繧九・
      XMVECTOR normal = XMVector3Cross(tangentX, tangentZ);
      normal = XMVector3Normalize(normal);

      XMStoreFloat3(&data.normals[z * resX + x], normal);
    }
  }
}

/**
 * @brief 蝨ｰ蠖｢繝｡繝・す繝･繧堤函謌舌＠縺ｾ縺吶・
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

  // 鬆らせ逕滓・
  for (int z = 0; z < resZ; ++z) {
    for (int x = 0; x < resX; ++x) {
      float u = (float)x / (resX - 1);
      float v = (float)z / (resZ - 1); // 1.0 - ... 縺ｫ縺吶ｋ縺九・UV蠎ｧ讓咏ｳｻ縺ｫ繧医ｋ

      float px = (u - 0.5f) * width;
      float pz =
          (0.5f - v) * depth; // Z霆ｸ蜿崎ｻ｢豕ｨ諢上ゅ％縺薙〒縺ｯ謇句燕縺・Z縺ｨ縺吶ｋ縺ｪ繧峨％繧後〒OK
      float py = GetHeight(data, x, z);

      graphics::Vertex vert;
      vert.position = {px, py, pz};
      vert.normal = data.normals[z * resX + x];
      vert.texCoord = {u, v};

      // 繝・ヵ繧ｩ繝ｫ繝郁牡 (繝槭ユ繝ｪ繧｢繝ｫ繝槭ャ繝励↓蝓ｺ縺･縺・
      int idx = z * resX + x;
      uint8_t mat = data.materialMap[idx];

      switch (mat) {
      case 0: // 繝輔ぉ繧｢繧ｦ繧ｧ繧､
        vert.color = {0.2f, 0.6f, 0.2f, 0.25f};
        break;
      case 1: // 繝ｩ繝・
        vert.color = {0.1f, 0.35f, 0.1f, 0.50f};
        break;
      case 2: // 繝舌Φ繧ｫ繝ｼ
        vert.color = {0.85f, 0.75f, 0.55f, 0.75f};
        break;
      case 3: // 繧ｰ繝ｪ繝ｼ繝ｳ
        vert.color = {0.3f, 0.8f, 0.3f, 1.0f};
        break;
      case 4: // 豌ｷ蜴・
        vert.color = {0.7f, 0.85f, 1.0f, 0.6f};
        break;
      case 5: // 豌ｴ
        vert.color = {0.2f, 0.4f, 0.8f, 0.5f};
        break;
      case 6: // 貅ｶ蟯ｩ
        vert.color = {1.0f, 0.3f, 0.1f, 0.9f};
        break;
      case 7: // 蟯ｩ遏ｳ
        vert.color = {0.5f, 0.5f, 0.55f, 0.8f};
        break;
      default:
        vert.color = {1.0f, 1.0f, 1.0f, 1.0f};
        break;
      }

      vertices.push_back(vert);
    }
  }

  // 繧､繝ｳ繝・ャ繧ｯ繧ｹ逕滓・ (Triangle List)
  for (int z = 0; z < resZ - 1; ++z) {
    for (int x = 0; x < resX - 1; ++x) {
      // 荳芽ｧ貞ｽ｢繧､繝ｳ繝・ャ繧ｯ繧ｹ縺ｮ險ｭ螳夲ｼ・-1-2縺翫ｈ縺ｳ2-1-3・・
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

  // 繝・・繧ｿ譬ｼ邏・
  data.vertices = std::move(vertices);
  data.indices = std::move(indices);
}

/**
 * @brief 謖・ｮ壹＠縺滓ｼ蟄仙ｺｧ讓吶・蝨ｰ蠖｢鬮倥＆繧貞叙蠕励＠縺ｾ縺吶・
 */
float TerrainGenerator::GetHeight(const TerrainData &data, int x, int z) {
  if (x < 0 || x >= data.config.resolutionX || z < 0 ||
      z >= data.config.resolutionZ)
    return 0.0f;
  return data.heightMap[z * data.config.resolutionX + x];
}

/**
 * @brief 謖・ｮ壹＠縺滓ｼ蟄仙ｺｧ讓呷攪蝨ｰ蠖｢鬮倥＆繧定ｨｭ螳壹＠縺ｾ縺吶・
 */
void TerrainGenerator::SetHeight(TerrainData &data, int x, int z, float h) {
  if (x < 0 || x >= data.config.resolutionX || z < 0 ||
      z >= data.config.resolutionZ)
    return;
  data.heightMap[z * data.config.resolutionX + x] = h;
}

} // namespace game::systems
