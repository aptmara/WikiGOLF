#include "src/game/utils/CourseIntroductionRules.h"
#include <cassert>

int main() {
  using game::utils::CourseIntroductionHole;
  using game::utils::CalculateCourseIntroductionDuration;
  using game::utils::SelectFeaturedCourseHoles;

  const std::vector<CourseIntroductionHole> holes = {
      {0.0f, 0.0f, 10.0f, "goal", true, 0, 0},
      {0.0f, 0.0f, 2.0f, "near", false, 1, 1},
      {0.0f, 0.0f, 20.0f, "near", false, 1, 2},
      {-20.0f, 0.0f, 20.0f, "left", false, 1, 3},
      {20.0f, 0.0f, 20.0f, "right", false, 1, 4},
      {0.0f, 0.0f, 30.0f, "two-hop", false, 2, 5},
  };
  const auto featured = SelectFeaturedCourseHoles(holes, 0.0f, 0.0f, 2);
  assert(featured.goals.size() == 1);
  assert(featured.goals[0].linkTarget == "goal");
  assert(featured.oneHop.size() == 2);
  assert(featured.oneHop[0].linkTarget == "near");
  assert(featured.oneHop[0].z == 2.0f);
  assert(featured.oneHop[1].linkTarget != "near");

  const std::vector<CourseIntroductionHole> manyGoodHoles = {
      {1.0f, 0.0f, 0.0f, "near", false, 1, 0},
      {100.0f, 0.0f, 0.0f, "far-right", false, 1, 1},
      {2.0f, 0.0f, 0.0f, "next", false, 1, 2},
      {-100.0f, 0.0f, 0.0f, "far-left", false, 1, 3},
      {3.0f, 0.0f, 0.0f, "third", false, 1, 4},
  };
  const auto nearbyFirst =
      SelectFeaturedCourseHoles(manyGoodHoles, 0.0f, 0.0f, 5);
  assert(nearbyFirst.oneHop.size() == 5);
  assert(nearbyFirst.oneHop[0].linkTarget == "near");
  assert(nearbyFirst.oneHop[1].linkTarget == "next");
  assert(nearbyFirst.oneHop[2].linkTarget == "third");

  const DirectX::XMFLOAT3 origin = {0.0f, 0.0f, 0.0f};
  assert(CalculateCourseIntroductionDuration(
             origin, {10.0f, 0.0f, 0.0f}, 3.0f) == 3.0f);
  assert(CalculateCourseIntroductionDuration(
             origin, {700.0f, 0.0f, 0.0f}, 3.0f) == 5.0f);
  assert(CalculateCourseIntroductionDuration(
             origin, {10000.0f, 0.0f, 0.0f}, 3.0f) == 8.0f);

  return 0;
}
