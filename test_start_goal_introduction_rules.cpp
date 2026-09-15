#include "src/game/utils/StartGoalIntroductionRules.h"
#include <cassert>
#include <vector>

int main() {
  using game::utils::CalculateStartGoalRevealProgress;
  using game::utils::FormatWikipediaArticleUrl;
  using game::utils::ResolveStartGoalAdvance;
  using game::utils::SelectStartGoalTitleFontSize;
  using game::utils::StartGoalAdvance;
  using Timing = game::utils::StartGoalIntroductionTiming;

  assert(CalculateStartGoalRevealProgress(0.0f, 1.0f) == 0.0f);
  assert(CalculateStartGoalRevealProgress(1.0f + Timing::kRevealDuration,
                                          1.0f) == 1.0f);
  const float halfway = CalculateStartGoalRevealProgress(
      1.0f + Timing::kRevealDuration * 0.5f, 1.0f);
  assert(halfway > 0.5f && halfway < 1.0f); // ease-out なので前半で大きく進む

  assert(Timing::kHeaderDelay < Timing::kStartCardDelay);
  assert(Timing::kStartCardDelay < Timing::kArrowDelay);
  assert(Timing::kArrowDelay < Timing::kGoalCardDelay);
  assert(Timing::kGoalCardDelay < Timing::kHintDelay);
  assert(Timing::FullyRevealedTime() < Timing::kAutoAdvanceTime);

  assert(ResolveStartGoalAdvance(0.5f) == StartGoalAdvance::RevealAll);
  assert(ResolveStartGoalAdvance(Timing::FullyRevealedTime()) ==
         StartGoalAdvance::FadeOut);

  assert(SelectStartGoalTitleFontSize(4) == 32.0f);
  assert(SelectStartGoalTitleFontSize(16) == 27.0f);
  assert(SelectStartGoalTitleFontSize(20) == 22.0f);
  assert(SelectStartGoalTitleFontSize(40) == 19.0f);

  using game::utils::CalculateStartToGoalHops;
  using game::utils::FitStartGoalBody;

  // 余裕があれば標準サイズのまま
  const auto roomy = FitStartGoalBody(
      [](float, std::size_t) { return 100.0f; }, 200.0f);
  assert(roomy.fits && roomy.fontSize == 15.0f && roomy.maxCharacters == 180);

  // 文字サイズを下げれば収まる場合は、文字数を削らない
  const auto smaller = FitStartGoalBody(
      [](float fontSize, std::size_t) { return fontSize * 14.0f; }, 190.0f);
  assert(smaller.fits && smaller.fontSize == 13.0f &&
         smaller.maxCharacters == 180);

  // 最小サイズでも溢れるなら文字数を削る
  const auto trimmed = FitStartGoalBody(
      [](float, std::size_t characters) {
        return static_cast<float>(characters);
      },
      125.0f);
  assert(trimmed.fits && trimmed.fontSize == 13.0f &&
         trimmed.maxCharacters == 120);

  const auto overflow = FitStartGoalBody(
      [](float, std::size_t) { return 999.0f; }, 10.0f);
  assert(!overflow.fits);

  assert(CalculateStartToGoalHops(std::vector<int>{3, 1, -1, 2}) == 2);
  assert(CalculateStartToGoalHops(std::vector<int>{0, 4}) == 1);
  assert(CalculateStartToGoalHops(std::vector<int>{-1, -1}) == 0);

  assert(FormatWikipediaArticleUrl(L"New York City") ==
         L"ja.wikipedia.org/wiki/New_York_City");
  assert(FormatWikipediaArticleUrl(L"1234567890", 6) ==
         L"ja.wikipedia.org/wiki/12345…");

  return 0;
}
