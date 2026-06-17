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

  std::cout << "All Par rules tests passed.\n";
  return 0;
}
