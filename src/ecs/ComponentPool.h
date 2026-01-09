#pragma once
/**
 * @file ComponentPool.h
 * @brief Sparse Setベースの高効率コンポーネントプール
 * 
 * 設計：
 * - Sparse配列: EntityIndex → DenseIndex のマッピング
 * - Dense配列: 連続したコンポーネントデータ（キャッシュフレンドリー）
 * - Entity配列: DenseIndex → Entity のマッピング（逆引き用）
 * 
 * 計算量：
 * - has/get/add/remove: O(1)
 * - イテレーション: O(n) where n = コンポーネント数
 */

#include "Entity.h"
#include <vector>
#include <cassert>
#include <type_traits>
#include <limits>

namespace ecs {

/// @brief Sparse Set用の無効インデックス定数
constexpr size_t INVALID_SPARSE_INDEX = (std::numeric_limits<size_t>::max)();

/// @brief 型消去されたコンポーネントプールの基底クラス
class IComponentPool {
public:
    virtual ~IComponentPool() = default;
    virtual void Remove(Entity entity) = 0;
    virtual bool Has(Entity entity) const = 0;
    virtual size_t Size() const = 0;
    virtual void Clear() = 0;
    
    // Type-erased access required for View optimization
    virtual const std::vector<Entity>& Entities() const = 0;

    // Debug / Statistics
    virtual const char* GetTypeName() const = 0;
};

/// @brief Sparse Setベースのコンポーネントプール
/// @tparam T コンポーネント型（POD推奨）
template<typename T>
class ComponentPool final : public IComponentPool {
public:
    static_assert(std::is_default_constructible_v<T>, "Tはデフォルト構築可能である必要があります");

    ComponentPool() {
        m_sparse.resize(1024, INVALID_SPARSE_INDEX); // 初期容量
    }

    /// @brief エンティティがこのコンポーネントを持っているか
    bool Has(Entity entity) const override {
        const uint16_t index = GetEntityIndex(entity);
        if (index >= m_sparse.size()) return false;
        const size_t denseIndex = m_sparse[index];
        return denseIndex != INVALID_SPARSE_INDEX && denseIndex < m_dense.size() && m_entities[denseIndex] == entity;
    }

    /// @brief コンポーネントを取得（存在しない場合はnullptr）
    T* Get(Entity entity) {
        if (!Has(entity)) return nullptr;
        return &m_dense[m_sparse[GetEntityIndex(entity)]];
    }

    const T* Get(Entity entity) const {
        if (!Has(entity)) return nullptr;
        return &m_dense[m_sparse[GetEntityIndex(entity)]];
    }

    /// @brief コンポーネントを追加（既に存在する場合は上書き）
    template<typename... Args>
    T& Add(Entity entity, Args&&... args) {
        const uint16_t index = GetEntityIndex(entity);
        
        // Sparse配列を必要に応じて拡張
        if (index >= m_sparse.size()) {
            m_sparse.resize(static_cast<size_t>(index) + 1, INVALID_SPARSE_INDEX);
        }

        if (Has(entity)) {
            // 既存のコンポーネントを上書き
            T& component = m_dense[m_sparse[index]];
            component = T{std::forward<Args>(args)...};
            return component;
        }

        // 新規追加
        const size_t denseIndex = m_dense.size();
        m_sparse[index] = denseIndex;
        m_dense.emplace_back(std::forward<Args>(args)...);
        m_entities.push_back(entity);

        return m_dense.back();
    }

    /// @brief コンポーネントを削除
    void Remove(Entity entity) override {
        if (!Has(entity)) return;

        const uint16_t index = GetEntityIndex(entity);
        const size_t denseIndex = m_sparse[index];
        const size_t lastIndex = m_dense.size() - 1;

        if (denseIndex != lastIndex) {
            // 最後の要素と入れ替え（Swap & Pop）
            m_dense[denseIndex] = std::move(m_dense[lastIndex]);
            m_entities[denseIndex] = m_entities[lastIndex];
            m_sparse[GetEntityIndex(m_entities[denseIndex])] = denseIndex;
        }

        m_dense.pop_back();
        m_entities.pop_back();
        m_sparse[index] = INVALID_SPARSE_INDEX;
    }

    /// @brief コンポーネント数を取得
    size_t Size() const override {
        return m_dense.size();
    }

    /// @brief Dense配列の先頭イテレータ
    auto begin() { return m_dense.begin(); }
    auto begin() const { return m_dense.begin(); }

    /// @brief Dense配列の終端イテレータ
    auto end() { return m_dense.end(); }
    auto end() const { return m_dense.end(); }

    /// @brief エンティティ配列を取得（イテレーション用）
    const std::vector<Entity>& Entities() const override { return m_entities; }

    /// @brief Dense配列を直接取得（バッチ処理用）
    std::vector<T>& Data() { return m_dense; }
    const std::vector<T>& Data() const { return m_dense; }

    /// @brief 内部データをクリア（リソース破棄はTのデストラクタに依存）
    void Clear() override {
        m_dense.clear();
        m_entities.clear();
        std::fill(m_sparse.begin(), m_sparse.end(), INVALID_SPARSE_INDEX);
    }

    /// @brief コンポーネント型名を取得（RTTI使用）
    const char* GetTypeName() const override {
        return typeid(T).name();
    }

private:
    std::vector<size_t> m_sparse;   ///< EntityIndex → DenseIndex
    std::vector<T> m_dense;          ///< 連続したコンポーネントデータ
    std::vector<Entity> m_entities;  ///< DenseIndex → Entity
};

} // namespace ecs
