// Release ビルドでも assert で確かめるため、NDEBUG を外してから読み込む。
#undef NDEBUG
#include "game/utils/ReturnPenaltyRules.h"

#include <cassert>

int main() {
  using namespace game::utils;

  // ゴールへ届くリンクが見つかっていれば、評価の途中でも「ある」
  assert(ResolveRouteToTargetStatus(2, false) == RouteToTargetStatus::Exists);
  assert(ResolveRouteToTargetStatus(0, true) == RouteToTargetStatus::Exists);
  // 見つからないまま評価が終われば行き止まり
  assert(ResolveRouteToTargetStatus(-1, true) == RouteToTargetStatus::None);
  // 評価の途中ならまだ分からない
  assert(ResolveRouteToTargetStatus(-1, false) == RouteToTargetStatus::Unknown);

  // 罰打はゴールへ届く記事から戻るときだけ
  assert(GetReturnToPreviousPagePenalty(RouteToTargetStatus::Exists) == 1);
  assert(GetReturnToPreviousPagePenalty(RouteToTargetStatus::None) == 0);
  assert(GetReturnToPreviousPagePenalty(RouteToTargetStatus::Unknown) == 0);

  return 0;
}
