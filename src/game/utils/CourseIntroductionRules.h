#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cwctype>
#include <limits>
#include <string>
#include <string_view>
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

inline std::wstring FormatCourseAbstract(std::wstring_view source,
                                         std::size_t maxCharacters = 180) {
  if (maxCharacters == 0) {
    return {};
  }

  std::size_t end = source.size();
  const std::size_t paragraphEnd = source.find(L"\n\n");
  if (paragraphEnd != std::wstring_view::npos) {
    end = std::min(end, paragraphEnd);
  }
  const std::size_t tableSection = source.find(L"\n==");
  if (tableSection != std::wstring_view::npos) {
    end = std::min(end, tableSection);
  }

  std::wstring normalized;
  normalized.reserve(std::min(end, maxCharacters));
  bool pendingSpace = false;
  for (std::size_t index = 0; index < end; ++index) {
    if (index + 1 < end && source[index] == L'[' &&
        source[index + 1] == L'[') {
      const std::size_t close = source.find(L"]]", index + 2);
      if (close != std::wstring_view::npos && close < end) {
        const std::wstring_view link = source.substr(index + 2,
                                                      close - index - 2);
        const std::size_t separator = link.find_last_of(L'|');
        const std::wstring_view label = separator == std::wstring_view::npos
                                            ? link
                                            : link.substr(separator + 1);
        if (pendingSpace && !normalized.empty()) {
          normalized.push_back(L' ');
          pendingSpace = false;
        }
        normalized.append(label);
        index = close + 1;
        continue;
      }
    }
    if (index + 1 < end && source[index] == L'{' &&
        source[index + 1] == L'{') {
      const std::size_t close = source.find(L"}}", index + 2);
      if (close != std::wstring_view::npos && close < end) {
        index = close + 1;
        continue;
      }
    }
    const wchar_t character = source[index];
    if (std::iswspace(character)) {
      pendingSpace = !normalized.empty();
      continue;
    }
    if (pendingSpace) {
      normalized.push_back(L' ');
      pendingSpace = false;
    }
    if (character == L'\'') {
      continue;
    }
    normalized.push_back(character);
  }

  if (normalized.size() <= maxCharacters) {
    return normalized;
  }

  std::wstring shortened = normalized.substr(0, maxCharacters);
  const std::size_t sentenceEnd = shortened.find_last_of(L"。！？.!?");
  if (sentenceEnd != std::wstring::npos &&
      sentenceEnd >= maxCharacters / 2) {
    shortened.resize(sentenceEnd + 1);
    return shortened;
  }

  if (maxCharacters == 1) {
    return L"…";
  }
  shortened.resize(maxCharacters - 1);
  shortened.push_back(L'…');
  return shortened;
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

  return result;
}

} // namespace game::utils
