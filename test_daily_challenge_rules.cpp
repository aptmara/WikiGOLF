#include "game/systems/DailyChallengeRules.h"

#include <cassert>
#include <cstddef>

int main() {
  using game::systems::DailyChallengeRandom;
  using game::systems::MakeDailyChallengeSeed;
  using game::systems::ParseDailyChallengeSeed;

  assert(MakeDailyChallengeSeed(2026, 9, 10) == 20260910u);
  assert(MakeDailyChallengeSeed(2026, 9, 10) !=
         MakeDailyChallengeSeed(2026, 9, 11));
  assert(ParseDailyChallengeSeed("2026-09-10T12:34:56Z") == 20260910u);
  assert(ParseDailyChallengeSeed("invalid") == 0u);
  assert(ParseDailyChallengeSeed("2026-13-10T12:34:56Z") == 0u);

  DailyChallengeRandom first(20260910u);
  DailyChallengeRandom second(20260910u);
  DailyChallengeRandom nextDay(20260911u);
  bool hasDifferentValue = false;

  for (int i = 0; i < 16; ++i) {
    const std::size_t firstIndex = first.NextIndex(2000);
    const std::size_t secondIndex = second.NextIndex(2000);
    const std::size_t nextDayIndex = nextDay.NextIndex(2000);
    assert(firstIndex == secondIndex);
    assert(firstIndex < 2000);
    assert(nextDayIndex < 2000);
    hasDifferentValue = hasDifferentValue || firstIndex != nextDayIndex;
  }

  assert(hasDifferentValue);
  return 0;
}
