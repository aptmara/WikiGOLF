#pragma once
/**
 * @file AchievementDefinitions.h
 * @brief 実績IDと表示用メタデータの静的定義
 * @details ここには純粋なデータのみを置く。解除条件の判定は AchievementRules.h、
 *          進捗の保存/読込は AchievementStore、両者を束ねる購読処理は
 *          AchievementManager が担当する。
*/

#include <array>
#include <cstddef>

namespace game::systems {

/** @brief 実績ID。*/
enum class AchievementId {
  FirstClear,        ///< はじめの一歩
  TenClears,          ///< ウィキゴルファー
  HundredClears,      ///< 殿堂入り
  TutorialComplete,   ///< チュートリアル修了
  FreePlayClear,      ///< 道なき道
  Birdie,             ///< バーディー級
  ParFirstTry,        ///< ワンパット感覚
  TripleSpecial,      ///< 職人技
  NoMiss,             ///< ノーミス進行
  ShortestPath,       ///< 最短経路
  FirstWaterHazard,   ///< 水没注意報
  FirstLavaHazard,    ///< 灼熱の洗礼
  ResilientClear,     ///< 不屈の精神
  DailyFirst,         ///< 今日の一打
  DailyStreak3,       ///< 継続は力なり
  DailyStreak7,       ///< 常連プレイヤー
  DailyRankingFirst,  ///< 本日の王者
  DisplayNameRegistered, ///< 名乗りを上げる
  NewRecord,          ///< 記録更新
  Explorer100Pages,   ///< 知の探求者
  Wanderer10Hops,     ///< 寄り道の達人

  Count, ///< 実績数（配列サイズ用。実際のIDとしては使わない）
};

/** @brief 一覧表示・進捗表示のグルーピング用カテゴリ。*/
enum class AchievementCategory {
  Progress,     ///< クリア回数などの基本プレイ実績
  Skill,        ///< スコア・技術系
  Hazard,       ///< OB/ハザード系
  Daily,        ///< デイリーチャレンジ系
  Online,       ///< オンライン/ランキング系
  Exploration,  ///< やり込み・探索系
};

/** @brief 実績1件の表示メタデータ。*/
struct AchievementDef {
  AchievementId id;
  const wchar_t *name;
  const wchar_t *description;
  AchievementCategory category;
  /**
   * @brief 進捗表示の目標値。0なら単発条件（進捗バー不要）。
   * @details 例: TenClears なら 10、DailyStreak7 なら 7。
  */
  int targetValue = 0;
};

/** @brief 実績定義テーブル（AchievementId の宣言順と一致させること）。*/
inline constexpr std::array<AchievementDef,
                            static_cast<std::size_t>(AchievementId::Count)>
    kAchievementDefs = {{
        {AchievementId::FirstClear, L"はじめの一歩",
         L"初めてコースをクリアする", AchievementCategory::Progress, 0},
        {AchievementId::TenClears, L"ウィキゴルファー",
         L"累計10ホールクリアする", AchievementCategory::Progress, 10},
        {AchievementId::HundredClears, L"殿堂入り",
         L"累計100ホールクリアする", AchievementCategory::Progress, 100},
        {AchievementId::TutorialComplete, L"チュートリアル修了",
         L"チュートリアルコースをクリアする", AchievementCategory::Progress,
         0},
        {AchievementId::FreePlayClear, L"道なき道",
         L"自由入力でスタート/ゴール記事を指定してクリアする",
         AchievementCategory::Progress, 0},
        {AchievementId::Birdie, L"バーディー級",
         L"パーより2打以上少ない打数でクリアする", AchievementCategory::Skill,
         0},
        {AchievementId::ParFirstTry, L"ワンパット感覚",
         L"パー通りの打数でクリアする（初回）", AchievementCategory::Skill,
         0},
        {AchievementId::TripleSpecial, L"職人技",
         L"1ラウンド中に3回以上Special判定を出してクリアする",
         AchievementCategory::Skill, 0},
        {AchievementId::NoMiss, L"ノーミス進行",
         L"Miss判定を一度も出さずにクリアする", AchievementCategory::Skill,
         0},
        {AchievementId::ShortestPath, L"最短経路",
         L"理論上最短のリンク数でクリアする", AchievementCategory::Skill, 0},
        {AchievementId::FirstWaterHazard, L"水没注意報",
         L"池（水場）にボールを落とす（初回）", AchievementCategory::Hazard,
         0},
        {AchievementId::FirstLavaHazard, L"灼熱の洗礼",
         L"溶岩にボールを落とす（初回）", AchievementCategory::Hazard, 0},
        {AchievementId::ResilientClear, L"不屈の精神",
         L"OBを経験しつつも同じラウンドをクリアする",
         AchievementCategory::Hazard, 0},
        {AchievementId::DailyFirst, L"今日の一打",
         L"デイリーチャレンジに初参加する", AchievementCategory::Daily, 0},
        {AchievementId::DailyStreak3, L"継続は力なり",
         L"デイリーチャレンジを3日連続でプレイする",
         AchievementCategory::Daily, 3},
        {AchievementId::DailyStreak7, L"常連プレイヤー",
         L"デイリーチャレンジを7日連続でプレイする",
         AchievementCategory::Daily, 7},
        {AchievementId::DailyRankingFirst, L"本日の王者",
         L"デイリーチャレンジのランキングで1位を獲得する",
         AchievementCategory::Online, 0},
        {AchievementId::DisplayNameRegistered, L"名乗りを上げる",
         L"オンラインランキング用の表示名を登録する",
         AchievementCategory::Online, 0},
        {AchievementId::NewRecord, L"記録更新",
         L"自己ベスト（打数 or クリアタイム）を更新する",
         AchievementCategory::Online, 0},
        {AchievementId::Explorer100Pages, L"知の探求者",
         L"累計で訪問した記事数が100を超える",
         AchievementCategory::Exploration, 100},
        {AchievementId::Wanderer10Hops, L"寄り道の達人",
         L"1ラウンドで10回以上リンクをたどってからクリアする",
         AchievementCategory::Exploration, 0},
    }};

/** @brief IDから定義を検索します。見つからない場合はnullptr。*/
inline const AchievementDef *FindAchievementDef(AchievementId id) {
  for (const auto &def : kAchievementDefs) {
    if (def.id == id) {
      return &def;
    }
  }
  return nullptr;
}

} // namespace game::systems
