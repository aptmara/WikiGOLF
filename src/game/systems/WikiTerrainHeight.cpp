/**
 * @file WikiTerrainHeight.cpp
 * @brief 生成済み地形からワールド座標の高さを問い合わせます。
*/

#include "WikiTerrainSystem.h"

#include <algorithm>

namespace game::systems {

/**
 * @brief 指定したワールド座標における地形の高さを取得します。
 * @details 地形データの格子点をバイリニア補間して返します。
*/
float WikiTerrainSystem::GetHeight(float x, float z) const {
  if (!m_terrainData) {
    return 0.0f;
  }

  const float worldW = m_terrainData->config.worldWidth;
  const float worldD = m_terrainData->config.worldDepth;
  const int resX = m_terrainData->config.resolutionX;
  const int resZ = m_terrainData->config.resolutionZ;

  const float u = x / worldW + 0.5f;
  const float v = 0.5f - z / worldD;
  if (u < 0.0f || u >= 1.0f || v < 0.0f || v >= 1.0f) {
    return 0.0f;
  }

  const float fx = u * (resX - 1);
  const float fz = v * (resZ - 1);
  int ix = static_cast<int>(fx);
  int iz = static_cast<int>(fz);

  // 補間のためインデックスの範囲を制限します。
  ix = std::clamp(ix, 0, resX - 2);
  iz = std::clamp(iz, 0, resZ - 2);

  const float dx = fx - ix;
  const float dz = fz - iz;
  const float h00 = m_terrainData->heightMap[iz * resX + ix];
  const float h10 = m_terrainData->heightMap[iz * resX + (ix + 1)];
  const float h01 = m_terrainData->heightMap[(iz + 1) * resX + ix];
  const float h11 = m_terrainData->heightMap[(iz + 1) * resX + (ix + 1)];

  const float h0 = h00 * (1.0f - dx) + h10 * dx;
  const float h1 = h01 * (1.0f - dx) + h11 * dx;
  return h0 * (1.0f - dz) + h1 * dz;
}

} // namespace game::systems
