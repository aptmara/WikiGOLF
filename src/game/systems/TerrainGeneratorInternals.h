#pragma once
/**
 * @file TerrainGeneratorInternals.h
 * @brief 地形生成の分割実装で共有する内部関数
 */

#include "TerrainGenerator.h"

namespace game::systems {

float Lerp(float a, float b, float t);
float SmoothStep(float edge0, float edge1, float x);
float FractalNoise(float x, float z, uint32_t seed);
DirectX::XMFLOAT3 TerrainMaterialColor(uint8_t material);
float DistanceToSegment(float px, float pz, float ax, float az, float bx,
                        float bz);
uint8_t HazardMaterialForBiome(int biome, float roll);

} // namespace game::systems
