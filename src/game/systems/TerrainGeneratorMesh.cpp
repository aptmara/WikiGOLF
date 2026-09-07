#include "TerrainGeneratorInternals.h"
#include "TerrainGenerator.h"
#include "../../core/Logger.h"
#include "../../graphics/TangentGenerator.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace game::systems {

using namespace DirectX;

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
