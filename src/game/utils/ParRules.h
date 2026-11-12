#pragma once

/**
 * @file ParRules.h
 * @brief WikiGolf の Par 算出ルールです。
*/

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace game::utils {

/**
 * @brief 記事1バイトあたりのコース奥行(m)の目安です。
 * @details コース奥行は記事HTMLを描いた高さから決まる(HtmlCourseSizing.h)。
 *          まだ読み込んでいない記事の奥行を知るため、Wikipedia の記事長
 *          (prop=info の length、ウィキテキストのバイト数)で近似する。
 *          実測(DVD 103003B→764m、ドイツ 221033B→1467m、
 *          ハントバル・ブンデスリーガ 1981/82 17236B→134m)の中央付近の値。
 *          表や一覧の多い記事は実際より短く見積もられる。
*/
inline constexpr float kCourseDepthPerArticleByte = 0.0072f;
/** @brief コース奥行の下限(m)。HtmlCourseSizing.h の最小奥行と同じ値です。*/
inline constexpr float kEstimatedCourseMinDepth = 120.0f;
/** @brief コース奥行の上限(m)。HtmlCourseSizing.h の最大奥行と同じ値です。*/
inline constexpr float kEstimatedCourseMaxDepth = 20000.0f;
/** @brief ラウンドパーの1打あたり距離に使う、ドライバー飛距離の割合です。*/
inline constexpr float kRoundParDriverCarryRatio = 0.8f;

/**
 * @brief 記事長(バイト数)からコース奥行を見積もります。
 * @param articleBytes 記事長。負なら記事長不明として下限を返します。
 * @return 見積もったコース奥行(m)
*/
inline float EstimateCourseDepthFromArticleBytes(int articleBytes) {
  if (articleBytes <= 0) {
    return kEstimatedCourseMinDepth;
  }
  return std::clamp(static_cast<float>(articleBytes) *
                        kCourseDepthPerArticleByte,
                    kEstimatedCourseMinDepth, kEstimatedCourseMaxDepth);
}

/**
 * @brief ラウンド全体のパーを算出します。
 * @details パー = 最短経路でプレイする記事のコース奥行の合計
 *               ÷ (ドライバー飛距離 × 0.8)、端数は切り上げ。
 *          どの記事も最低1打は要るので、プレイする記事数を下限にする。
 * @param totalCourseDepth 最短経路でプレイする記事のコース奥行の合計(m)
 * @param driverCarryDistance ドライバーの基準飛距離(m)
 * @param playedArticleCount 最短経路でプレイする記事数(ゴール記事を除く)
 * @return パー。入力が不明(0以下)なら0を返します。
*/
inline int CalculateRoundPar(float totalCourseDepth,
                             float driverCarryDistance,
                             int playedArticleCount) {
  const float strokeDistance = driverCarryDistance * kRoundParDriverCarryRatio;
  if (totalCourseDepth <= 0.0f || strokeDistance <= 0.0f) {
    return 0;
  }
  const int byDistance =
      static_cast<int>(std::ceil(totalCourseDepth / strokeDistance));
  return std::max({1, playedArticleCount, byDistance});
}

/**
 * @brief 解決済みホップ数を優先して Par を算出します。
 * @param minHopsToTarget 現在ページのリンク先からターゲットまでの最小ホップ数です。
 * @param validLinkCount フォールバックに使う有効リンク数です。
 * @return ゲーム表示用の Par です。
*/
inline int CalculateWikiGolfPar(int minHopsToTarget,
                                std::size_t validLinkCount) {
  if (minHopsToTarget >= 0) {
    return std::max(1, minHopsToTarget + 1);
  }

  constexpr int kFallbackMinPar = 3;
  constexpr int kFallbackMaxPar = 12;
  const int linkBasedPar = static_cast<int>(validLinkCount) / 2 + 2;
  return std::clamp(linkBasedPar, kFallbackMinPar, kFallbackMaxPar);
}

} // namespace game::utils
