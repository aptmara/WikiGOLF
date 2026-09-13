/**
 * @file HolePlacementPlanner.cpp
 * @brief ホール配置計画の実装
*/

#include "HolePlacementPlanner.h"

namespace game::scenes {

HolePlacementCandidate HolePlacementPlanner::BuildCandidate(
    const graphics::LinkRegion &link, std::size_t originalIndex,
    std::uint32_t textureWidth, std::uint32_t textureHeight, float fieldWidth,
    float fieldDepth) const {
  HolePlacementCandidate candidate;
  if (textureWidth == 0 || textureHeight == 0) {
    return candidate;
  }

  const float width = static_cast<float>(textureWidth);
  const float height = static_cast<float>(textureHeight);
  const float centerX = link.x + link.width * 0.5f;
  const float centerY = link.y + link.height * 0.5f;
  candidate.x = (centerX / width - 0.5f) * fieldWidth;
  candidate.z = (0.5f - centerY / height) * fieldDepth;
  candidate.linkTarget = link.targetPage;
  candidate.isTarget = link.isTarget;
  candidate.originalIndex = originalIndex;
  return candidate;
}

std::vector<HolePlacementCandidate> HolePlacementPlanner::SelectMapCandidates(
    const std::vector<HolePlacementCandidate> &candidates) const {
  return candidates;
}

} // namespace game::scenes
