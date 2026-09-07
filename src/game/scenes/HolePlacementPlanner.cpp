/**
 * @file HolePlacementPlanner.cpp
 * @brief ホール配置計画の実装
 */

#include "HolePlacementPlanner.h"
#include "../../core/Logger.h"
#include <algorithm>

namespace game::scenes {
namespace {

constexpr float kMinimumMapIconDistance = 8.0f;
constexpr std::size_t kMaximumMapHoleIcons = 160;

} // namespace

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
  std::vector<HolePlacementCandidate> sorted = candidates;
  std::stable_sort(
      sorted.begin(), sorted.end(),
      [](const HolePlacementCandidate &left,
         const HolePlacementCandidate &right) {
        if (left.isTarget != right.isTarget) {
          return left.isTarget;
        }
        return left.originalIndex < right.originalIndex;
      });

  std::vector<HolePlacementCandidate> selected;
  selected.reserve(std::min(sorted.size(), kMaximumMapHoleIcons));
  for (const HolePlacementCandidate &candidate : sorted) {
    if (candidate.isTarget || IsFarEnoughFromSelected(
                                  selected, candidate,
                                  kMinimumMapIconDistance)) {
      selected.push_back(candidate);
    }
    if (selected.size() >= kMaximumMapHoleIcons) {
      break;
    }
  }

  std::sort(selected.begin(), selected.end(),
            [](const HolePlacementCandidate &left,
               const HolePlacementCandidate &right) {
              return left.originalIndex < right.originalIndex;
            });
  if (selected.size() != candidates.size()) {
    LOG_INFO("WikiPageLoader",
             "Map link icons filtered: candidates={}, selected={}, "
             "minDistance={:.1f}",
             candidates.size(), selected.size(), kMinimumMapIconDistance);
  }
  return selected;
}

bool HolePlacementPlanner::IsFarEnoughFromSelected(
    const std::vector<HolePlacementCandidate> &selected,
    const HolePlacementCandidate &candidate, float minDistance) const {
  const float minimumDistanceSquared = minDistance * minDistance;
  return std::none_of(
      selected.begin(), selected.end(),
      [&](const HolePlacementCandidate &existing) {
        const float deltaX = candidate.x - existing.x;
        const float deltaZ = candidate.z - existing.z;
        return deltaX * deltaX + deltaZ * deltaZ < minimumDistanceSquared;
      });
}

} // namespace game::scenes
