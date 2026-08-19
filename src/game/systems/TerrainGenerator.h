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

  // バイオーム設定（草原、砂漠、氷原、岩場）
  int biome = 0;
};

struct TerrainData {
  std::vector<float> heightMap;
  std::vector<uint8_t>
      materialMap; // マテリアルID（Fairway, Rough, Bunker, Green）
  std::vector<DirectX::XMFLOAT3>
      visualMaterialColors; // 描画専用の連続した地表カラー
  std::vector<DirectX::XMFLOAT3> normals; // 物理・描画用法線

  // 生のメッシュデータ (リソース生成用)
  std::vector<graphics::Vertex> vertices;
  std::vector<uint32_t> indices;

  graphics::Mesh mesh; // (Optional)
  TerrainConfig config;
};

class TerrainGenerator {
public:
  /**
   * @brief 記事データに基づいて地形データを生成します。
   */
  static TerrainData
  GenerateTerrain(const std::string &articleText,
                  const std::vector<DirectX::XMFLOAT2> &holePositions,
                  const TerrainConfig &config);

  /**
   * @brief チュートリアル用の固定教材地形を生成します。
   */
  static TerrainData
  GenerateTutorialTerrain(const TerrainConfig &config,
                          const std::vector<DirectX::XMFLOAT2> &holePositions);

private:
  /**
   * @brief 基準ハイトマップを生成します。
   */
  static void GenerateBaseHeightMap(TerrainData &data, const std::string &text);

  /**
   * @brief ホール周辺に平らなプラットフォームを作成します。
   */
  static void
  CreatePlatforms(TerrainData &data,
                   const std::vector<DirectX::XMFLOAT2> &holePositions);

  /**
   * @brief 小さすぎる孤立地形を整理し、コースの読みやすさを保ちます。
   */
  static void ApplyMaterialCleanup(TerrainData &data);

  /**
   * @brief 離散した物理材質から描画専用の連続カラーフィールドを生成します。
   */
  static void GenerateVisualMaterialColors(TerrainData &data);

  /**
   * @brief ハイトマップにスムージング処理を適用します。
   */
  static void ApplySmoothing(TerrainData &data, int iterations);

  /**
   * @brief 地形メッシュを生成します。
   */
  static void
  GenerateMesh(TerrainData &data,
               const std::vector<DirectX::XMFLOAT2> &holePositions = {});

  /**
   * @brief 地形の法線を計算します。
   */
  static void CalculateNormals(TerrainData &data);

  /**
   * @brief 指定した格子座標の地形高さを取得します。
   */
  static float GetHeight(const TerrainData &data, int x, int z);

  /**
   * @brief 指定した格子座標の地形高さを設定します。
   */
  static void SetHeight(TerrainData &data, int x, int z, float h);
};

} // namespace game::systems
