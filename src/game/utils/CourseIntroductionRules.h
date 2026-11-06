#pragma once

#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace game::utils {

struct CourseIntroductionHole {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  std::string linkTarget;
  bool isTarget = false;
  int hopsToTarget = -1;
  std::size_t originalIndex = 0;
};

struct FeaturedCourseHoles {
  std::vector<CourseIntroductionHole> goals;
  std::vector<CourseIntroductionHole> oneHop;
};

inline float CalculateCourseIntroductionDuration(
    const DirectX::XMFLOAT3 &start, const DirectX::XMFLOAT3 &end,
    float minimumDuration) {
  constexpr float kTravelSpeed = 140.0f;
  constexpr float kMaximumDuration = 8.0f;
  const float dx = end.x - start.x;
  const float dy = end.y - start.y;
  const float dz = end.z - start.z;
  const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
  return std::clamp(distance / kTravelSpeed, minimumDuration,
                    kMaximumDuration);
}

inline FeaturedCourseHoles SelectFeaturedCourseHoles(
    const std::vector<CourseIntroductionHole> &holes, float teeX, float teeZ,
    std::size_t maximumOneHop = 5) {
  FeaturedCourseHoles result;
  std::unordered_map<std::string, CourseIntroductionHole> nearestByTarget;

  auto distanceSquared = [](float leftX, float leftZ, float rightX,
                            float rightZ) {
    const float dx = leftX - rightX;
    const float dz = leftZ - rightZ;
    return dx * dx + dz * dz;
  };

  for (const CourseIntroductionHole &hole : holes) {
    if (hole.isTarget) {
      result.goals.push_back(hole);
      continue;
    }
    if (hole.hopsToTarget != 1 || hole.linkTarget.empty()) {
      continue;
    }

    const auto existing = nearestByTarget.find(hole.linkTarget);
    if (existing == nearestByTarget.end() ||
        distanceSquared(hole.x, hole.z, teeX, teeZ) <
            distanceSquared(existing->second.x, existing->second.z, teeX,
                            teeZ)) {
      nearestByTarget[hole.linkTarget] = hole;
    }
  }

  std::vector<CourseIntroductionHole> candidates;
  candidates.reserve(nearestByTarget.size());
  for (const auto &[target, hole] : nearestByTarget) {
    candidates.push_back(hole);
  }
  std::sort(candidates.begin(), candidates.end(),
            [](const CourseIntroductionHole &left,
               const CourseIntroductionHole &right) {
              return left.originalIndex < right.originalIndex;
            });

  while (!candidates.empty() && result.oneHop.size() < maximumOneHop) {
    std::size_t selectedIndex = 0;
    float selectedScore = std::numeric_limits<float>::lowest();
    for (std::size_t index = 0; index < candidates.size(); ++index) {
      float score = distanceSquared(candidates[index].x, candidates[index].z,
                                    teeX, teeZ);
      if (result.oneHop.empty()) {
        score = -score;
      } else {
        score = std::numeric_limits<float>::max();
        for (const CourseIntroductionHole &selected : result.oneHop) {
          score = std::min(score,
                           distanceSquared(candidates[index].x,
                                           candidates[index].z, selected.x,
                                           selected.z));
        }
      }
      if (score > selectedScore) {
        selectedScore = score;
        selectedIndex = index;
      }
    }
    result.oneHop.push_back(candidates[selectedIndex]);
    candidates.erase(candidates.begin() + selectedIndex);
  }

  if (result.oneHop.size() >= 5) {
    std::vector<CourseIntroductionHole> ordered;
    ordered.reserve(result.oneHop.size());
    float currentX = teeX;
    float currentZ = teeZ;
    while (!result.oneHop.empty()) {
      const auto nearest = std::min_element(
          result.oneHop.begin(), result.oneHop.end(),
          [&](const CourseIntroductionHole &left,
              const CourseIntroductionHole &right) {
            const float leftDistance =
                distanceSquared(left.x, left.z, currentX, currentZ);
            const float rightDistance =
                distanceSquared(right.x, right.z, currentX, currentZ);
            if (leftDistance == rightDistance) {
              return left.originalIndex < right.originalIndex;
            }
            return leftDistance < rightDistance;
          });
      currentX = nearest->x;
      currentZ = nearest->z;
      ordered.push_back(*nearest);
      result.oneHop.erase(nearest);
    }
    result.oneHop = std::move(ordered);
  }

  return result;
}

} // namespace game::utils
