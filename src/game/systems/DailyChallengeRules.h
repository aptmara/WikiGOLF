#pragma once

#include <cstddef>
#include <cstdint>
#include <random>
#include <string_view>

namespace game::systems {

inline std::uint32_t MakeDailyChallengeSeed(int year, int month, int day) {
  return static_cast<std::uint32_t>(year * 10000 + month * 100 + day);
}

inline std::uint32_t ParseDailyChallengeSeed(std::string_view timestamp) {
  if (timestamp.size() < 10 || timestamp[4] != '-' || timestamp[7] != '-') {
    return 0;
  }

  const auto digit = [&](std::size_t index) -> int {
    const char value = timestamp[index];
    if (value < '0' || value > '9') {
      return -1;
    }
    return value - '0';
  };

  const int yearThousands = digit(0);
  const int yearHundreds = digit(1);
  const int yearTens = digit(2);
  const int yearOnes = digit(3);
  const int monthTens = digit(5);
  const int monthOnes = digit(6);
  const int dayTens = digit(8);
  const int dayOnes = digit(9);
  if (yearThousands < 0 || yearHundreds < 0 || yearTens < 0 ||
      yearOnes < 0 || monthTens < 0 || monthOnes < 0 || dayTens < 0 ||
      dayOnes < 0) {
    return 0;
  }

  const int year = yearThousands * 1000 + yearHundreds * 100 +
                   yearTens * 10 + yearOnes;
  const int month = monthTens * 10 + monthOnes;
  const int day = dayTens * 10 + dayOnes;
  if (month < 1 || month > 12 || day < 1 || day > 31) {
    return 0;
  }
  return MakeDailyChallengeSeed(year, month, day);
}

class DailyChallengeRandom {
public:
  explicit DailyChallengeRandom(std::uint32_t seed) : m_rng(seed) {}

  std::size_t NextIndex(std::size_t candidateCount) {
    std::uniform_int_distribution<std::size_t> distribution(
        0, candidateCount - 1);
    return distribution(m_rng);
  }

  /** @brief 32bitの乱数をそのまま返します。*/
  std::uint32_t NextRaw() { return static_cast<std::uint32_t>(m_rng()); }

private:
  std::mt19937 m_rng;
};

/** @brief スタート記事の抽選をゴールの抽選とずらすための値。*/
inline constexpr std::uint32_t kDailyStartSeedSalt = 0x5A17u;
/** @brief スタート記事として好ましいゴールまでの手数(下限・上限)。*/
inline constexpr int kDailyStartPreferredMinHops = 3;
inline constexpr int kDailyStartPreferredMaxHops = 5;
/** @brief 好ましい手数の記事が見つからないときに許す手数の下限。*/
inline constexpr int kDailyStartFallbackMinHops = 2;
/** @brief スタート記事に求める被リンク数の範囲(知られていて、かつ大きすぎるハブではない)。*/
inline constexpr std::uint32_t kDailyStartMinIncoming = 100;
inline constexpr std::uint32_t kDailyStartMaxIncoming = 5000;
/** @brief スタート記事の抽選で試す最大回数。*/
inline constexpr int kDailyStartMaxAttempts = 20000;

/** @brief デイリーのスタート候補としての評価です。*/
enum class DailyStartFit { Reject, Fallback, Preferred };

/** @brief ゴールまでの手数からスタート候補としての評価を決めます。*/
inline DailyStartFit ClassifyDailyStartHops(int hops) {
  if (hops >= kDailyStartPreferredMinHops &&
      hops <= kDailyStartPreferredMaxHops) {
    return DailyStartFit::Preferred;
  }
  if (hops >= kDailyStartFallbackMinHops &&
      hops <= kDailyStartPreferredMaxHops) {
    return DailyStartFit::Fallback;
  }
  return DailyStartFit::Reject;
}

/**
 * @brief デイリーのスタート記事を、シードから決まった順に引いて選びます。
 * @details 同じシード・同じ記事数・同じ判定なら、誰が引いても同じ記事になる。
 *          最初に見つかった Preferred を返し、無ければ最初の Fallback を返す。
 * @param seed デイリーのシード(日付)
 * @param articleCount 記事の総数
 * @param classify 記事番号 → 評価
 * @param attempts 実際に試した回数(ログ用、nullptr可)
 * @return 選んだ記事番号。見つからなければ articleCount を返す
*/
template <class Classify>
std::uint32_t PickDailyStartArticle(std::uint32_t seed,
                                    std::uint32_t articleCount,
                                    Classify &&classify,
                                    int *attempts = nullptr) {
  std::uint32_t fallback = articleCount;
  if (articleCount == 0) {
    return articleCount;
  }
  DailyChallengeRandom random(seed ^ kDailyStartSeedSalt);
  for (int i = 0; i < kDailyStartMaxAttempts; ++i) {
    const auto article =
        static_cast<std::uint32_t>(random.NextIndex(articleCount));
    const DailyStartFit fit = classify(article);
    if (fit == DailyStartFit::Preferred) {
      if (attempts) *attempts = i + 1;
      return article;
    }
    if (fit == DailyStartFit::Fallback && fallback == articleCount) {
      fallback = article;
    }
  }
  if (attempts) *attempts = kDailyStartMaxAttempts;
  return fallback;
}

/**
 * @brief デイリーのシードと記事名から、記事ごとに決まる値を作ります(FNV-1a)。
 * @details 風など「同じ日の同じ記事なら誰が来ても同じ」にしたい値の種に使う。
*/
inline std::uint32_t HashDailyArticle(std::uint32_t seed,
                                      std::string_view title) {
  std::uint32_t hash = 2166136261u ^ seed;
  hash *= 16777619u;
  for (const char c : title) {
    hash ^= static_cast<std::uint8_t>(c);
    hash *= 16777619u;
  }
  return hash;
}

/**
 * @brief 今回のデイリーがその日の1回目か。
 * @param lastAttemptSeed 前回デイリーを始めた日のシード(0=記録なし)
 * @param seed 今回のシード(0=日付が分からない)
*/
inline bool IsFirstDailyAttempt(std::uint32_t lastAttemptSeed,
                                std::uint32_t seed) {
  return seed != 0 && lastAttemptSeed != seed;
}

/** @brief デイリーをランキングに載せない理由です。*/
enum class DailyPracticeReason { None, AlreadyPlayedToday, SafeMode, UnknownDate };

/**
 * @brief デイリーの記録をランキングへ送るかを決めます。
 * @details ランキングは「その日の1回目」「通常の内容フィルタ」の記録だけを比べる。
*/
inline DailyPracticeReason DecideDailyPracticeReason(
    std::uint32_t lastAttemptSeed, std::uint32_t seed, bool safeMode) {
  if (seed == 0) {
    return DailyPracticeReason::UnknownDate;
  }
  if (!IsFirstDailyAttempt(lastAttemptSeed, seed)) {
    return DailyPracticeReason::AlreadyPlayedToday;
  }
  if (safeMode) {
    return DailyPracticeReason::SafeMode;
  }
  return DailyPracticeReason::None;
}

} // namespace game::systems
