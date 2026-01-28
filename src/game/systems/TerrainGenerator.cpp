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
  // 擬似乱数生成器 (記事テキストをシードにする)
  std::seed_seq seed(text.begin(), text.end());
  std::mt19937 rng(seed);
  std::uniform_real_distribution<float> dist(0.0f, 1.0f);

  int resX = data.config.resolutionX;
  int resZ = data.config.resolutionZ;

  // 簡易パーリンノイズ風 (周波数を変えて重ね合わせ)
  for (int z = 0; z < resZ; ++z) {
    for (int x = 0; x < resX; ++x) {
      float nx = (float)x / resX;
      float nz = (float)z / resZ;

      // 低周波 (大きな起伏)
      float h1 = std::sin(nx * 3.14f * 2.0f) * std::cos(nz * 3.14f * 2.0f);

      // 高周波 (細かな凹凸) -> 文字が読みにくくなるので大幅に減らす
      float h2 = std::sin(nx * 10.0f + nz * 5.0f) * 0.05f;

      // 外周を高くする (壁)
      float wallFactor = 0.0f;
      float dx = nx - 0.5f;
      float dz = nz - 0.5f;
      float distFromCenter =
          std::sqrt(dx * dx + dz * dz) * 2.0f; // 0.0 center -> 1.0 edge

      if (distFromCenter > 0.8f) {
        wallFactor = SmoothStep(0.8f, 1.0f, distFromCenter) * 3.0f;
      }

      float h = (h1 * 0.5f + h2 * 0.1f) * data.config.heightScale + wallFactor;

      // ベース高さ調整
      SetHeight(data, x, z, h + data.config.baseHeight);

      // マテリアル設定: 外周(壁)はラフ、またはノイズが高い場所
      int idx = z * resX + x;
      if (wallFactor > 0.5f) {
        data.materialMap[idx] = 1; // Rough
      } else if (h2 > 0.03f) {     // 起伏が激しい場所もラフ
        data.materialMap[idx] = 1;
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
    float bowlDepth = 0.15f;

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

          float cupRadius = 1.5f;
          if (dist < cupRadius) {
            float t = dist / cupRadius;
            float shape = std::cos(t * 3.14159f * 0.5f);
            float h = targetHeight - shape * bowlDepth;
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
        vert.color = {0.2f, 0.6f, 0.2f, 1.0f};
        break;
      case 1: // Rough
        vert.color = {0.1f, 0.35f, 0.1f, 1.0f};
        break;
      case 2: // Bunker
        vert.color = {0.85f, 0.75f, 0.55f, 1.0f};
        break;
      case 3: // Green
        vert.color = {0.3f, 0.8f, 0.3f, 1.0f};
        break;
      default:
        vert.color = {1.0f, 1.0f, 1.0f, 1.0f};
        break;
      }

      // ホール可視化（黒く塗る）
      // 座標系: px, pz (World)
      for (const auto &hole : holePositions) {
        float dx = px - hole.x;
        float dz = pz - hole.y; // hole.y is Z
        float distSq = dx * dx + dz * dz;

        // 塗りつぶしは極小範囲に限定し、色も明るめにして黒ずみを避ける
        if (distSq < 0.25f * 0.25f) {
          vert.color = {0.9f, 0.95f, 0.9f, 1.0f};
          break;
        }
      }

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
