#pragma once

#include <algorithm>

namespace game::scenes {

struct HtmlCourseFieldSize {
  float width;
  float depth;
};

inline constexpr float kHtmlCourseFieldWidth = 80.0f;
inline constexpr float kHtmlCourseMinimumDepth = 120.0f;
inline constexpr float kHtmlCourseMaximumDepth = 20000.0f;

inline HtmlCourseFieldSize CalculateHtmlCourseFieldSize(float layoutWidth,
                                                        float layoutHeight) {
  const float depth = kHtmlCourseFieldWidth * layoutHeight / layoutWidth;
  return {kHtmlCourseFieldWidth,
          std::clamp(depth, kHtmlCourseMinimumDepth,
                     kHtmlCourseMaximumDepth)};
}

} // namespace game::scenes
