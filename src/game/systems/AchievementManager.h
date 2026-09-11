#pragma once
/**
 * @file AchievementManager.h
 * @brief 実績システムの独立クラス
 * @details メインロジックからは一切参照されない。AchievementEventBus を購読し、
 *          進捗の判定・保存（AchievementStore）とトースト通知
 *          （AchievementToastPanel）だけを行う。DX_GAME.cpp から
 *          ctx.audio 等と同じ配線で一度だけ生成・Initializeし、
 *          毎フレーム Update(ctx) を呼び出すことでシーンをまたいで生存する。
*/

#include "AchievementDefinitions.h"
#include "AchievementEvent.h"
#include "AchievementStore.h"
#include "../controllers/hud/AchievementToastPanel.h"

namespace core {
struct GameContext;
}

namespace graphics {
class TextRenderer;
}

namespace game::systems {

class AchievementEventBus;

/** @brief 実績イベントを購読し、判定・保存・通知を行う独立クラス。*/
class AchievementManager {
public:
  /** @brief 進捗のロードとイベント購読を行います。プログラム開始時に一度だけ呼びます。*/
  void Initialize(core::GameContext &ctx, AchievementEventBus &bus);

  /** @brief 毎フレーム呼び出し、トースト通知の表示を更新します。*/
  void Update(core::GameContext &ctx);

  /** @brief 全UI描画・シーンオーバーレイの後に呼び出し、トースト通知を
   *         どの要素よりも手前へ描画します。*/
  void Render(core::GameContext &ctx, graphics::TextRenderer &renderer);

  /** @brief トースト通知のエンティティを破棄します。*/
  void Shutdown(core::GameContext &ctx);

  /** @brief 現在の進捗を取得します（AchievementScene等からの参照専用）。*/
  const AchievementProgress &GetProgress() const { return m_progress; }

  /** @brief 指定した実績が解除済みか。*/
  bool IsUnlocked(AchievementId id) const { return m_progress.IsUnlocked(id); }

private:
  /** @brief 1ラウンド内でのみ有効な一時集計（保存はしない）。*/
  struct RoundSessionState {
    int specialJudgementCount = 0;
    bool hadMissThisRound = false;
    bool hadObThisRound = false;
  };

  void HandleEvent(const AchievementEvent &event);
  void OnRoundStarted(const AchievementEvent &event);
  void OnShotJudged(const AchievementEvent &event);
  void OnHazardEntered(const AchievementEvent &event);
  void OnHoleCleared(const AchievementEvent &event);
  void OnDisplayNameRegistered();
  void OnDailyRankingFetched(const AchievementEvent &event);

  /** @brief 未解除なら解除してトーストをキューに積みます。解除した場合true。*/
  bool TryUnlock(AchievementId id);
  void SaveProgress();

  core::GameContext *m_ctx = nullptr;
  AchievementProgress m_progress;
  RoundSessionState m_session;
  game::controllers::hud::AchievementToastPanel m_toastPanel;
};

} // namespace game::systems
