#pragma once

/**
 * @file ParRules.h
 * @brief WikiGolf の Par 算出ルールです。 山内陽
 */

#include <algorithm>
#include <cstddef>

namespace game::utils {

/**
 * @brief 解決済みホップ数を優先して Par を算出します。 山内陽
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
