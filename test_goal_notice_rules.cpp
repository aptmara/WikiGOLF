#include "src/game/utils/GoalNoticeRules.h"
#include <cassert>
#include <cmath>
#include <vector>

namespace {
bool Near(float left, float right) { return std::fabs(left - right) < 0.0001f; }
} // namespace

int main() {
  using namespace game::utils;
  const ArticleArrivalTiming timing = kRoundStartArrival;

  // 紙は倒れ始めるまで立ったまま、倒れるほど速くなる
  assert(CalculatePaperFallProgress(0.0f, timing) == 0.0f);
  assert(CalculatePaperFallProgress(timing.fallStart, timing) == 0.0f);
  assert(Near(CalculatePaperFallProgress(timing.End(), timing), 1.0f));
  assert(CalculatePaperFallProgress(
             timing.fallStart + timing.fallDuration * 0.5f, timing) < 0.5f);

  assert(Near(CalculatePaperAngle(0.0f, 0.4f), 0.4f));
  assert(Near(CalculatePaperAngle(1.0f, 0.4f), DirectX::XM_PIDIV2));

  // 紙はロード画面の背景色から明るくなり、時刻 0 で本来の色になる
  for (const ArticleArrivalTiming &arrival : {kRoundStartArrival, kShortArrival}) {
    assert(arrival.fadeInDuration > 0.0f);
    assert(CalculatePaperBrightness(arrival.Begin(), arrival) == 0.0f);
    assert(Near(CalculatePaperBrightness(0.0f, arrival), 1.0f));
    assert(CalculatePaperBrightness(arrival.fallStart, arrival) == 1.0f);
    // 明るくなる間は紙は立ったまま、不透明のまま（後ろの地形を透かさない）
    assert(CalculatePaperFallProgress(arrival.Begin(), arrival) == 0.0f);
    assert(CalculatePaperOpacity(arrival.Begin(), arrival) == 1.0f);
    // 入力は前の画面の分を無視する
    assert(ResolveGoalNoticeAdvance(arrival.Begin() + 0.1f, arrival) ==
           arrival.Begin() + 0.1f);
  }
  // 立ち上がりは速め（中間で半分より明るい）で、単調に増える
  const float midFade = timing.Begin() + timing.fadeInDuration * 0.5f;
  assert(CalculatePaperBrightness(midFade, timing) > 0.5f);
  assert(CalculatePaperBrightness(midFade, timing) <
         CalculatePaperBrightness(midFade + 0.05f, timing));
  // 道のりは紙が明るくなり切るまで出さない
  assert(CalculateRouteOpacity(timing.Begin(), timing) == 0.0f);
  assert(CalculateRouteLineProgress(timing.Begin()) == 0.0f);
  // 明るくなる時間を持たない設定は、最初から本来の色
  assert(CalculatePaperBrightness(-1.0f, ArticleArrivalTiming{1.0f, 1.0f}) == 1.0f);

  // 紙は倒れる時間の後半で消える
  assert(CalculatePaperOpacity(timing.fallStart, timing) == 1.0f);
  assert(Near(CalculatePaperOpacity(
                  timing.fallStart + timing.fallDuration * 0.5f, timing),
              1.0f));
  assert(Near(CalculatePaperOpacity(timing.End(), timing), 0.0f));

  // 入力は前の画面の分を無視し、読んでいる間なら倒れ始めへ飛ばす
  assert(ResolveGoalNoticeAdvance(0.1f, timing) == 0.1f);
  assert(ResolveGoalNoticeAdvance(1.0f, timing) == timing.fallStart);
  assert(ResolveGoalNoticeAdvance(4.0f, timing) == 4.0f);

  // 短縮版はプレイを止めない長さに収める
  assert(kShortArrival.End() <= 1.25f);
  assert(kShortArrival.End() < kRoundStartArrival.End());

  assert(SelectRouteGoalTitleFontSize(4) == 46.0f);
  assert(SelectRouteGoalTitleFontSize(16) == 38.0f);
  assert(SelectRouteGoalTitleFontSize(20) == 30.0f);
  assert(SelectRouteGoalTitleFontSize(40) == 24.0f);

  // 旗はたどる順に並び、色を決める残り手数が 1 ずつ減る
  const auto route = BuildGoalRouteFlags(3);
  assert(route.size() == 3);
  assert(route[0].step == 1 && route[0].remaining == 2);
  assert(route[2].remaining == 0 && Near(route[2].x, kRouteEndX));
  assert(route[0].x > kRouteStartX && route[0].x < route[1].x);
  // 最短手数が不明ならゴールの旗だけ
  const auto unknownRoute = BuildGoalRouteFlags(0);
  assert(unknownRoute.size() == 1 && unknownRoute[0].step == 0 &&
         unknownRoute[0].remaining == 0);

  // 線が届いた順に旗が立つ
  assert(Near(CalculateRouteArrivalTime(kRouteStartX), kRouteLineStart));
  assert(Near(CalculateRouteArrivalTime(kRouteEndX), kRouteLineEnd));
  for (const GoalRouteFlag &flag : route) {
    const float arrival = CalculateRouteArrivalTime(flag.x);
    assert(std::fabs(kRouteStartX + (kRouteEndX - kRouteStartX) *
                                        CalculateRouteLineProgress(arrival) -
                     flag.x) < 0.05f);
    assert(CalculateRouteFlagProgress(arrival, flag) == 0.0f);
  }
  assert(CalculateRouteFlagProgress(kRouteLineEnd + kRouteGoalFlagDuration,
                                    route[2]) == 1.0f);
  // 竿は少し行き過ぎてから戻り、布は竿が伸びたあとに広がる
  assert(CalculateFlagRise(0.0f) == 0.0f);
  assert(CalculateFlagRise(0.45f) > 1.0f);
  assert(Near(CalculateFlagRise(1.0f), 1.0f));
  assert(CalculateFlagUnfurl(0.4f) == 0.0f);
  assert(Near(CalculateFlagUnfurl(1.0f), 1.0f));
  // すべて立ち終わり、記事名を読む時間を取ってから紙が倒れる
  assert(CalculateRouteGoalTitleProgress(timing.fallStart - 1.0f) == 1.0f);
  // ボールは落ちて着地する
  assert(Near(CalculateRouteBallLift(kRouteBallAppear), 70.0f));
  assert(Near(CalculateRouteBallLift(1.0f), 0.0f));
  // 文字と線は紙が倒れ始めるとすぐ消える
  assert(CalculateRouteOpacity(1.0f, timing) == 1.0f);
  assert(CalculateRouteOpacity(timing.fallStart + kRouteFadeOutDuration,
                               timing) == 0.0f);

  assert(FormatRouteRemainingLabel(0) == L"ゴールへ");
  assert(FormatRouteRemainingLabel(2) == L"あと2手");
  assert(FormatRouteStepLabel(1) == L"1手目");
  assert(FormatRouteStepLabel(0).empty());

  // 未解析の値は出さない
  assert(FormatGoalNoticeDetail(4, 3) == L"パー4 · 最短3手");
  assert(FormatGoalNoticeDetail(4, 0) == L"パー4");
  assert(FormatGoalNoticeDetail(0, 3) == L"最短3手");
  assert(FormatGoalNoticeDetail(0, 0).empty());

  // 記事は上のタイルから順に並べ、紙の高さで打ち切る
  const std::vector<ArticlePaperTileSize> tiles = {
      {2048.0f, 512.0f}, {2048.0f, 512.0f}, {2048.0f, 512.0f}};
  const auto strips = BuildArticlePaperStrips(32.0f, 18.0f, tiles);
  assert(strips.size() == 3);
  assert(strips[0].tileIndex == 0 && Near(strips[0].height, 8.0f) &&
         Near(strips[0].bottomOffset, 10.0f) &&
         Near(strips[0].verticalFraction, 1.0f));
  assert(strips[1].tileIndex == 1 && Near(strips[1].bottomOffset, 2.0f));
  assert(strips[2].tileIndex == 2 && Near(strips[2].height, 2.0f) &&
         Near(strips[2].bottomOffset, 0.0f) &&
         Near(strips[2].verticalFraction, 0.25f));
  // 短い記事は字を歪ませず、足りない分を白紙で埋める
  const std::vector<ArticlePaperTileSize> oneTile = {{2048.0f, 512.0f}};
  const auto shortStrips = BuildArticlePaperStrips(32.0f, 18.0f, oneTile);
  assert(shortStrips.size() == 2);
  assert(shortStrips[0].tileIndex == 0 &&
         Near(shortStrips[0].bottomOffset, 10.0f));
  assert(shortStrips[1].tileIndex == -1 && Near(shortStrips[1].height, 10.0f));

  assert(CalculateStartToGoalHops(std::vector<int>{3, 1, -1, 2}) == 2);
  assert(CalculateStartToGoalHops(std::vector<int>{0, 4}) == 1);
  assert(CalculateStartToGoalHops(std::vector<int>{-1, -1}) == 0);

  return 0;
}
