/**
 * @file test_content_filter.cpp
 * @brief 記事フィルターの判定と、カップ選択への適用を検証します。
 */

#include "src/game/scenes/PageLinkSelector.h"
#include "src/game/systems/ContentFilter.h"

#include <algorithm>
#include <cassert>
#include <sstream>
#include <string>
#include <vector>

using game::systems::ContentFilter;

namespace {

const char *const kList = "1\t100\tAlways_Blocked\n"
                          "2\t200\tSafe_Blocked\n"
                          "1\t101\t転送_記事\n"
                          "3\t300\tUnknown_Level\n"
                          "x\t400\tBad_Level\n"
                          "1\tx\tBad_Id\n"
                          "1\t500\n"
                          "\n";

void VerifyLevels() {
  ContentFilter filter;
  std::istringstream input(kList);
  assert(filter.LoadFromStream(input) == 3);

  assert(!filter.IsSafeMode());
  assert(filter.IsTitleBlocked("Always_Blocked"));
  assert(filter.IsPageIdBlocked(100));
  assert(!filter.IsTitleBlocked("Safe_Blocked"));
  assert(!filter.IsPageIdBlocked(200));
  assert(!filter.IsTitleBlocked("Other"));
  assert(!filter.IsPageIdBlocked(999));

  filter.SetSafeMode(true);
  assert(filter.IsTitleBlocked("Always_Blocked"));
  assert(filter.IsTitleBlocked("Safe_Blocked"));
  assert(filter.IsPageIdBlocked(200));
  assert(!filter.IsTitleBlocked("Other"));

  filter.SetSafeMode(false);
  assert(!filter.IsTitleBlocked("Safe_Blocked"));
}

void VerifyInvalidLinesAreIgnored() {
  ContentFilter filter;
  std::istringstream input(kList);
  filter.LoadFromStream(input);
  filter.SetSafeMode(true);
  assert(!filter.IsPageIdBlocked(300));
  assert(!filter.IsTitleBlocked("Unknown_Level"));
  assert(!filter.IsTitleBlocked("Bad_Level"));
  assert(!filter.IsTitleBlocked("Bad_Id"));
}

void VerifyTitleVariants() {
  ContentFilter filter;
  std::istringstream input(kList);
  filter.LoadFromStream(input);
  assert(filter.IsTitleBlocked("Always Blocked"));
  assert(filter.IsTitleBlocked("always blocked"));
  assert(filter.IsTitleBlocked("転送 記事"));
}

void VerifyEmptyFilterBlocksNothing() {
  ContentFilter filter;
  assert(!filter.LoadFromFile("missing_content_filter.tsv"));
  filter.SetSafeMode(true);
  assert(!filter.IsTitleBlocked("Always_Blocked"));
  assert(!filter.IsPageIdBlocked(100));
}

bool HasLink(const game::scenes::PageLinkSelectionResult &result,
             const std::string &title) {
  return std::any_of(result.links.begin(), result.links.end(),
                     [&](const auto &link) { return link.first == title; });
}

void VerifyLinkSelection() {
  auto &filter = ContentFilter::Instance();
  std::istringstream input(kList);
  filter.LoadFromStream(input);

  std::vector<game::WikiLink> links(4);
  links[0].title = "Always Blocked";
  links[1].title = "Safe Blocked";
  links[2].title = "Allowed A";
  links[3].title = "Allowed B";
  const std::string text = "Always Blocked Safe Blocked Allowed A Allowed B";

  const game::scenes::PageLinkSelector selector;
  auto normal = selector.Select(links, text, "");
  assert(!HasLink(normal, "Always Blocked"));
  assert(HasLink(normal, "Safe Blocked"));
  assert(HasLink(normal, "Allowed A"));

  filter.SetSafeMode(true);
  auto safe = selector.Select(links, text, "");
  assert(!HasLink(safe, "Always Blocked"));
  assert(!HasLink(safe, "Safe Blocked"));
  assert(HasLink(safe, "Allowed A"));
  assert(HasLink(safe, "Allowed B"));
  filter.SetSafeMode(false);
}

void VerifyShippedList() {
  ContentFilter filter;
  assert(filter.LoadFromFile());
  assert(filter.IsTitleBlocked("児童ポルノ"));
  assert(filter.IsTitleBlocked("ポルノグラフィ"));
  // セーフモードの追加分は、通常時には除外しない
  assert(!filter.IsTitleBlocked("自殺"));
  assert(!filter.IsTitleBlocked("アダルトゲーム"));
  // 明確に性的ではない記事は、どちらのモードでも除外しない
  filter.SetSafeMode(true);
  assert(filter.IsTitleBlocked("自殺"));
  assert(filter.IsTitleBlocked("アダルトゲーム"));
  for (const char *title :
       {"日本", "ゴルフ", "東京都", "朝鮮民主主義人民共和国", "第二次世界大戦",
        "太宰治", "妊娠", "竹中直人", "大杉漣", "栗山千明", "マリリン・モンロー",
        "伊藤博文", "天智天皇", "豊臣秀次", "広島市への原子爆弾投下", "東京大空襲",
        "ホロコースト", "アメリカ同時多発テロ事件", "十字架", "毒", "いじめ",
        "DMM.com", "レンタルビデオ", "シカゴ_(2002年の映画)"}) {
    assert(!filter.IsTitleBlocked(title));
  }
}

} // namespace

int main() {
  VerifyLevels();
  VerifyInvalidLinesAreIgnored();
  VerifyTitleVariants();
  VerifyEmptyFilterBlocksNothing();
  VerifyLinkSelection();
  VerifyShippedList();
  return 0;
}
