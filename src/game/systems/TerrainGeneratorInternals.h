#pragma once
/**
 * @file TerrainGeneratorInternals.h
 * @brief 地形生成の分割実装で共有する内部関数
*/

#include "TerrainGenerator.h"

namespace game::systems {

/** @brief 線形補間を計算します。 */
float Lerp(float a, float b, float t);

/** @brief エルミート補間（SmoothStep）を計算します。 */
float SmoothStep(float edge0, float edge1, float x);

/** @brief フラクタルノイズ値を計算します。 */
float FractalNoise(float x, float z, uint32_t seed);

/** @brief 地形マテリアルに応じたベースカラーを返します。 */
DirectX::XMFLOAT3 TerrainMaterialColor(uint8_t material);

/** @brief 点と線分の最短距離を計算します。 */
float DistanceToSegment(float px, float pz, float ax, float az, float bx,
                        float bz);

/** @brief バイオームに応じたハザードマテリアル種別を判定します。 */
uint8_t HazardMaterialForBiome(int biome, float roll);

} // namespace game::systems
