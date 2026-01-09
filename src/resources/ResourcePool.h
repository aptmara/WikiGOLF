#pragma once
/**
 * @file ResourcePool.h
 * @brief 世代管理付きリソースプール (Slot Map pattern)
 */

#include "../core/Logger.h"
#include "../core/ResourceHandle.h"
#include <cassert>
#include <deque> // Changed from queue to deque (queue uses deque by default but we need explicit deque)
#include <functional>
#include <iostream>
#include <queue>
#include <type_traits>
#include <vector>


namespace resources {

/// @brief リソース管理プール
/// @tparam T リソース型 (Movableであること)
template <typename T> class ResourcePool {
public:
  using Handle = core::ResourceHandle<T>;

  /// @brief コンストラクタ
  /// @param dummyFallback エラー時に返すダミーリソース（Releaseビルド用）
  ResourcePool(T &&dummyFallback) : m_dummy(std::move(dummyFallback)) {
    // dequeにはreserveがないが、パフォーマンスへの影響は軽微
  }

  // コピー禁止
  ResourcePool(const ResourcePool &) = delete;
  ResourcePool &operator=(const ResourcePool &) = delete;

  /// @brief リソースを追加し、ハンドルを返す
  /// @param resource リソース実体（Move）
  Handle Add(T &&resource) {
    uint32_t index;
    if (!m_freeIndices.empty()) {
      index = m_freeIndices.front();
      m_freeIndices.pop();
    } else {
      index = static_cast<uint32_t>(m_slots.size());
      m_slots.emplace_back();
    }

    Slot &slot = m_slots[index];
    slot.generation++; // 世代を進める（古いハンドルを無効化）
    slot.resource = std::move(resource);
    slot.isAlive = true;

    return {index, slot.generation};
  }

  /// @brief リソースを取得 (安全なポインタアクセス)
  /// @param handle リソースハンドル
  /// @return
  /// リソースへのポインタ。無効な場合はDebugではAssert、ReleaseではDummyを返す。
  T *Get(Handle handle) {
    // インデックス範囲チェック
    if (handle.index >= m_slots.size()) {
      return HandleError("インデックス範囲外");
    }

    Slot &slot = m_slots[handle.index];

    // 生存チェック & 世代チェック
    if (!slot.isAlive || slot.generation != handle.generation) {
      return HandleError("無効な世代または破棄されたリソース");
    }

    return &slot.resource;
  }

  /// @brief リソースを解放
  void Remove(Handle handle) {
    if (handle.index >= m_slots.size())
      return;

    Slot &slot = m_slots[handle.index];
    if (slot.isAlive && slot.generation == handle.generation) {
      slot.isAlive = false;

      // 安全のためデフォルト構築したもので上書きしてリソース解放を促進
      if constexpr (std::is_default_constructible_v<T>) {
        slot.resource = T();
      }

      m_freeIndices.push(handle.index);
    }
  }

  /// @brief 全リソースを解放（シーン遷移用）
  void Clear() {
    m_slots.clear();
    m_freeIndices = {};
    // ダミーは残る
  }

private:
  struct Slot {
    T resource;
    uint32_t generation = 0;
    bool isAlive = false;
  };

  // std::dequeを使用することで、要素追加時のメモリアドレス無効化を防ぐ
  std::deque<Slot> m_slots;
  std::queue<uint32_t> m_freeIndices;
  T m_dummy; // フォールバック用ダミーリソース

  /// @brief エラーハンドリング
  T *HandleError(const char *message) {
#ifdef _DEBUG
    // デバッグ時は即停止
    std::cerr << "[ResourcePool Error] " << message
              << " Type: " << typeid(T).name() << std::endl;
    LOG_ERROR("ResourcePool", "Error: {} (Type: {})", message,
              typeid(T).name());
    assert(false && "不正なリソースアクセス");
    return nullptr; // ここには来ない
#else
    // リリース時はログを出してダミーを返す
    static bool logged = false;
    if (!logged) {
      std::cerr << "[ResourcePool Error] " << message << " (ダミーを返します)"
                << std::endl;
      logged = true; // ログは1回だけ
    }
    return &m_dummy;
#endif
  }
};

} // namespace resources
