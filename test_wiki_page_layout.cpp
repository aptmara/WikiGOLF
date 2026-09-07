#include "src/game/scenes/HolePlacementPlanner.h"
#include "src/game/scenes/HoleVisualRules.h"
#include "src/game/scenes/AsyncPathEvaluator.h"
#include "src/game/scenes/TutorialCourseLayout.h"
#include "src/game/scenes/PageLinkSelector.h"
#include "src/graphics/WikiTextureGenerator.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <type_traits>
#include <thread>

#define CHECK_TRUE(condition, message)                                         \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << "[FAIL] " << message << "\n";                             \
      std::exit(1);                                                            \
    }                                                                          \
    std::cout << "[PASS] " << message << "\n";                               \
  } while (0)

int main() {
  const auto targetColor =
      game::scenes::HoleVisualRules::GetColor(true, 0);
  CHECK_TRUE(targetColor.x == 1.0f && targetColor.y == 0.2f &&
                 targetColor.z == 0.2f && targetColor.w == 1.0f,
             "目的ホールの表示色を固定する");

  const auto oneHopColor =
      game::scenes::HoleVisualRules::GetColor(false, 1);
  CHECK_TRUE(oneHopColor.x == 1.0f && oneHopColor.y == 0.85f &&
                 oneHopColor.z == 0.0f && oneHopColor.w == 1.0f,
             "1ホップホールの表示色を固定する");

  const auto bodyColor =
      game::scenes::HoleVisualRules::GetBodyColor(false, 2);
  CHECK_TRUE(std::fabs(bodyColor.x - 0.45f) < 0.0001f &&
                 std::fabs(bodyColor.y - 0.27f) < 0.0001f &&
                 std::fabs(bodyColor.z - 0.09f) < 0.0001f &&
                 std::fabs(bodyColor.w - 1.0f) < 0.0001f,
             "ホール本体色を表示色から一貫して生成する");

  game::scenes::PageLinkSelector linkSelector;
  std::vector<game::WikiLink> links;
  links.push_back({"記事B"});
  links.push_back({"2024年"});
  links.push_back({"記事A"});
  links.push_back({"記事C"});
  const auto selection =
      linkSelector.Select(links, "記事A本文 記事B本文 記事C本文", "記事C");
  CHECK_TRUE(selection.links.size() == 3 && selection.links[0].first == "記事A" &&
                 selection.links[1].first == "記事B" &&
                 selection.links[2].first == "記事C" && selection.targetAdded,
             "本文順、除外規則、目的記事追加を一度に適用する");

  game::scenes::TutorialCourseLayout tutorialLayout;
  CHECK_TRUE(tutorialLayout.IsPresetPage("チュートリアル"),
             "チュートリアル記事だけを固定教材コースとして判定する");
  CHECK_TRUE(!tutorialLayout.IsPresetPage("ゴルフ"),
             "通常記事を固定教材コースとして扱わない");

  graphics::WikiTextureResult tutorialTexture;
  tutorialTexture.width = 1000;
  tutorialTexture.height = 1000;
  graphics::LinkRegion sourceRough;
  sourceRough.targetPage = "ラフ";
  sourceRough.width = 11.0f;
  sourceRough.height = 13.0f;
  tutorialTexture.links.push_back(sourceRough);

  const auto tutorialLinks =
      tutorialLayout.BuildGameplayLinks(tutorialTexture, "グリーン");
  CHECK_TRUE(tutorialLinks.size() == 6,
             "固定教材コースは6種類のリンクを所定順で生成する");
  CHECK_TRUE(tutorialLinks[0].targetPage == "フェアウェイ" &&
                 tutorialLinks[1].targetPage == "ラフ" &&
                 tutorialLinks[2].targetPage == "バンカー" &&
                 tutorialLinks[3].targetPage == "ウォーターハザード" &&
                 tutorialLinks[4].targetPage == "グリーン" &&
                 tutorialLinks[5].targetPage == "ゴール",
             "固定教材コースの遷移先と順序を保持する");
  CHECK_TRUE(tutorialLinks[4].isTarget,
             "指定した目的記事のリンクだけを目的地として扱う");
  CHECK_TRUE(!tutorialLinks[0].isTarget && !tutorialLinks[5].isTarget,
             "目的記事以外のリンクを通常ホールとして扱う");
  CHECK_TRUE(std::fabs(tutorialLinks[0].x - 410.0f) < 0.0001f &&
                 std::fabs(tutorialLinks[0].y - 658.44446f) < 0.001f &&
                 std::fabs(tutorialLinks[0].width - 180.0f) < 0.0001f &&
                 std::fabs(tutorialLinks[0].height - 72.0f) < 0.0001f,
             "固定ワールド座標を従来どおりテクスチャ矩形へ変換する");
  CHECK_TRUE(std::fabs(tutorialLinks[1].width - 180.0f) < 0.0001f &&
                 std::fabs(tutorialLinks[1].height - 72.0f) < 0.0001f,
             "既存リンクの寸法も固定教材用の矩形へ置き換える");

  game::scenes::HolePlacementPlanner planner;

  graphics::LinkRegion link;
  link.x = 10.0f;
  link.y = 20.0f;
  link.width = 20.0f;
  link.height = 40.0f;
  link.targetPage = "記事A";
  link.isTarget = true;
  const auto candidate = planner.BuildCandidate(link, 7, 100, 200, 80.0f,
                                                 120.0f);
  CHECK_TRUE(std::fabs(candidate.x - (-24.0f)) < 0.0001f,
             "リンク矩形の中心Xをフィールド座標へ変換する");
  CHECK_TRUE(std::fabs(candidate.z - 36.0f) < 0.0001f,
             "リンク矩形の中心YをフィールドZ座標へ変換する");
  CHECK_TRUE(candidate.linkTarget == "記事A" && candidate.isTarget &&
                 candidate.originalIndex == 7,
             "リンクの遷移先、目的地フラグ、元順序を保持する");

  using Candidate = std::remove_cv_t<decltype(candidate)>;
  std::vector<Candidate> candidates;
  Candidate first;
  first.x = 20.0f;
  first.z = 0.0f;
  first.originalIndex = 0;
  candidates.push_back(first);
  Candidate nearFirst;
  nearFirst.x = 23.0f;
  nearFirst.z = 4.0f;
  nearFirst.originalIndex = 1;
  candidates.push_back(nearFirst);
  Candidate target;
  target.x = 2.0f;
  target.z = 0.0f;
  target.isTarget = true;
  target.originalIndex = 2;
  candidates.push_back(target);
  Candidate distantCandidate;
  distantCandidate.x = 40.0f;
  distantCandidate.z = 0.0f;
  distantCandidate.originalIndex = 3;
  candidates.push_back(distantCandidate);

  const auto selected = planner.SelectMapCandidates(candidates);
  CHECK_TRUE(selected.size() == 3,
             "近接する通常候補を除外して目的地候補を必ず残す");
  CHECK_TRUE(selected[0].originalIndex == 0 &&
                 selected[1].originalIndex == 2 &&
                 selected[2].originalIndex == 3,
             "選抜後の候補を記事内の元順序へ戻す");

  std::vector<game::scenes::HolePlacementCandidate> asyncCandidates =
      selected;
  game::scenes::AsyncPathEvaluator evaluator;
  evaluator.Start(asyncCandidates, -1, 2, 42);
  std::optional<std::vector<game::scenes::HolePlacementCandidate>> evaluated;
  for (int attempt = 0; attempt < 1000 && !evaluated; ++attempt) {
    evaluated = evaluator.TryConsumeCompleted();
    if (!evaluated) {
      std::this_thread::yield();
    }
  }
  CHECK_TRUE(evaluated.has_value(),
             "対象ページIDがない非同期経路評価を完了して消費する");
  CHECK_TRUE(evaluator.GetProgress() == 1.0f,
             "スキップした非同期経路評価も進捗を完了値へ更新する");
  CHECK_TRUE(evaluated->size() == asyncCandidates.size() &&
                 (*evaluated)[0].originalIndex ==
                     asyncCandidates[0].originalIndex,
             "経路評価をスキップした場合は候補の順序と内容を保持する");

  std::cout << "All Wiki page layout tests passed!\n";
  return 0;
}
