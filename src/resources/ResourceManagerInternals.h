/**
 * @file ResourceManagerInternals.h
 * @brief ResourceManager 内部で共有する時間計測処理
 */
#pragma once

#include <chrono>

namespace resources {

/// @brief 開始時刻からの経過時間をミリ秒で返します。
inline long long ElapsedMs(
    const std::chrono::steady_clock::time_point &startedAt) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now() - startedAt)
      .count();
}

} // namespace resources

