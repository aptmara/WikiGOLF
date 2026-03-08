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

// ヘルパー：スムースステップ
static float SmoothStep(float edge0, float edge1, float x) {
  x = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
  return x * x * (3 - 2 * x);
}

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

  // 1. 基本形状生成 (ノイズ + プラットフォーム)
  GenerateBaseHeightMap(data, articleText);

  // 2. リンク位置に基づくプラットフォーム生成
  CreatePlatforms(data, holePositions);

  // 3. スムージング処理
  ApplySmoothing(data, 3); // 3回スムージング

  // 4. メッシュ生成
  CalculateNormals(data);
  GenerateMesh(data, holePositions); // ホール位置を渡す

  return data;
}

void TerrainGenerator::GenerateBaseHeightMap(TerrainData &data,
                                             const std::string &text) {
  std::seed_seq seed(text.begin(), text.end());
  std::mt19937 rng(seed);
  std::uniform_real_distribution<float> dist(0.0f, 1.0f);

  int resX = data.config.resolutionX;
  int resZ = data.config.resolutionZ;
  int biome = data.config.biome;
  float hScale = data.config.heightScale;

  // 全体をデフォルトで初期化
  for (int i = 0; i < resX * resZ; ++i) {
    data.materialMap[i] = 1;
    data.heightMap[i] = data.config.baseHeight;
  }

  float startU = 0.5f;
  float startV = 0.85f;
  float fairwayWidthBase = resX * 0.12f;

  // ルート生成
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
      h += (dist(rng) - 0.5f) * 0.03f * hScale;

      SetHeight(data, x, z, h);

      // マテリアル判定
      float wallFactor = (distFromCenter > 0.85f)
                             ? SmoothStep(0.85f, 1.0f, distFromCenter)
                             : 0.0f;
      if (wallFactor > 0.3f) {
        data.materialMap[idx] = 1;
      } else if (distPixels < fairwayWidth * 0.5f) {
        data.materialMap[idx] = 0;
      } else if (distPixels < fairwayWidth * 0.9f) {
        data.materialMap[idx] = (dist(rng) < 0.7f) ? 0 : 1;
      } else if (distPixels < fairwayWidth * 1.5f) {
        data.materialMap[idx] = 1;
      } else {
        float hazardChance = std::clamp(
            (distPixels - fairwayWidth * 1.5f) / (resX * 0.1f), 0.0f, 0.5f);
        if (dist(rng) < hazardChance) {
          switch (biome) {
          case 0:
            data.materialMap[idx] = (dist(rng) < 0.3f) ? 2 : 1;
            break;
          case 1:
            data.materialMap[idx] = (dist(rng) < 0.6f) ? 2 : 7;
            break;
          case 2:
            data.materialMap[idx] = (dist(rng) < 0.5f) ? 4 : 1;
            break;
          case 3:
            data.materialMap[idx] = (dist(rng) < 0.4f) ? 7 : 6;
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

void TerrainGenerator::CreatePlatforms(
    TerrainData &data, const std::vector<DirectX::XMFLOAT2> &holePositions) {
  int resX = data.config.resolutionX;
  int resZ = data.config.resolutionZ;
  float worldW = data.config.worldWidth;
  float worldD = data.config.worldDepth;

  // 頂点カラー初期化（全体を白に）
  // 頂点生成前だが、ここでフラグなどを持てないので、一旦GenerateMesh内でやるか、
  // あるいはここでハイトマップ以外の情報も操作するか。
  // 今回はGenerateMeshで色を決めるために、TerrainDataに「地形属性マップ」を追加するのが正しいが、
  // 簡易的に「GenerateMeshで色を塗る際のロジック」を修正する方針にする。
  // しかしGenerateMeshは座標を知らない。
  // なので、GenerateMeshを修正し、holePositionsを参照できるようにするか、
  // データに色情報を埋め込むか。
  // verticesはこの後生成されるので、Vertex.colorをいじるなら生成時。
  // ひとまずここは形状のみに注力し、色は別途考える（あるいはGenerateMeshにholePositionsを渡す？）。
  // いや、頂点ごとに属性を持つ配列を追加しよう。
  // TerrainDataにはまだないので、GenerateMesh内で色を決定するロジックをハードコードする。

  // リンク位置に基づいてプラットフォームを作る
  for (const auto &pos : holePositions) {
    // ワールド座標 -> グリッドUV -> インデックス
    // px = (u - 0.5) * W  => u = px/W + 0.5
    // pz = (0.5 - v) * D  => v = 0.5 - pz/D

    float u = pos.x / worldW + 0.5f;
    float v = 0.5f - pos.y / worldD; // pos.y is Z in world coords here (vector2
                                     // x, z passed as x, y)

    int cx = (int)(u * (resX - 1));
    int cz = (int)(v * (resZ - 1));

    // プラットフォーム半径 (グリーン)
    int radius = resX / 10; // ほどよい大きさでテキストを邪魔しない

    // プラットフォームの高さ
    float currentCenterH = GetHeight(data, cx, cz);
    float targetHeight = currentCenterH + 0.05f; // わずかに持ち上げて埋没を防ぐ

    // Cup bowl depth
    float bowlDepth = 0.2f;

    // Bunker generation
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

        // Green area
        if (dist < radius) {
          data.materialMap[idx] = 3; // Green

          float cupRadius = 3.0f; // カップ半径（グリッド単位）
          if (dist < cupRadius) {
            // 平坦な穴（すり鉢ではなく一定深さ）
            float h = targetHeight - bowlDepth;
            SetHeight(data, x, z, h);
          } else {
            SetHeight(data, x, z, targetHeight);
          }
        } else if (dist < radius * 1.5f) {
          // Apron
          data.materialMap[idx] = 0;

          // Connect to rough
          float t = SmoothStep(radius, radius * 1.5f, dist);
          float currentH = GetHeight(data, x, z);
          SetHeight(data, x, z, Lerp(targetHeight, currentH, t));
        } else {
          // Generate bunkers
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
              // 窪ませる
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

void TerrainGenerator::CalculateNormals(TerrainData &data) {
  int resX = data.config.resolutionX;
  int resZ = data.config.resolutionZ;
  float cellW = data.config.worldWidth / (resX - 1);
  float cellD = data.config.worldDepth / (resZ - 1);

  data.normals.resize(data.heightMap.size());

  for (int z = 0; z < resZ; ++z) {
    for (int x = 0; x < resX; ++x) {
      // 隣接点を使って勾配を計算
      // L R
      // T B (Top/Bottom is Z axis)

      float hL = (x > 0) ? GetHeight(data, x - 1, z) : GetHeight(data, x, z);
      float hR =
          (x < resX - 1) ? GetHeight(data, x + 1, z) : GetHeight(data, x, z);
      float hD = (z > 0) ? GetHeight(data, x, z - 1)
                         : GetHeight(data, x, z); // Down (-Z)
      float hU = (z < resZ - 1) ? GetHeight(data, x, z + 1)
                                : GetHeight(data, x, z); // Up (+Z)

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

      switch (mat) {
      case 0: // Fairway
        vert.color = {0.2f, 0.6f, 0.2f, 0.25f};
        break;
      case 1: // Rough
        vert.color = {0.1f, 0.35f, 0.1f, 0.50f};
        break;
      case 2: // Bunker
        vert.color = {0.85f, 0.75f, 0.55f, 0.75f};
        break;
      case 3: // Green
        vert.color = {0.3f, 0.8f, 0.3f, 1.0f};
        break;
      case 4: // Ice
        vert.color = {0.7f, 0.85f, 1.0f, 0.6f};
        break;
      case 5: // Water
        vert.color = {0.2f, 0.4f, 0.8f, 0.5f};
        break;
      case 6: // Lava
        vert.color = {1.0f, 0.3f, 0.1f, 0.9f};
        break;
      case 7: // Stone
        vert.color = {0.5f, 0.5f, 0.55f, 0.8f};
        break;
      default:
        vert.color = {1.0f, 1.0f, 1.0f, 1.0f};
        break;
      }

      // ホール可視化（黒く塗る）
      // ホール可視化オーバーレイは行わない（旗モデルで示す）

      vertices.push_back(vert);
    }
  }

  // インデックス生成 (Triangle List)
  for (int z = 0; z < resZ - 1; ++z) {
    for (int x = 0; x < resX - 1; ++x) {
      // 0 --- 1
      // |  /  |
      // 2 --- 3
      //
      // Tri 1: 0-1-2
      // Tri 2: 2-1-3

      uint32_t i0 = z * resX + x;
      uint32_t i1 = z * resX + (x + 1);
      uint32_t i2 = (z + 1) * resX + x;
      uint32_t i3 = (z + 1) * resX + (x + 1);

      // 時計回りか反時計回りかはカリング設定による
      // 通常DirectXは時計回りが表面だが、CullNoneならどちらでも見える
      // ここでは標準的な時計回りで定義

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

float TerrainGenerator::GetHeight(const TerrainData &data, int x, int z) {
  if (x < 0 || x >= data.config.resolutionX || z < 0 ||
      z >= data.config.resolutionZ)
    return 0.0f;
  return data.heightMap[z * data.config.resolutionX + x];
}

void TerrainGenerator::SetHeight(TerrainData &data, int x, int z, float h) {
  if (x < 0 || x >= data.config.resolutionX || z < 0 ||
      z >= data.config.resolutionZ)
    return;
  data.heightMap[z * data.config.resolutionX + x] = h;
}

} // namespace game::systems
