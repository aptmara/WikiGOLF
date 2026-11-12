#pragma once
/**
 * @file ReturnPenaltyRules.h
 * @brief 「前のページへ戻る」の罰打を決めるルールです。
 * @details ECSやファイルI/Oに依存しない純粋関数だけを置く
 *          (test_return_penalty_rules.cpp がこのヘッダだけで検証する)。
*/

namespace game::utils {

/** @brief いまの記事からゴールへたどり着けるか。*/
enum class RouteToTargetStatus {
  Unknown, ///< 経路の評価が終わっていない、またはリンクグラフが使えない
  Exists,  ///< ゴールへ届くリンクが少なくとも1つある
  None,    ///< 評価が終わり、ゴールへ届くリンクが無かった(行き止まり)
};

/** @brief 前のページへ戻るときの罰打です。*/
inline constexpr int kReturnToPreviousPagePenalty = 1;

/**
 * @brief 経路評価の状態から、ゴールへたどり着けるかを決めます。
 * @param minResolvedHops 評価済みのリンクのうち、ゴールまでの最小リンク数
 *        (ゴールへのリンクそのものは0、1つも届かなければ-1)
 * @param evaluationFinished 経路評価がすべて終わったか
*/
inline RouteToTargetStatus ResolveRouteToTargetStatus(int minResolvedHops,
                                                      bool evaluationFinished) {
  if (minResolvedHops >= 0) {
    return RouteToTargetStatus::Exists;
  }
  return evaluationFinished ? RouteToTargetStatus::None
                            : RouteToTargetStatus::Unknown;
}

/**
 * @brief 前のページへ戻るときの罰打を返します。
 * @details ゴールへ届くリンクがあると分かっている記事から戻るときだけ1打罰。
 *          行き止まりの記事からの救済として戻るときと、まだ分からないときは無償にする
 *          (分からないのにプレイヤーへ罰を与えない)。
*/
inline int GetReturnToPreviousPagePenalty(RouteToTargetStatus status) {
  return status == RouteToTargetStatus::Exists ? kReturnToPreviousPagePenalty
                                               : 0;
}

} // namespace game::utils
