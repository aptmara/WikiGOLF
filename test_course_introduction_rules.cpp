#include "src/game/utils/CourseIntroductionRules.h"
#include <cassert>

int main() {
  using game::utils::CourseIntroductionHole;
  using game::utils::FormatCourseAbstract;
  using game::utils::SelectFeaturedCourseHoles;

  assert(FormatCourseAbstract(L"  最初の  文です。\n続きです。\n\n次の段落") ==
         L"最初の 文です。 続きです。");
  assert(FormatCourseAbstract(L"概要です。\n== 表・インフォボックス ==\n値") ==
         L"概要です。");
  assert(FormatCourseAbstract(L"1234567890", 6) == L"12345…");
  assert(FormatCourseAbstract(
             L"[[ゴルフ|競技]]は''球技''です。{{読み仮名|不要}}") ==
         L"競技は球技です。");

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

  return 0;
}
