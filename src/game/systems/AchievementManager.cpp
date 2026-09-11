/**
 * @file AchievementManager.cpp
 * @brief 実績システムの独立クラスの実装
*/

#include "AchievementManager.h"
#include "AchievementEventBus.h"
#include "AchievementRules.h"
#include "../../core/GameContext.h"
#include <algorithm>

namespace game::systems {

void AchievementManager::Initialize(core::GameContext &ctx,
                                    AchievementEventBus &bus) {
  m_ctx = &ctx;
  m_progress = AchievementStore::Load();
  bus.Subscribe(
      [this](const AchievementEvent &event) { HandleEvent(event); });
}

void AchievementManager::Update(core::GameContext &ctx) {
  m_toastPanel.Update(ctx, ctx.dt);
}

void AchievementManager::Render(core::GameContext &ctx,
                                graphics::TextRenderer &renderer) {
  m_toastPanel.Render(ctx, renderer);
}

void AchievementManager::Shutdown(core::GameContext &ctx) {
  m_toastPanel.Shutdown(ctx);
}

void AchievementManager::HandleEvent(const AchievementEvent &event) {
  switch (event.type) {
  case AchievementEventType::RoundStarted:
    OnRoundStarted(event);
    break;
  case AchievementEventType::ShotJudged:
    OnShotJudged(event);
    break;
  case AchievementEventType::HazardEntered:
    OnHazardEntered(event);
    break;
  case AchievementEventType::HoleCleared:
    OnHoleCleared(event);
    break;
  case AchievementEventType::DisplayNameRegistered:
    OnDisplayNameRegistered();
    break;
  case AchievementEventType::DailyRankingFetched:
    OnDailyRankingFetched(event);
    break;
  }
}

void AchievementManager::OnRoundStarted(const AchievementEvent & /*event*/) {
  m_session = RoundSessionState{};
}

void AchievementManager::OnShotJudged(const AchievementEvent &event) {
  if (event.judgement == game::components::ShotJudgement::Special) {
    ++m_session.specialJudgementCount;
  } else if (event.judgement == game::components::ShotJudgement::Miss) {
    m_session.hadMissThisRound = true;
  }
}

void AchievementManager::OnHazardEntered(const AchievementEvent &event) {
  m_session.hadObThisRound = true;

  bool unlocked = false;
  if (event.material == game::components::TerrainMaterial::Water) {
    unlocked = TryUnlock(AchievementId::FirstWaterHazard);
  } else if (event.material == game::components::TerrainMaterial::Lava) {
    unlocked = TryUnlock(AchievementId::FirstLavaHazard);
  }
  if (unlocked) {
    SaveProgress();
  }
}

void AchievementManager::OnHoleCleared(const AchievementEvent &event) {
  if (event.isTutorial) {
    // チュートリアルクリアは「チュートリアル修了」以外の実績・進捗カウンタに
    // 一切影響させない（ノーミス進行等は通常プレイでのみ達成可能にする）。
    if (TryUnlock(AchievementId::TutorialComplete)) {
      SaveProgress();
    }
    return;
  }

  ++m_progress.totalClears;
  m_progress.totalPagesVisited += std::max(0, event.hopCount);

  TryUnlock(AchievementId::FirstClear);
  if (ReachedCountThreshold(m_progress.totalClears, 10)) {
    TryUnlock(AchievementId::TenClears);
  }
  if (ReachedCountThreshold(m_progress.totalClears, 100)) {
    TryUnlock(AchievementId::HundredClears);
  }

  if (event.isFreePlay) {
    TryUnlock(AchievementId::FreePlayClear);
  }

  if (ShouldUnlockBirdie(event.shotCount, event.par)) {
    TryUnlock(AchievementId::Birdie);
  }
  if (ShouldUnlockParFirstTry(event.shotCount, event.par)) {
    TryUnlock(AchievementId::ParFirstTry);
  }
  if (ShouldUnlockTripleSpecial(m_session.specialJudgementCount)) {
    TryUnlock(AchievementId::TripleSpecial);
  }
  if (ShouldUnlockNoMiss(m_session.hadMissThisRound)) {
    TryUnlock(AchievementId::NoMiss);
  }
  if (ShouldUnlockShortestPath(event.hopCount, event.par)) {
    TryUnlock(AchievementId::ShortestPath);
  }
  if (ShouldUnlockWanderer(event.hopCount)) {
    TryUnlock(AchievementId::Wanderer10Hops);
  }
  if (ShouldUnlockResilientClear(m_session.hadObThisRound)) {
    TryUnlock(AchievementId::ResilientClear);
  }
  if (ShouldUnlockExplorer(m_progress.totalPagesVisited, 100)) {
    TryUnlock(AchievementId::Explorer100Pages);
  }

  bool newRecord = false;
  if (IsNewBestStrokes(event.shotCount, m_progress.bestStrokes)) {
    m_progress.bestStrokes = event.shotCount;
    newRecord = true;
  }
  if (event.clearTimeMs > 0 &&
      IsNewBestClearTime(event.clearTimeMs, m_progress.bestClearTimeMs)) {
    m_progress.bestClearTimeMs = event.clearTimeMs;
    newRecord = true;
  }
  if (newRecord) {
    TryUnlock(AchievementId::NewRecord);
  }

  if (event.isDailyChallenge) {
    TryUnlock(AchievementId::DailyFirst);

    const std::string today = TodayIsoDateLocal();
    m_progress.dailyStreak = ComputeDailyStreak(
        m_progress.lastDailyDateIso, today, m_progress.dailyStreak);
    m_progress.lastDailyDateIso = today;

    if (ReachedCountThreshold(m_progress.dailyStreak, 3)) {
      TryUnlock(AchievementId::DailyStreak3);
    }
    if (ReachedCountThreshold(m_progress.dailyStreak, 7)) {
      TryUnlock(AchievementId::DailyStreak7);
    }
  }

  SaveProgress();
}

void AchievementManager::OnDisplayNameRegistered() {
  if (TryUnlock(AchievementId::DisplayNameRegistered)) {
    SaveProgress();
  }
}

void AchievementManager::OnDailyRankingFetched(const AchievementEvent &event) {
  if (!event.isTopRank) {
    return;
  }
  if (TryUnlock(AchievementId::DailyRankingFirst)) {
    SaveProgress();
  }
}

bool AchievementManager::TryUnlock(AchievementId id) {
  if (m_progress.IsUnlocked(id)) {
    return false;
  }
  m_progress.unlocked.push_back(id);

  if (m_ctx) {
    if (const AchievementDef *def = FindAchievementDef(id)) {
      m_toastPanel.PushToast(*m_ctx, def->name, def->description);
    }
  }
  return true;
}

void AchievementManager::SaveProgress() { AchievementStore::Save(m_progress); }

} // namespace game::systems
