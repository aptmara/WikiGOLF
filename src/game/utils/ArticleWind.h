#pragma once
/**
 * @file ArticleWind.h
 * @brief 記事ごとの風(風速・風向)の決め方
*/

#include "../systems/DailyChallengeRules.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>

namespace game::utils {

struct ArticleWind {
  float speed = 0.0f;       ///< 風速(m/s)
  float directionX = 1.0f;  ///< 風向(正規化)
  float directionY = 0.0f;
};

/**
 * @brief 乱数2つから記事の風を作ります。
 * @details 本文が2000字超なら3〜5m/s、500字超なら1〜3m/s、それ以下は無風。
*/
inline ArticleWind MakeArticleWind(std::size_t articleTextLength,
                                   std::uint32_t speedRoll,
                                   std::uint32_t angleRoll) {
  ArticleWind wind;
  if (articleTextLength > 2000) {
    wind.speed = 3.0f + static_cast<float>(speedRoll % 20) / 10.0f;
  } else if (articleTextLength > 500) {
    wind.speed = 1.0f + static_cast<float>(speedRoll % 20) / 10.0f;
  }
  const float angle = static_cast<float>(angleRoll % 360) * 3.14159f / 180.0f;
  wind.directionX = std::cos(angle);
  wind.directionY = std::sin(angle);
  return wind;
}

/**
 * @brief 記事の風を決めます。
 * @details デイリーは「日付seed+記事名」で決め、同じ日の同じ記事なら誰が来ても
 *          同じ風にする。それ以外は毎回ランダム。
 * @param dailySeed デイリーの日付seed(0ならデイリーではない扱い)
*/
inline ArticleWind ComputeArticleWind(std::size_t articleTextLength,
                                      std::uint32_t dailySeed,
                                      std::string_view pageName) {
  if (dailySeed != 0) {
    game::systems::DailyChallengeRandom random(
        game::systems::HashDailyArticle(dailySeed, pageName));
    const std::uint32_t speedRoll = random.NextRaw();
    const std::uint32_t angleRoll = random.NextRaw();
    return MakeArticleWind(articleTextLength, speedRoll, angleRoll);
  }
  const auto speedRoll = static_cast<std::uint32_t>(std::rand());
  const auto angleRoll = static_cast<std::uint32_t>(std::rand());
  return MakeArticleWind(articleTextLength, speedRoll, angleRoll);
}

} // namespace game::utils
