#pragma once
/**
 * @file AchievementEvent.h
 * @brief 実績システムへ送るイベントの定義
 * @details メインロジックはこのヘッダだけを見て AchievementEventBus::Publish() を
 *          呼び出せばよい。判定ロジックや保存処理には一切依存しない、純粋データ。
*/

#include "../components/WikiComponents.h"

namespace game::systems {

/** @brief 実績システムへ送られるイベントの種別。*/
enum class AchievementEventType {
  RoundStarted,          ///< 1ラウンド（スタート記事〜ゴール記事）の開始
  ShotJudged,             ///< 1打のインパクト判定が確定した
  HazardEntered,          ///< ボールがOB地形（水・溶岩）に静止した
  HoleCleared,            ///< ラウンドをクリアした（ゴール記事へ到達）
  DisplayNameRegistered,  ///< PlayFabの表示名を新規登録した
  DailyRankingFetched,    ///< デイリーチャレンジのランキングを取得した
};

/**
 * @brief 実績システムへ送るイベント本体。
 * @details 種別ごとに使うフィールドだけが有効。未使用フィールドは既定値のまま。
*/
struct AchievementEvent {
  AchievementEventType type = AchievementEventType::RoundStarted;

  // RoundStarted / HoleCleared で使用
  bool isDailyChallenge = false;
  bool isTutorial = false;
  bool isFreePlay = false;

  // HoleCleared で使用
  int shotCount = 0;
  int par = 0;
  int clearTimeMs = 0;
  int hopCount = 0; ///< pathHistory.size() - 1 相当（実際に辿ったリンク数）

  // ShotJudged で使用
  game::components::ShotJudgement judgement =
      game::components::ShotJudgement::None;

  // HazardEntered で使用
  game::components::TerrainMaterial material =
      game::components::TerrainMaterial::None;

  // DailyRankingFetched で使用
  bool isTopRank = false;
};

} // namespace game::systems
