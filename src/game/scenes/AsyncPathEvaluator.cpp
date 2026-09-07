/**
 * @file AsyncPathEvaluator.cpp
 * @brief 非同期経路評価の実装
*/

#include "AsyncPathEvaluator.h"
#include "../../core/Logger.h"
#include "../systems/WikiShortestPath.h"
#include <algorithm>
#include <chrono>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace game::scenes {
namespace {

constexpr std::size_t kDepthProgressUnits = 100;

long long ElapsedMilliseconds(
    const std::chrono::steady_clock::time_point &startedAt) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now() - startedAt)
      .count();
}

} // namespace

void AsyncPathEvaluator::Start(
    const std::vector<HolePlacementCandidate> &sourceCandidates,
    int targetPageId, int maxDepth, std::uint64_t loadId) {
  if (m_started) {
    return;
  }

  m_started = true;
  m_progress = std::make_shared<std::atomic<std::size_t>>(0);
  m_total = std::make_shared<std::atomic<std::size_t>>(
      sourceCandidates.size() +
      static_cast<std::size_t>(std::max(0, maxDepth)) * kDepthProgressUnits);
  m_partialMutex = std::make_shared<std::mutex>();
  m_partialResults =
      std::make_shared<std::vector<HolePlacementCandidate>>();
  m_cancelRequested = std::make_shared<std::atomic<bool>>(false);
  m_consumedPartialCount = 0;

  auto candidates = sourceCandidates;
  std::unordered_map<std::string, std::vector<HolePlacementCandidate>>
      candidatesByTarget;
  for (const HolePlacementCandidate &candidate : candidates) {
    if (!candidate.linkTarget.empty()) {
      candidatesByTarget[candidate.linkTarget].push_back(candidate);
    }
  }
  auto progress = m_progress;
  auto total = m_total;
  auto partialMutex = m_partialMutex;
  auto partialResults = m_partialResults;
  auto cancelRequested = m_cancelRequested;

  m_task = std::async(
      std::launch::async,
      [candidates = std::move(candidates), targetPageId, progress, total,
       loadId, maxDepth, partialMutex, partialResults, cancelRequested,
       candidatesByTarget = std::move(candidatesByTarget)]() mutable {
        const auto taskStartedAt = std::chrono::steady_clock::now();
        LOG_INFO("WikiPageLoader",
                 "Path evaluation task started: loadId={} candidates={} "
                 "targetId={} maxDepth={}",
                 loadId, candidates.size(), targetPageId, maxDepth);
        if (targetPageId == -1 || candidates.empty()) {
          progress->store(total->load(std::memory_order_relaxed),
                          std::memory_order_relaxed);
          LOG_WARN("WikiPageLoader",
                   "Path evaluation task skipped: loadId={} candidates={} "
                   "targetId={} maxDepth={}",
                   loadId, candidates.size(), targetPageId, maxDepth);
          return candidates;
        }

        game::systems::WikiShortestPath pathSystem;
        const auto databaseStartedAt = std::chrono::steady_clock::now();
        if (!pathSystem.Initialize("Assets/data/jawiki_sdow-001.sqlite",
                                   false)) {
          progress->store(total->load(std::memory_order_relaxed),
                          std::memory_order_relaxed);
          LOG_ERROR("WikiPageLoader",
                    "Path evaluation DB initialize failed: loadId={} "
                    "elapsed={}ms",
                    loadId, ElapsedMilliseconds(databaseStartedAt));
          return candidates;
        }
        LOG_INFO("WikiPageLoader",
                 "Path evaluation DB initialized: loadId={} elapsed={}ms",
                 loadId, ElapsedMilliseconds(databaseStartedAt));

        std::vector<std::string> linkTargets;
        std::unordered_set<std::string> seenTargets;
        linkTargets.reserve(candidates.size());
        std::size_t emptyTargets = 0;
        std::size_t duplicateTargets = 0;
        for (std::size_t index = 0; index < candidates.size(); ++index) {
          const HolePlacementCandidate &candidate = candidates[index];
          if (candidate.linkTarget.empty()) {
            ++emptyTargets;
            progress->store(index + 1, std::memory_order_relaxed);
            continue;
          }
          if (!seenTargets.insert(candidate.linkTarget).second) {
            ++duplicateTargets;
            progress->store(index + 1, std::memory_order_relaxed);
            continue;
          }
          linkTargets.push_back(candidate.linkTarget);
          progress->store(index + 1, std::memory_order_relaxed);
        }

        total->store(candidates.size() + linkTargets.size() +
                         static_cast<std::size_t>(std::max(0, maxDepth)) *
                             kDepthProgressUnits,
                     std::memory_order_relaxed);
        LOG_INFO("WikiPageLoader",
                 "Path evaluation targets prepared: loadId={} candidates={} "
                 "unique={} empty={} duplicates={} totalUnits={}",
                 loadId, candidates.size(), linkTargets.size(), emptyTargets,
                 duplicateTargets, total->load(std::memory_order_relaxed));

        const auto distanceStartedAt = std::chrono::steady_clock::now();
        const auto distances = pathSystem.ComputeDistancesToTarget(
            linkTargets, targetPageId, maxDepth, progress.get(),
            candidates.size(),
            [&](const std::string &resolvedTitle, int hopsToTarget) {
              if (!partialMutex || !partialResults) {
                return;
              }
              const auto targetIterator =
                  candidatesByTarget.find(resolvedTitle);
              if (targetIterator == candidatesByTarget.end()) {
                return;
              }
              std::lock_guard<std::mutex> lock(*partialMutex);
              for (const HolePlacementCandidate &candidate :
                   targetIterator->second) {
                HolePlacementCandidate resolved = candidate;
                resolved.hopsToTarget = hopsToTarget;
                resolved.isPlayable = true;
                partialResults->push_back(std::move(resolved));
              }
            },
            cancelRequested.get());
        LOG_INFO("WikiPageLoader",
                 "Path evaluation distance map ready: loadId={} "
                 "uniqueTargets={} resolved={} maxDepth={} elapsed={}ms",
                 loadId, linkTargets.size(), distances.size(), maxDepth,
                 ElapsedMilliseconds(distanceStartedAt));

        std::size_t assignedDistances = 0;
        for (HolePlacementCandidate &candidate : candidates) {
          const auto distance = distances.find(candidate.linkTarget);
          candidate.hopsToTarget = -1;
          if (distance != distances.end()) {
            candidate.hopsToTarget = distance->second;
            ++assignedDistances;
          }
        }
        progress->store(total->load(std::memory_order_relaxed),
                        std::memory_order_relaxed);
        LOG_INFO("WikiPageLoader",
                 "Path evaluation task finished: loadId={} candidates={} "
                 "assigned={} unresolved={} elapsed={}ms",
                 loadId, candidates.size(), assignedDistances,
                 candidates.size() - assignedDistances,
                 ElapsedMilliseconds(taskStartedAt));
        return candidates;
      });

  LOG_INFO("WikiPageLoader",
           "Path evaluation launched in background: loadId={} targets={} "
           "maxDepth={}",
           loadId, sourceCandidates.size(), maxDepth);
}

void AsyncPathEvaluator::Cancel() {
  if (m_cancelRequested) {
    m_cancelRequested->store(true, std::memory_order_relaxed);
  }
}

void AsyncPathEvaluator::ResetForNewLoad(std::uint64_t loadId) {
  RetireActiveTask(loadId);
  m_progress.reset();
  m_total.reset();
  m_partialMutex.reset();
  m_partialResults.reset();
  m_consumedPartialCount = 0;
  m_started = false;
}

std::optional<std::vector<HolePlacementCandidate>>
AsyncPathEvaluator::TryConsumeCompleted() {
  CollectRetiredTasks();
  if (!m_task.valid() ||
      m_task.wait_for(std::chrono::milliseconds(0)) !=
          std::future_status::ready) {
    return std::nullopt;
  }
  auto result = m_task.get();
  m_cancelRequested.reset();
  return result;
}

std::vector<HolePlacementCandidate> AsyncPathEvaluator::ConsumePartial() {
  std::vector<HolePlacementCandidate> partial;
  if (!m_partialMutex || !m_partialResults) {
    return partial;
  }
  std::lock_guard<std::mutex> lock(*m_partialMutex);
  if (m_consumedPartialCount >= m_partialResults->size()) {
    return partial;
  }
  partial.assign(
      m_partialResults->begin() +
          static_cast<std::vector<HolePlacementCandidate>::difference_type>(
              m_consumedPartialCount),
      m_partialResults->end());
  m_consumedPartialCount = m_partialResults->size();
  return partial;
}

void AsyncPathEvaluator::PrepareNextEvaluation() { m_started = false; }

float AsyncPathEvaluator::GetProgress() const {
  if (!m_progress || !m_total) {
    return 0.0f;
  }
  const float completed =
      static_cast<float>(m_progress->load(std::memory_order_relaxed));
  const float total = std::max(
      1.0f, static_cast<float>(m_total->load(std::memory_order_relaxed)));
  return std::clamp(completed / total, 0.0f, 1.0f);
}

void AsyncPathEvaluator::RetireActiveTask(std::uint64_t loadId) {
  if (!m_task.valid()) {
    return;
  }
  if (m_task.wait_for(std::chrono::milliseconds(0)) ==
      std::future_status::ready) {
    (void)m_task.get();
    m_cancelRequested.reset();
    return;
  }
  Cancel();
  LOG_WARN("WikiPageLoader",
           "Path evaluation task retired without blocking: loadId={}",
           loadId);
  m_retiredTasks.push_back(std::move(m_task));
  m_cancelRequested.reset();
}

void AsyncPathEvaluator::CollectRetiredTasks() {
  for (auto iterator = m_retiredTasks.begin();
       iterator != m_retiredTasks.end();) {
    if (iterator->valid() &&
        iterator->wait_for(std::chrono::milliseconds(0)) ==
            std::future_status::ready) {
      (void)iterator->get();
      iterator = m_retiredTasks.erase(iterator);
    } else {
      ++iterator;
    }
  }
}

} // namespace game::scenes
