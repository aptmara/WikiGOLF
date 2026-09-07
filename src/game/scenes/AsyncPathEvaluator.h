#pragma once
/**
 * @file AsyncPathEvaluator.h
 * @brief ホール候補から目的記事までの距離を非同期評価するクラス
*/

#include "HolePlacementPlanner.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace game::scenes {

/**
 * @brief 経路探索スレッド、キャンセル、部分結果、進捗を所有します。
 * @details ECSへの反映は呼び出し側へ返し、このクラスではバックグラウンド
 *          計算の寿命と結果受け渡しだけを扱います。
*/
class AsyncPathEvaluator {
public:
  /** @brief 未開始の場合に候補の経路評価を開始します。*/
  void Start(const std::vector<HolePlacementCandidate> &candidates,
             int targetPageId, int maxDepth, std::uint64_t loadId);

  /** @brief 実行中の評価へキャンセルを要求します。*/
  void Cancel();

  /** @brief 現在のタスクを待たずに退役させ、新しいロード用へ初期化します。*/
  void ResetForNewLoad(std::uint64_t loadId);

  /** @brief 完了済みなら最終結果を一度だけ返します。*/
  std::optional<std::vector<HolePlacementCandidate>> TryConsumeCompleted();

  /** @brief 前回取得後に追加された部分結果を返します。*/
  std::vector<HolePlacementCandidate> ConsumePartial();

  /** @brief 開始済みフラグを戻し、次の深度で再評価可能にします。*/
  void PrepareNextEvaluation();

  /** @brief 現在の評価が開始済みか返します。*/
  bool HasStarted() const { return m_started; }

  /** @brief 消費前の有効なfutureを保持しているか返します。*/
  bool HasActiveTask() const { return m_task.valid(); }

  /** @brief 現在の進捗を0～1で返します。*/
  float GetProgress() const;

private:
  /**
   * @brief 実行中のタスクをリタイアキューへ移動します。
   * @param loadId ロード識別子
*/
  void RetireActiveTask(std::uint64_t loadId);

  /**
   * @brief 完了済みのリタイアタスクを回収します。
*/
  void CollectRetiredTasks();

  std::future<std::vector<HolePlacementCandidate>> m_task;
  std::vector<std::future<std::vector<HolePlacementCandidate>>> m_retiredTasks;
  std::shared_ptr<std::atomic<bool>> m_cancelRequested;
  std::shared_ptr<std::atomic<std::size_t>> m_progress;
  std::shared_ptr<std::atomic<std::size_t>> m_total;
  std::shared_ptr<std::mutex> m_partialMutex;
  std::shared_ptr<std::vector<HolePlacementCandidate>> m_partialResults;
  std::size_t m_consumedPartialCount = 0;
  bool m_started = false;
};

} // namespace game::scenes
