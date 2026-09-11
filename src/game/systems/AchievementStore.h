#pragma once
/**
 * @file AchievementStore.h
 * @brief 実績の進捗データとローカル保存（save/achievements.txt）
 * @details オフライン専用。PlayFabとの同期は行わない。ファイル形式は
 *          PlayFabClient::LoadOrCreateProfile と同じ "key=value" 平テキスト。
*/

#include "AchievementDefinitions.h"
#include <algorithm>
#include <string>
#include <vector>

namespace game::systems {

/** @brief 実績の進捗（解除済み一覧＋累計・自己ベスト系のカウンタ）。*/
struct AchievementProgress {
  std::vector<AchievementId> unlocked;
  int totalClears = 0;         ///< 累計クリア回数
  int totalPagesVisited = 0;   ///< 累計で辿ったリンク数（訪問記事数相当）
  int bestStrokes = 0;         ///< 自己ベスト打数（0=未記録）
  int bestClearTimeMs = 0;     ///< 自己ベストクリアタイム（0=未記録、デイリーのみ）
  int dailyStreak = 0;         ///< デイリーチャレンジ連続プレイ日数
  std::string lastDailyDateIso; ///< 最後にデイリーチャレンジをプレイした日付("YYYY-MM-DD")

  /** @brief 指定した実績が解除済みか。*/
  bool IsUnlocked(AchievementId id) const {
    return std::find(unlocked.begin(), unlocked.end(), id) != unlocked.end();
  }
};

/** @brief 実績進捗のロード/セーブを担当する（状態を持たない静的関数群）。*/
class AchievementStore {
public:
  /** @brief save/achievements.txt を読み込みます。無ければ初期値を返します。*/
  static AchievementProgress Load();

  /** @brief save/achievements.txt へ書き込みます。*/
  static bool Save(const AchievementProgress &progress);
};

} // namespace game::systems
