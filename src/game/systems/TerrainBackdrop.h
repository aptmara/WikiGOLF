#pragma once
/**
 * @file TerrainBackdrop.h
 * @brief コースの四方に広がる、見た目専用の延長地形（山並み）を生成します。
 */

#include "TerrainGenerator.h"
#include <cstdint>
#include <vector>

namespace game::systems {

/** @brief 延長地形メッシュの 1 チャンク（分割して視錐台カリングを効かせる）。 */
struct TerrainBackdropChunk {
  std::vector<graphics::Vertex> vertices;
  std::vector<uint32_t> indices;
};

/** @brief 延長地形の標高と地表。 */
struct TerrainBackdropSample {
  float height = 0.0f;
  uint8_t material = 1;
  DirectX::XMFLOAT3 color = {1.0f, 1.0f, 1.0f}; // 頂点カラー（コースの色から連続させる）
};

/** @brief コースの外側へ広げる延長地形の幅（メートル）。 */
constexpr float kTerrainBackdropReach = 150.0f;

/** @brief 縁の近くで延長地形をそのまま見せ、山並みを重ね始めるまでの目安（メートル）。 */
constexpr float kTerrainBackdropBlend = 50.0f;

/** @brief コースと延長地形の高さ・色のずれを消す縁からの距離（メートル）。 */
constexpr float kTerrainBackdropSeam = 12.0f;

/**
 * @brief 地点 (worldX, worldZ) の延長地形を返します。
 * @details コース内ではコースの地形そのものを返す。コースの外では、コースと同じ規則で
 *          外まで生成した延長地形（TerrainData::extension）を使い、章の段・テーマ・地表の模様を
 *          そのまま外へ続ける。遠くへ行くほどバイオームに応じた山並みを重ねる。
 *          縁の高さは必ずコースと一致する。
 *          seed は記事ごとに山並みの形を変えるために使う。
 */
TerrainBackdropSample SampleTerrainBackdrop(const TerrainData &data, float worldX,
                                            float worldZ, uint32_t seed);

/** @brief コースの前後左右を囲む延長地形メッシュを生成します。 */
std::vector<TerrainBackdropChunk> BuildTerrainBackdrop(const TerrainData &data,
                                                       uint32_t seed);

} // namespace game::systems
