#include "src/game/utils/TutorialGuideRules.h"
#include <cmath>
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
  using game::controllers::TutorialStep;
  using namespace game::utils;

  constexpr std::size_t kTerrainCards = 5;
  constexpr std::size_t kFlagCards = 3;
  // 見出しは2行、補足は3行に収まる長さ（案内パネルの幅から決めた上限）。
  constexpr std::size_t kMaxHeadlineLength = 16;
  constexpr std::size_t kMaxDetailLength = 60;

  const auto makeState = [&](TutorialStep step, std::size_t card = 0) {
    TutorialGuideState state;
    state.step = step;
    state.cardIndex = card;
    state.terrainCardCount = kTerrainCards;
    state.flagCardCount = kFlagCards;
    if (step == TutorialStep::TerrainInfo || step == TutorialStep::FlagInfo) {
      state.cardName = L"フェアウェイ";
      state.cardDescription = L"短く刈った芝です。まっすぐ、よく転がります。";
    }
    return state;
  };

  bool textsFit = true;
  bool chaptersInOrder = true;
  bool keysFit = true;
  int previousChapter = 0;
  for (int value = 0; value <= static_cast<int>(TutorialStep::Done); ++value) {
    const auto step = static_cast<TutorialStep>(value);
    const auto content = BuildTutorialGuideContent(makeState(step));
    if (content.headline.empty() ||
        content.headline.size() > kMaxHeadlineLength ||
        content.detail.empty() || content.detail.size() > kMaxDetailLength ||
        content.chapterTitle.empty()) {
      std::cerr << "step " << value << " text does not fit\n";
      textsFit = false;
    }
    if (content.keys.size() > 3) {
      keysFit = false;
    }
    if (content.chapterIndex < previousChapter ||
        content.chapterIndex >= kTutorialChapterCount ||
        content.pageInChapter < 1 ||
        content.pageInChapter > content.pagesInChapter) {
      chaptersInOrder = false;
    }
    previousChapter = content.chapterIndex;
  }
  CHECK(textsFit, "Every step has a headline and detail that fit the panel");
  CHECK(keysFit, "No step shows more key chips than the panel can hold");
  CHECK(chaptersInOrder, "Chapters advance in order and pages stay in range");

  const auto firstTerrain =
      BuildTutorialGuideContent(makeState(TutorialStep::TerrainInfo, 0));
  const auto lastFlag =
      BuildTutorialGuideContent(makeState(TutorialStep::FlagInfo, 2));
  CHECK(firstTerrain.pageInChapter == 1 && firstTerrain.pagesInChapter == 8,
        "Terrain and flag cards share one chapter of eight pages");
  CHECK(lastFlag.pageInChapter == 8,
        "The last flag card is the last page of the chapter");
  CHECK(firstTerrain.headline == L"フェアウェイ",
        "A card shows its subject as the headline");

  const auto mapClose =
      BuildTutorialGuideContent(makeState(TutorialStep::MapClose));
  CHECK(mapClose.pageInChapter == 6 && mapClose.pagesInChapter == 6,
        "The map chapter counts each map step as a page");

  auto goalState = makeState(TutorialStep::GoalCup);
  const auto goalBefore = BuildTutorialGuideContent(goalState);
  goalState.goalCleared = true;
  const auto goalAfter = BuildTutorialGuideContent(goalState);
  CHECK(!goalBefore.celebrate && goalAfter.celebrate,
        "Reaching the goal switches to the celebration content");
  auto wrongState = makeState(TutorialStep::LinkCup);
  wrongState.wrongCupName = L"ゴール";
  const auto wrongCup = BuildTutorialGuideContent(wrongState);
  CHECK(wrongCup.detail.find(L"「ゴール」") != std::wstring::npos &&
            wrongCup.detail.find(L"フェアウェイ") != std::wstring::npos,
        "Entering the wrong cup says which cup it was and which one to aim for");
  CHECK(BuildTutorialGuideContent(makeState(TutorialStep::Club)).pageInChapter ==
                1 &&
            BuildTutorialGuideContent(makeState(TutorialStep::Aim))
                    .pageInChapter == 2,
        "The club comes before the aim pin");
  CHECK(!(goalBefore == goalAfter),
        "Changed content compares as different so the panel refreshes");

  CHECK(BuildTutorialGuideContent(makeState(TutorialStep::Power)).spotlight ==
            TutorialSpotlight::ShotRuler,
        "The power step points at the shot ruler");
  CHECK(BuildTutorialGuideContent(makeState(TutorialStep::Aim)).spotlight ==
            TutorialSpotlight::Minimap,
        "The aim step points at the minimap");

  bool spotlightsNamed = true;
  for (int value = 0; value <= static_cast<int>(TutorialStep::Done); ++value) {
    const auto content =
        BuildTutorialGuideContent(makeState(static_cast<TutorialStep>(value)));
    const bool hasLabel =
        std::wstring(GetTutorialSpotlightLabel(content.spotlight)).size() > 0;
    if (hasLabel != (content.spotlight != TutorialSpotlight::None)) {
      spotlightsNamed = false;
    }
  }
  CHECK(spotlightsNamed, "Every pointed HUD part has a name for the contents");

  CHECK(GetTutorialSectionNumber(2, 1) == L"3.1" &&
            GetTutorialSectionNumber(5, 7) == L"6.7",
        "Section numbers count chapter and page from one");

  int first = -1;
  int last = -1;
  GetTutorialTocRange(3, false, first, last);
  CHECK(first == 0 && last == kTutorialChapterCount - 1,
        "The full contents lists every chapter");
  GetTutorialTocRange(0, true, first, last);
  CHECK(first == 0 && last == 1,
        "The short contents at the first chapter shows it and the next");
  GetTutorialTocRange(kTutorialChapterCount - 1, true, first, last);
  CHECK(first == kTutorialChapterCount - 2 && last == kTutorialChapterCount - 1,
        "The short contents at the last chapter shows it and the previous");

  CHECK(GetTutorialChapterFill(0, 2, 1, 2) == 1.0f,
        "Finished chapters are fully filled");
  CHECK(GetTutorialChapterFill(5, 2, 1, 2) == 0.0f,
        "Upcoming chapters are empty");
  const float firstPage = GetTutorialChapterFill(2, 2, 1, 2);
  const float secondPage = GetTutorialChapterFill(2, 2, 2, 2);
  CHECK(firstPage > 0.0f && firstPage < secondPage && secondPage < 1.0f,
        "The current chapter fills partly and grows with each page");
  CHECK(std::abs(GetTutorialChapterFill(4, 4, 99, 8) - 7.5f / 8.0f) < 1e-5f,
        "Out-of-range pages are clamped");

  std::cout << "All tutorial guide rules tests passed.\n";
  return 0;
}
