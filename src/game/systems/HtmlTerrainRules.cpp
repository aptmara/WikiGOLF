#include "HtmlTerrainRules.h"

#include <algorithm>
#include <cmath>

namespace game::systems {
namespace {

float SmoothCoverage(float distance) {
  const float t = 1.0f - std::clamp(distance, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

bool HasTerrainStorage(int resolutionX, int resolutionZ,
                       const std::vector<float> &heights,
                       const std::vector<std::uint8_t> &materials) {
  if (resolutionX < 2 || resolutionZ < 2) {
    return false;
  }

  const auto expectedSize =
      static_cast<std::size_t>(resolutionX) * resolutionZ;
  return heights.size() == expectedSize && materials.size() == expectedSize;
}

} // namespace

void ApplyHtmlTerrainLayout(
    int resolutionX, int resolutionZ, float worldWidth, float worldDepth,
    float heightScale, const std::vector<HtmlTerrainRegion> &regions,
    std::vector<float> &heights, std::vector<std::uint8_t> &materials) {
  if (worldWidth <= 0.0f || worldDepth <= 0.0f ||
      !HasTerrainStorage(resolutionX, resolutionZ, heights, materials)) {
    return;
  }

  const float featherU =
      std::max(4.0f / worldWidth, 2.0f / (resolutionX - 1));
  const float featherV =
      std::max(4.0f / worldDepth, 2.0f / (resolutionZ - 1));

  for (const auto &region : regions) {
    const int left = std::clamp(
        static_cast<int>((region.u - featherU) * (resolutionX - 1)), 0,
        resolutionX - 1);
    const int right = std::clamp(
        static_cast<int>(std::ceil((region.u + region.width + featherU) *
                                   (resolutionX - 1))),
        0, resolutionX - 1);
    const int top = std::clamp(
        static_cast<int>((region.v - featherV) * (resolutionZ - 1)), 0,
        resolutionZ - 1);
    const int bottom = std::clamp(
        static_cast<int>(std::ceil((region.v + region.height + featherV) *
                                   (resolutionZ - 1))),
        0, resolutionZ - 1);

    for (int z = top; z <= bottom; ++z) {
      for (int x = left; x <= right; ++x) {
        const float u = static_cast<float>(x) / (resolutionX - 1);
        const float v = static_cast<float>(z) / (resolutionZ - 1);
        const float du =
            std::max({region.u - u, 0.0f, u - region.u - region.width}) /
            featherU;
        const float dv =
            std::max({region.v - v, 0.0f, v - region.v - region.height}) /
            featherV;
        const float coverage = SmoothCoverage(std::max(du, dv));
        const auto index = static_cast<std::size_t>(z) * resolutionX + x;
        const float relief = region.heading ? 0.10f : 0.24f;
        heights[index] += relief * heightScale * coverage;

        const bool insideRegion = du == 0.0f && dv == 0.0f;
        const bool playableGround = materials[index] <= 1;
        if (!region.heading && insideRegion && playableGround) {
          materials[index] = 1;
        }
      }
    }
  }
}

} // namespace game::systems
