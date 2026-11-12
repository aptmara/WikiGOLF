// Release ビルドでも assert で確かめるため、NDEBUG を外してから読み込む。
#undef NDEBUG
#include "game/systems/DailyChallengeRules.h"
#include "game/utils/ArticleWind.h"

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

  using game::systems::ClassifyDailyStartHops;
  using game::systems::DailyStartFit;
  assert(ClassifyDailyStartHops(1) == DailyStartFit::Reject);
  assert(ClassifyDailyStartHops(2) == DailyStartFit::Fallback);
  assert(ClassifyDailyStartHops(3) == DailyStartFit::Preferred);
  assert(ClassifyDailyStartHops(5) == DailyStartFit::Preferred);
  assert(ClassifyDailyStartHops(6) == DailyStartFit::Reject);

  // 同じシードなら同じ記事、Preferred を優先する
  using game::systems::PickDailyStartArticle;
  const auto everyTenthPreferred = [](std::uint32_t article) {
    if (article % 10 == 0) return DailyStartFit::Preferred;
    if (article % 3 == 0) return DailyStartFit::Fallback;
    return DailyStartFit::Reject;
  };
  const std::uint32_t pickA =
      PickDailyStartArticle(20260910u, 1000u, everyTenthPreferred);
  const std::uint32_t pickB =
      PickDailyStartArticle(20260910u, 1000u, everyTenthPreferred);
  assert(pickA == pickB);
  assert(pickA < 1000u && pickA % 10 == 0);

  const auto onlyFallback = [](std::uint32_t article) {
    return article == 7 ? DailyStartFit::Fallback : DailyStartFit::Reject;
  };
  assert(PickDailyStartArticle(1u, 8u, onlyFallback) == 7u);
  const auto none = [](std::uint32_t) { return DailyStartFit::Reject; };
  assert(PickDailyStartArticle(1u, 8u, none) == 8u);
  assert(PickDailyStartArticle(1u, 0u, none) == 0u);

  using game::systems::HashDailyArticle;
  assert(HashDailyArticle(20260910u, "日本") == HashDailyArticle(20260910u, "日本"));
  assert(HashDailyArticle(20260910u, "日本") != HashDailyArticle(20260911u, "日本"));
  assert(HashDailyArticle(20260910u, "日本") != HashDailyArticle(20260910u, "東京"));

  using game::systems::DailyPracticeReason;
  using game::systems::DecideDailyPracticeReason;
  assert(DecideDailyPracticeReason(0u, 20260910u, false) ==
         DailyPracticeReason::None);
  assert(DecideDailyPracticeReason(20260909u, 20260910u, false) ==
         DailyPracticeReason::None);
  assert(DecideDailyPracticeReason(20260910u, 20260910u, false) ==
         DailyPracticeReason::AlreadyPlayedToday);
  assert(DecideDailyPracticeReason(0u, 20260910u, true) ==
         DailyPracticeReason::SafeMode);
  assert(DecideDailyPracticeReason(0u, 0u, false) ==
         DailyPracticeReason::UnknownDate);

  // デイリーの風: 同じ日・同じ記事なら同じ、短い記事は無風
  using game::utils::ComputeArticleWind;
  const auto windA = ComputeArticleWind(3000, 20260910u, "日本");
  const auto windB = ComputeArticleWind(3000, 20260910u, "日本");
  assert(windA.speed == windB.speed && windA.directionX == windB.directionX &&
         windA.directionY == windB.directionY);
  assert(windA.speed >= 3.0f && windA.speed < 5.0f);
  assert(ComputeArticleWind(100, 20260910u, "日本").speed == 0.0f);
  return 0;
}
