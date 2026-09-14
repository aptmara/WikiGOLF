#pragma once
/**
 * @file TerrainGeneratorInternals.h
 * @brief 地形生成の分割実装で共有する内部関数
*/

#include "TerrainGenerator.h"

namespace game::systems {

/** @brief 生成範囲とコースの位置関係（コースは生成範囲の中央）。 */
struct CourseFrame {
  float width;   // コースの幅
  float depth;   // コースの奥行き
  float marginX; // 生成範囲の左端からコースの左端まで
  float marginZ; // 生成範囲の奥端（v = 0）からコースの奥端まで
};

inline CourseFrame CourseFrameOf(const TerrainConfig &config) {
  const float width = config.courseWidth > 0.0f ? config.courseWidth : config.worldWidth;
  const float depth = config.courseDepth > 0.0f ? config.courseDepth : config.worldDepth;
  return {width, depth, (config.worldWidth - width) * 0.5f,
          (config.worldDepth - depth) * 0.5f};
}

/** @brief 線形補間を計算します。 */
float Lerp(float a, float b, float t);

/** @brief エルミート補間（SmoothStep）を計算します。 */
float SmoothStep(float edge0, float edge1, float x);

/** @brief フラクタルノイズ値を計算します。 */
float FractalNoise(float x, float z, uint32_t seed);

/**
 * @brief ホール数とフィールド面積からグリーンの基準半径（メートル）を求めます。
 * @details ホールが密集する長い記事ではグリーンを小さくし、全面グリーン化を防ぐ。
 */
float BaseGreenRadius(float worldWidth, float worldDepth, size_t holeCount);

/** @brief 1 オクターブのなめらかな値ノイズ（-1〜1）を計算します。 */
float ValueNoise(float x, float z, uint32_t seed);

/**
 * @brief 記事の章ごとに高さの段と地形テーマを割り当て、大きな起伏を加えます。
 * @details ホール周辺は平らに保ち、ホールの無い場所にだけ丘や谷を作る。
 *          章の境目の斜面はホールが最も少ない位置へ寄せる。
 */
void ApplyLandforms(TerrainData &data, const std::string &seedText,
                    const std::vector<DirectX::XMFLOAT2> &holePositions);

/** @brief 地形マテリアルに応じたベースカラーを返します。 */
DirectX::XMFLOAT3 TerrainMaterialColor(uint8_t material);

/** @brief 点と線分の最短距離を計算します。 */
float DistanceToSegment(float px, float pz, float ax, float az, float bx,
                        float bz);

/** @brief バイオームに応じたハザードマテリアル種別を判定します。 */
uint8_t HazardMaterialForBiome(int biome, float roll);

} // namespace game::systems
