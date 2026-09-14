/**
 * @file SlopeVisualizationMeshBuilder.cpp
 * @brief SlopeVisualizationMeshBuilderの実装です。
*/

#include "SlopeVisualizationMeshBuilder.h"
#include "../utils/TerrainBounds.h"
#include <algorithm>
#include <cmath>

namespace game::systems {

using namespace DirectX;

SlopeVisualizationMeshBuilder::SampledPoint
SlopeVisualizationMeshBuilder::SamplePoint(const TerrainData &terrain,
                                           float worldX, float worldZ,
                                           const SlopeOverlayConfig &config) {
  const float d = (std::max)(config.sampleOffset, 0.05f);
  const auto bounds = game::utils::CalculateTerrainWorldBounds(terrain);
  const float leftX = std::clamp(worldX - d, bounds.minX, bounds.maxX);
  const float rightX = std::clamp(worldX + d, bounds.minX, bounds.maxX);
  const float downZ = std::clamp(worldZ - d, bounds.minZ, bounds.maxZ);
  const float upZ = std::clamp(worldZ + d, bounds.minZ, bounds.maxZ);
  const float hL = game::utils::SampleTerrainHeight(terrain, leftX, worldZ);
  const float hR = game::utils::SampleTerrainHeight(terrain, rightX, worldZ);
  const float hD = game::utils::SampleTerrainHeight(terrain, worldX, downZ);
  const float hU = game::utils::SampleTerrainHeight(terrain, worldX, upZ);

  // 高さが下がる方向（hL > hR なら +x 方向へ下る）
  const float sampleSpanX = (std::max)(rightX - leftX, 1e-5f);
  const float sampleSpanZ = (std::max)(upZ - downZ, 1e-5f);
  const float dx = (hL - hR) / sampleSpanX;
  const float dz = (hD - hU) / sampleSpanZ;
  const float gradLen = std::sqrt(dx * dx + dz * dz);

  SampledPoint result;
  result.height = game::utils::SampleTerrainHeight(terrain, worldX, worldZ);
  if (gradLen > 1e-5f) {
    result.downhill = {dx / gradLen, dz / gradLen};
  }

  // 勾配(高さ差/水平距離)をmaxSlopeで正規化
  const float slopeRatio = gradLen;
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
    const TerrainData &terrain, const XMFLOAT3 &center,
    const SlopeOverlayConfig &config) {
  BuildResult result;

  if (!game::utils::HasValidTerrainGrid(terrain)) {
    return result;
  }

  const float cellSize = (std::max)(config.cellSize, 0.05f);
  const int half = (std::max)(1, static_cast<int>(config.radius / cellSize));
  const auto bounds = game::utils::CalculateTerrainWorldBounds(terrain);
  const int minOffsetX = (std::max)(
      -half, static_cast<int>(std::ceil((bounds.minX - center.x) / cellSize)));
  const int maxOffsetX = (std::min)(
      half, static_cast<int>(std::floor((bounds.maxX - center.x) / cellSize)));
  const int minOffsetZ = (std::max)(
      -half, static_cast<int>(std::ceil((bounds.minZ - center.z) / cellSize)));
  const int maxOffsetZ = (std::min)(
      half, static_cast<int>(std::floor((bounds.maxZ - center.z) / cellSize)));
  const int resX = maxOffsetX - minOffsetX + 1;
  const int resZ = maxOffsetZ - minOffsetZ + 1;
  if (resX < 2 || resZ < 2) {
    return result;
  }

  result.vertices.reserve(static_cast<size_t>(resX) * static_cast<size_t>(resZ));
  result.indices.reserve(static_cast<size_t>(resX - 1) *
                         static_cast<size_t>(resZ - 1) * 6);

  for (int gz = 0; gz < resZ; ++gz) {
    const float worldZ = center.z +
        static_cast<float>(minOffsetZ + gz) * cellSize;
    for (int gx = 0; gx < resX; ++gx) {
      const float worldX = center.x +
          static_cast<float>(minOffsetX + gx) * cellSize;

      const SampledPoint sp = SamplePoint(terrain, worldX, worldZ, config);

      // 中心からの正規化距離[0,1超も含む]。ピクセルシェーダー側で
      // これを使って円形にフェードアウトさせ、円形オーバーレイに見せる。
      const float dx = worldX - center.x;
      const float dz = worldZ - center.z;
      const float distNorm = std::sqrt(dx * dx + dz * dz) / (std::max)(config.radius, 0.01f);

      graphics::Vertex v;
      v.position  = {worldX, sp.height + config.heightOffset, worldZ};
      v.normal    = {0.0f, 1.0f, 0.0f};
      v.texCoord  = {distNorm, 0.0f};
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
  for (int gz = 0; gz < resZ - 1; ++gz) {
    for (int gx = 0; gx < resX - 1; ++gx) {
      const uint32_t i0 = static_cast<uint32_t>(gz * resX + gx);
      const uint32_t i1 = static_cast<uint32_t>(gz * resX + gx + 1);
      const uint32_t i2 = static_cast<uint32_t>((gz + 1) * resX + gx);
      const uint32_t i3 = static_cast<uint32_t>((gz + 1) * resX + gx + 1);
      result.indices.insert(result.indices.end(), {i0, i2, i1, i1, i2, i3});
    }
  }

  return result;
}

} // namespace game::systems
