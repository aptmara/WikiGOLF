#pragma once

#include <cstdint>
#include <vector>

namespace game::systems {

enum class HtmlRegionKind {
  Body,
  Heading,
  Hazard,
};

struct HtmlTerrainRegion {
  float u;
  float v;
  float width;
  float height;
  HtmlRegionKind kind = HtmlRegionKind::Body;
};

/**
 * @brief 生成済みの地形へHTML要素に対応した局所的な起伏を加えます。
 */
void ApplyHtmlTerrainLayout(
    int resolutionX, int resolutionZ, float worldWidth, float worldDepth,
    float heightScale, const std::vector<HtmlTerrainRegion> &regions,
    std::vector<float> &heights, std::vector<std::uint8_t> &materials);

} // namespace game::systems
