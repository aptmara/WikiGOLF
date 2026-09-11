#include "game/systems/AchievementRules.h"

#include <cassert>

int main() {
  using namespace game::systems;

  // クリア回数系
  assert(ReachedCountThreshold(10, 10));
  assert(ReachedCountThreshold(15, 10));
  assert(!ReachedCountThreshold(9, 10));

  // スコア・技術系
  assert(ShouldUnlockBirdie(3, 5));
  assert(!ShouldUnlockBirdie(4, 5));
  assert(!ShouldUnlockBirdie(0, 5));

  assert(ShouldUnlockParFirstTry(5, 5));
  assert(!ShouldUnlockParFirstTry(4, 5));

  assert(ShouldUnlockTripleSpecial(3));
  assert(ShouldUnlockTripleSpecial(5));
  assert(!ShouldUnlockTripleSpecial(2));

  assert(ShouldUnlockNoMiss(false));
  assert(!ShouldUnlockNoMiss(true));

  assert(ComputeMinimumHopsFromPar(5) == 6);
  assert(ComputeMinimumHopsFromPar(2) == 0);
  assert(ShouldUnlockShortestPath(6, 5));
  assert(!ShouldUnlockShortestPath(7, 5));
  assert(!ShouldUnlockShortestPath(0, 2));

  assert(ShouldUnlockWanderer(10));
  assert(ShouldUnlockWanderer(15));
  assert(!ShouldUnlockWanderer(9));

  // ハザード系
  assert(ShouldUnlockResilientClear(true));
  assert(!ShouldUnlockResilientClear(false));

  // 累計・進捗系
  assert(ShouldUnlockExplorer(100, 100));
  assert(ShouldUnlockExplorer(150, 100));
  assert(!ShouldUnlockExplorer(99, 100));

  // 自己ベスト系
  assert(IsNewBestStrokes(5, 0));   // 未記録は常に更新
  assert(IsNewBestStrokes(4, 5));
  assert(!IsNewBestStrokes(5, 5));
  assert(!IsNewBestStrokes(6, 5));

  assert(IsNewBestClearTime(1000, 0));
  assert(IsNewBestClearTime(900, 1000));
  assert(!IsNewBestClearTime(1000, 1000));

  // ISO日付パース
  int year = 0, month = 0, day = 0;
  assert(ParseIsoDate("2026-09-10", year, month, day));
  assert(year == 2026 && month == 9 && day == 10);
  assert(!ParseIsoDate("invalid", year, month, day));
  assert(!ParseIsoDate("2026-13-10", year, month, day));

  // 通算日数変換（月・年境界をまたぐケースを検証）
  assert(DaysFromCivil(2026, 9, 10) + 1 == DaysFromCivil(2026, 9, 11));
  assert(DaysFromCivil(2026, 1, 31) + 1 == DaysFromCivil(2026, 2, 1));
  assert(DaysFromCivil(2026, 12, 31) + 1 == DaysFromCivil(2027, 1, 1));
  assert(DaysFromCivil(2024, 2, 28) + 1 == DaysFromCivil(2024, 2, 29)); // 閏年

  // 連続プレイ日数
  assert(ComputeDailyStreak("", "2026-09-10", 0) == 1);
  assert(ComputeDailyStreak("2026-09-10", "2026-09-10", 3) == 3); // 同日は維持
  assert(ComputeDailyStreak("2026-09-09", "2026-09-10", 3) == 4); // 翌日は+1
  assert(ComputeDailyStreak("2026-09-01", "2026-09-10", 3) == 1); // 途切れたらリセット
  assert(ComputeDailyStreak("2026-01-31", "2026-02-01", 2) == 3); // 月境界の連続

  return 0;
}
