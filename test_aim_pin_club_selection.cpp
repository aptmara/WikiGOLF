#include "src/game/utils/AimPinClubSelection.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

#define CHECK_TRUE(condition, message)                                        \
  do {                                                                        \
    if (!(condition)) {                                                       \
      std::cerr << "[FAIL] " << message << "\n";                            \
      std::exit(1);                                                           \
    }                                                                         \
  } while (false)

int main() {
  using game::utils::AimPinClubCandidate;
  using game::utils::HasAbnormalSlopeBetween;
  using game::utils::SelectAimPinClubIndex;

  const std::vector<AimPinClubCandidate> boundaryClubs = {
      {0.7001f, 130.0f, false}, {0.70f, 115.0f, false},
      {0.4001f, 20.0f, true}, {0.40f, 15.0f, true}};
  CHECK_TRUE(SelectAimPinClubIndex(boundaryClubs, false) == 1,
             "70 percent is eligible and values above it are excluded");

  const std::vector<AimPinClubCandidate> putterClubs = {
      {0.30f, 60.0f, false}, {0.39f, 15.0f, true}};
  CHECK_TRUE(SelectAimPinClubIndex(putterClubs, false) == 1,
             "putter is eligible at or below 40 percent on a safe path");
  CHECK_TRUE(SelectAimPinClubIndex(putterClubs, true) == 0,
             "abnormal slope excludes the putter");

  const std::vector<AimPinClubCandidate> noCandidates = {
      {0.90f, 130.0f, false}, {0.80f, 115.0f, false},
      {0.50f, 15.0f, true}};
  CHECK_TRUE(SelectAimPinClubIndex(noCandidates, false) == 0,
             "longest regular club is the fallback");

  const DirectX::XMFLOAT3 from{0.0f, 0.0f, 0.0f};
  const DirectX::XMFLOAT3 to{10.0f, 0.0f, 0.0f};
  CHECK_TRUE(!HasAbnormalSlopeBetween(
                 from, to, [](float x, float) { return x * 0.12f; }),
             "12 percent slope is accepted");
  CHECK_TRUE(HasAbnormalSlopeBetween(
                 from, to, [](float x, float) { return x * 0.121f; }),
             "slope above 12 percent is abnormal");

  std::cout << "All aim pin club selection tests passed.\n";
  return 0;
}
