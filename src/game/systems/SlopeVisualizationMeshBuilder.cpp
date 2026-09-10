/**
 * @file SlopeVisualizationMeshBuilder.cpp
 * @brief SlopeVisualizationMeshBuilderの実装です。
*/

#include "SlopeVisualizationMeshBuilder.h"
#include "WikiTerrainSystem.h"
#include <algorithm>
#include <cmath>

namespace game::systems {

using namespace DirectX;

SlopeVisualizationMeshBuilder::SampledPoint
SlopeVisualizationMeshBuilder::SamplePoint(const WikiTerrainSystem &terrain,
                                           float worldX, float worldZ,
                                           const SlopeOverlayConfig &config) {
  const float d = (std::max)(config.sampleOffset, 0.05f);
  const float hL = terrain.GetHeight(worldX - d, worldZ);
  const float hR = terrain.GetHeight(worldX + d, worldZ);
  const float hD = terrain.GetHeight(worldX, worldZ - d);
  const float hU = terrain.GetHeight(worldX, worldZ + d);

  // 高さが下がる方向（hL > hR なら +x 方向へ下る）
  const float dx = hL - hR;
  const float dz = hD - hU;
  const float gradLen = std::sqrt(dx * dx + dz * dz);

  SampledPoint result;
  result.height = terrain.GetHeight(worldX, worldZ);
  if (gradLen > 1e-5f) {
    result.downhill = {dx / gradLen, dz / gradLen};
  }

  // 勾配(高さ差/水平距離)をmaxSlopeで正規化
  const float slopeRatio = gradLen / (2.0f * d);
  const float maxSlope = (std::max)(config.maxSlope, 1e-4f);
  result.slope01 = std::clamp(slopeRatio / maxSlope, 0.0f, 1.0f);
  return result;
}

XMFLOAT4 SlopeVisualizationMeshBuilder::SlopeToColor(float slope01) {
  // 緩やか=青、急=赤の線形補間（既存コードの手動lerp慣習に合わせる）
  const XMFLOAT3 kLow  = {0.20f, 0.45f, 0.95f};
  const XMFLOAT3 kHigh = {0.95f, 0.20f, 0.15f};
  const float r = kLow.x + (kHigh.x - kLow.x) * slope01;
  const float g = kLow.y + (kHigh.y - kLow.y) * slope01;
  const float b = kLow.z + (kHigh.z - kLow.z) * slope01;
  return {r, g, b, slope01};
}

SlopeVisualizationMeshBuilder::BuildResult SlopeVisualizationMeshBuilder::Build(
    const WikiTerrainSystem &terrain, const XMFLOAT3 &center,
    const SlopeOverlayConfig &config) {
  BuildResult result;

  const float cellSize = (std::max)(config.cellSize, 0.05f);
  const int half = (std::max)(1, static_cast<int>(config.radius / cellSize));
  const int res = half * 2 + 1; // 1辺あたりの頂点数

  result.vertices.reserve(static_cast<size_t>(res) * static_cast<size_t>(res));
  result.indices.reserve(static_cast<size_t>(res - 1) * static_cast<size_t>(res - 1) * 6);

  for (int gz = 0; gz < res; ++gz) {
    const float worldZ = center.z + static_cast<float>(gz - half) * cellSize;
    for (int gx = 0; gx < res; ++gx) {
      const float worldX = center.x + static_cast<float>(gx - half) * cellSize;

      const SampledPoint sp = SamplePoint(terrain, worldX, worldZ, config);

      graphics::Vertex v;
      v.position  = {worldX, sp.height + config.heightOffset, worldZ};
      v.normal    = {0.0f, 1.0f, 0.0f};
      v.texCoord  = {0.0f, 0.0f};
      v.color     = SlopeToColor(sp.slope01);
      v.tangent   = {sp.downhill.x, 0.0f, sp.downhill.y};
      v.bitangent = {0.0f, 1.0f, 0.0f};
      result.vertices.push_back(v);
    }
  }

  // 頂点は「Z添字が増えるとworldZも増える」向きで並べているため、
  // 地形本体メッシュ(WikiTerrainBuild.cpp、Z添字が増えるとworldZが減る向き)と
  // 巻き順を一致させるには三角形の頂点順序を反転させる必要がある。
  // これをしないと裏面カリングで完全に非表示になる。
  for (int gz = 0; gz < res - 1; ++gz) {
    for (int gx = 0; gx < res - 1; ++gx) {
      const uint32_t i0 = static_cast<uint32_t>(gz * res + gx);
      const uint32_t i1 = static_cast<uint32_t>(gz * res + gx + 1);
      const uint32_t i2 = static_cast<uint32_t>((gz + 1) * res + gx);
      const uint32_t i3 = static_cast<uint32_t>((gz + 1) * res + gx + 1);
      result.indices.insert(result.indices.end(), {i0, i2, i1, i1, i2, i3});
    }
  }

  return result;
}

} // namespace game::systems
