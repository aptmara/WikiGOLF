#pragma once
/**
 * @file TerrainGenerator.h
 * @brief TerrainGenerator クラスおよび関連システム
*/

#include "../../graphics/Mesh.h"
#include <DirectXMath.h>
#include "HtmlTerrainRules.h"
#include <memory>
#include <string>
#include <vector>

namespace game::systems {

struct TerrainConfig {
  bool htmlCourse = false;
  std::vector<HtmlTerrainRegion> htmlRegions;
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

  // コースの寸法。0 なら生成範囲（worldWidth/worldDepth）と同じ。
  // 延長地形ではコースより広い範囲を、コースを中心にして同じ規則で生成する。
  float courseWidth = 0.0f;
  float courseDepth = 0.0f;

  // コースの四方に続く延長地形（見た目用）も生成するか
  bool generateExtension = false;
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

  // コースの外側まで同じ規則で広げた粗い地形（generateExtension のときのみ）。
  // ホールに由来するグリーンや平坦化は含まない。
  std::shared_ptr<const TerrainData> extension;
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
   * @brief コースの四方へ広げた延長地形を、コースと同じ規則で粗く生成します。
   * @details 章の段・テーマ・地表の模様・記事ごとの模様をコースの外まで続ける。
   *          ホールに由来するグリーン、バンカー、平坦化は作らない。
*/
  static TerrainData
  GenerateExtensionTerrain(const std::string &articleText,
                           const std::vector<DirectX::XMFLOAT2> &holePositions,
                           const TerrainConfig &courseConfig);

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
  static void GenerateBaseHeightMap(
      TerrainData &data, const std::string &text,
      const std::vector<DirectX::XMFLOAT2> &holePositions);

  /**
   * @brief ホール周辺に平らなプラットフォームを作成します。
*/
  static void
  CreatePlatforms(TerrainData &data,
                   const std::vector<DirectX::XMFLOAT2> &holePositions);

  /**
   * @brief カップ（穴）の周囲を完全に平らにします。
   * @details 地表は穴の円内をシェーダーでくり抜き、下にカップ内壁を描くため、
   *          縁の高さが一定でないと内壁との間に隙間が見えてしまう。
   *          スムージング後に呼び出すこと。
*/
  static void
  FlattenCupSites(TerrainData &data,
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
