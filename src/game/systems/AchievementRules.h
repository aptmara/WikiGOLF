#pragma once
/**
 * @file AchievementRules.h
 * @brief 実績の解除条件を判定する副作用のない純粋関数群
 * @details DailyChallengeRules.h / PlayFabRules.h と同じ方針で、ファイルI/Oや
 *          ECSに一切依存しない関数だけを置く。ユニットテスト (test_achievement_rules.cpp)
 *          はこのヘッダだけをincludeして検証する。
*/

#include <chrono>
#include <cstddef>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>

namespace game::systems {

// ---------------------------------------------------------------------
// クリア回数系（しきい値到達で解除。呼び出し側は更新後の累計値を渡す）
// ---------------------------------------------------------------------

/** @brief 累計値がちょうど目標値へ到達したかを判定します（多重解除防止は呼び出し側の既解除チェックに任せる）。*/
inline bool ReachedCountThreshold(int valueAfterUpdate, int threshold) {
  return valueAfterUpdate >= threshold;
}

// ---------------------------------------------------------------------
// スコア・技術系
// ---------------------------------------------------------------------

/** @brief パーより2打以上少ない打数でクリアしたか。*/
inline bool ShouldUnlockBirdie(int shotCount, int par) {
  return par > 0 && shotCount > 0 && shotCount <= par - 2;
}

/** @brief パー通りの打数でクリアしたか。*/
inline bool ShouldUnlockParFirstTry(int shotCount, int par) {
  return par > 0 && shotCount == par;
}

/** @brief 1ラウンド中にSpecial判定が3回以上出たか。*/
inline bool ShouldUnlockTripleSpecial(int specialJudgementCountThisRound) {
  return specialJudgementCountThisRound >= 3;
}

/** @brief 1ラウンド中にMiss判定を一度も出さなかったか。*/
inline bool ShouldUnlockNoMiss(bool hadMissThisRound) {
  return !hadMissThisRound;
}

/**
 * @brief パーから逆算した理論上の最短リンク数を返します。
 * @details GolfGameState::par のコメント通り「パー = リンク数÷2+2」で
 *          算出されているため、逆算すると リンク数 = (パー-2)*2 になる。
*/
inline int ComputeMinimumHopsFromPar(int par) {
  const int hops = (par - 2) * 2;
  return hops > 0 ? hops : 0;
}

/** @brief 実際に辿ったリンク数が理論上の最短リンク数と一致したか。*/
inline bool ShouldUnlockShortestPath(int hopCount, int par) {
  const int minimumHops = ComputeMinimumHopsFromPar(par);
  return minimumHops > 0 && hopCount == minimumHops;
}

/** @brief 1ラウンドで10回以上リンクをたどってからクリアしたか。*/
inline bool ShouldUnlockWanderer(int hopCount) { return hopCount >= 10; }

// ---------------------------------------------------------------------
// ハザード系
// ---------------------------------------------------------------------

/** @brief OBを経験しつつもラウンドをクリアできたか（呼び出しはクリア確定時のみ）。*/
inline bool ShouldUnlockResilientClear(bool hadObThisRound) {
  return hadObThisRound;
}

// ---------------------------------------------------------------------
// 累計・進捗系
// ---------------------------------------------------------------------

/** @brief 累計訪問記事数が目標値を超えたか。*/
inline bool ShouldUnlockExplorer(int totalPagesVisitedAfterUpdate,
                                  int threshold) {
  return totalPagesVisitedAfterUpdate >= threshold;
}

// ---------------------------------------------------------------------
// 自己ベスト系
// ---------------------------------------------------------------------

/** @brief 打数の自己ベストを更新したか（0以下＝未記録は常に更新扱い）。*/
inline bool IsNewBestStrokes(int shotCount, int currentBestStrokes) {
  return currentBestStrokes <= 0 || shotCount < currentBestStrokes;
}

/** @brief クリアタイムの自己ベストを更新したか（0以下＝未記録は常に更新扱い）。*/
inline bool IsNewBestClearTime(int clearTimeMs, int currentBestClearTimeMs) {
  return currentBestClearTimeMs <= 0 || clearTimeMs < currentBestClearTimeMs;
}

// ---------------------------------------------------------------------
// デイリーチャレンジの連続プレイ日数
// ---------------------------------------------------------------------

/**
 * @brief "YYYY-MM-DD" 形式の日付文字列を分解します。
 * @return 形式が不正な場合はfalse。
*/
inline bool ParseIsoDate(std::string_view date, int &year, int &month,
                          int &day) {
  if (date.size() != 10 || date[4] != '-' || date[7] != '-') {
    return false;
  }
  const auto digit = [&](std::size_t index) -> int {
    const char value = date[index];
    return (value >= '0' && value <= '9') ? value - '0' : -1;
  };
  int values[8];
  const std::size_t indices[8] = {0, 1, 2, 3, 5, 6, 8, 9};
  for (int i = 0; i < 8; ++i) {
    values[i] = digit(indices[i]);
    if (values[i] < 0) {
      return false;
    }
  }
  year = values[0] * 1000 + values[1] * 100 + values[2] * 10 + values[3];
  month = values[4] * 10 + values[5];
  day = values[6] * 10 + values[7];
  return month >= 1 && month <= 12 && day >= 1 && day <= 31;
}

/**
 * @brief グレゴリオ暦の年月日をエポックからの通算日数に変換します。
 * @details Howard Hinnant の civil_from_days/days_from_civil アルゴリズムに
 *          基づく実装。月境界・閏年を正しく扱える。
*/
inline long long DaysFromCivil(int year, int month, int day) {
  const long long y = year - (month <= 2 ? 1 : 0);
  const long long era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned mp = static_cast<unsigned>((month + (month > 2 ? -3 : 9)));
  const unsigned doy = (153 * mp + 2) / 5 + static_cast<unsigned>(day) - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<long long>(doe) - 719468;
}

/**
 * @brief 連続プレイ日数を更新します。
 * @param lastDateIso 前回プレイした日付（"YYYY-MM-DD"、未記録なら空文字）
 * @param todayIso 今日の日付（"YYYY-MM-DD"）
 * @param currentStreak 更新前の連続日数
 * @return 更新後の連続日数。同日内の再プレイは維持、1日空いたら+1、
 *         2日以上空いたら1にリセットする。
*/
inline int ComputeDailyStreak(std::string_view lastDateIso,
                               std::string_view todayIso,
                               int currentStreak) {
  int todayYear = 0, todayMonth = 0, todayDay = 0;
  if (!ParseIsoDate(todayIso, todayYear, todayMonth, todayDay)) {
    return currentStreak;
  }
  int lastYear = 0, lastMonth = 0, lastDay = 0;
  if (lastDateIso.empty() ||
      !ParseIsoDate(lastDateIso, lastYear, lastMonth, lastDay)) {
    return 1;
  }

  const long long todayDays = DaysFromCivil(todayYear, todayMonth, todayDay);
  const long long lastDays = DaysFromCivil(lastYear, lastMonth, lastDay);
  const long long diff = todayDays - lastDays;

  if (diff == 0) {
    return currentStreak > 0 ? currentStreak : 1;
  }
  if (diff == 1) {
    return currentStreak > 0 ? currentStreak + 1 : 1;
  }
  return 1;
}

/** @brief 実行環境のローカル日時から "YYYY-MM-DD" 形式の今日の日付を取得します。*/
inline std::string TodayIsoDateLocal() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t nowTimeT = std::chrono::system_clock::to_time_t(now);
  std::tm localTime{};
#if defined(_WIN32)
  localtime_s(&localTime, &nowTimeT);
#else
  localtime_r(&nowTimeT, &localTime);
#endif
  std::ostringstream text;
  text << (localTime.tm_year + 1900) << '-' << std::setfill('0')
       << std::setw(2) << (localTime.tm_mon + 1) << '-' << std::setw(2)
       << localTime.tm_mday;
  return text.str();
}

} // namespace game::systems
