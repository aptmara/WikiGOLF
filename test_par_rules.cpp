#include "src/game/utils/ParRules.h"
#include <iostream>

#define CHECK(condition, message)                                              \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << "[FAIL] " << message << "\n";                              \
      return 1;                                                                \
    }                                                                          \
    std::cout << "[PASS] " << message << "\n";                                \
  } while (0)

int main() {
  using game::utils::CalculateWikiGolfPar;

  CHECK(CalculateWikiGolfPar(0, 200) == 1,
        "Direct target link produces par 1");
  CHECK(CalculateWikiGolfPar(2, 200) == 3,
        "Resolved path uses one shot plus remaining hops");
  CHECK(CalculateWikiGolfPar(-1, 0) == 3,
        "Fallback keeps a minimum par");
  CHECK(CalculateWikiGolfPar(-1, 2000) == 12,
        "Fallback is capped for long articles");

  using game::utils::CalculateRoundPar;
  using game::utils::EstimateCourseDepthFromArticleBytes;

  CHECK(EstimateCourseDepthFromArticleBytes(-1) == 120.0f,
        "Unknown article length uses the minimum course depth");
  CHECK(EstimateCourseDepthFromArticleBytes(3000) == 120.0f,
        "Short articles keep the minimum course depth");
  CHECK(EstimateCourseDepthFromArticleBytes(100000) > 700.0f &&
            EstimateCourseDepthFromArticleBytes(100000) < 760.0f,
        "Article bytes scale to course depth");
  CHECK(EstimateCourseDepthFromArticleBytes(100000000) == 20000.0f,
        "Huge articles are capped at the maximum course depth");

  // 1打あたり 200m × 0.8 = 160m
  CHECK(CalculateRoundPar(160.0f, 200.0f, 1) == 1,
        "Exactly one stroke distance gives par 1");
  CHECK(CalculateRoundPar(161.0f, 200.0f, 1) == 2,
        "Remaining distance rounds up");
  CHECK(CalculateRoundPar(120.0f * 3.0f + 764.0f, 200.0f, 4) == 8,
        "Par sums the course depth of every article");
  CHECK(CalculateRoundPar(120.0f * 3.0f, 200.0f, 3) == 3,
        "Par is at least the number of played articles");
  CHECK(CalculateRoundPar(0.0f, 200.0f, 3) == 0,
        "Unknown course length gives no par");
  CHECK(CalculateRoundPar(500.0f, 0.0f, 3) == 0,
        "Unknown driver distance gives no par");

  std::cout << "All Par rules tests passed.\n";
  return 0;
}
