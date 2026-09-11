#pragma once
/**
 * @file AchievementEventBus.h
 * @brief 実績イベントの薄いpub-subバス
 * @details メインロジック側はPublish()だけを呼ぶ。購読側（AchievementManager）が
 *          Subscribe()で処理を登録する。呼び出しはすべてメインスレッド前提
 *          （非同期処理の結果は、既存のRankingScene等と同様に一度メインスレッドの
 *          OnUpdate側でatomicフラグを確認してからPublishすること）。
*/

#include "AchievementEvent.h"
#include <functional>
#include <vector>

namespace game::systems {

/** @brief 実績イベントの発行・購読を仲介するバス。*/
class AchievementEventBus {
public:
  using Listener = std::function<void(const AchievementEvent &)>;

  /** @brief イベントの購読者を登録します。*/
  void Subscribe(Listener listener);

  /** @brief イベントを発行し、登録済みの購読者全員へ同期的に通知します。*/
  void Publish(const AchievementEvent &event);

private:
  std::vector<Listener> m_listeners;
};

} // namespace game::systems
