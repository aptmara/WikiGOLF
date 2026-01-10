#pragma once

#include "../../graphics/Mesh.h"
#include <DirectXMath.h>
#include <string>
#include <vector>

namespace game::systems {

struct TerrainConfig {
  int resolutionX = 128;
  int resolutionZ = 128;
  float worldWidth = 20.0f;
  float worldDepth = 30.0f;
  float baseHeight = 0.0f;
  float heightScale = 5.0f; // 高低差の最大値
  float friction = 0.5f;    // 地形の基本摩擦
  float restitution = 0.2f; // 地形の基本反発
};

struct TerrainData {
  std::vector<float> heightMap;
  std::vector<uint8_t>
      materialMap; // マテリアルID (0:Fairway, 1:Rough, 2:Bunker, 3:Green)
  std::vector<DirectX::XMFLOAT3> normals; // 物理・描画用法線

  // 生のメッシュデータ (リソース生成用)
  std::vector<graphics::Vertex> vertices;
  std::vector<uint32_t> indices;

  graphics::Mesh mesh; // (Optional)
  TerrainConfig config;
};

class TerrainGenerator {
public:
  // 記事データに基づいて地形データを生成
  static TerrainData
  GenerateTerrain(const std::string &articleText,
                  const std::vector<DirectX::XMFLOAT2> &holePositions,
                  const TerrainConfig &config);

private:
  // ハイトマップ生成の各ステップ
  static void GenerateBaseHeightMap(TerrainData &data, const std::string &text);
  static void
  CreatePlatforms(TerrainData &data,
                  const std::vector<DirectX::XMFLOAT2> &holePositions);
  static void ApplySmoothing(TerrainData &data, int iterations);
  static void
  GenerateMesh(TerrainData &data,
               const std::vector<DirectX::XMFLOAT2> &holePositions = {});
  static void CalculateNormals(TerrainData &data);

  // ユーティリティ
  static float GetHeight(const TerrainData &data, int x, int z);
  static void SetHeight(TerrainData &data, int x, int z, float h);
};

} // namespace game::systems
