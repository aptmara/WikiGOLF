#pragma once
/**
 * @file View.h
 * @brief コンポーネントクエリ・イテレータ
 */

#include "ComponentPool.h"
#include <tuple>
#include <algorithm>

namespace ecs {

/// @brief 指定したコンポーネントセットを持つエンティティへのビュー
/// @tparam Ts 必要なコンポーネント型
template<typename... Ts>
class View {
public:
    View(ComponentPool<Ts>*... pools) : m_pools(pools...) {}

    /// @brief イテレーション実行
    /// @param func コールバック関数 (Entity, Ts&...)
    template<typename Func>
    void Each(Func&& func) {
        // プールが1つでも欠けていれば何もしない
        if (!IsValid()) return;

        // 最小のプールを探す
        size_t minSize = (std::numeric_limits<size_t>::max)();
        IComponentPool* smallestPool = nullptr;
        
        // テンプレートパラメータパックを展開して最小プールを特定
        FindSmallestPool<0>(minSize, smallestPool);

        if (!smallestPool) return;

        // 最小プールのエンティティリストでループ
        // ここでの型安全性の確保は少しトリックが必要だが、
        // どのプール由来のEntityであっても、HasAllチェックを通れば安全。
        const auto& entities = smallestPool->Entities();

        for (Entity e : entities) {
            if (HasAll(e)) {
                // 全コンポーネントを取得してコールバック呼び出し
                std::apply([&](auto*... pools) {
                    func(e, *pools->Get(e)...);
                }, m_pools);
            }
        }
    }

private:
    std::tuple<ComponentPool<Ts>*...> m_pools;

    bool IsValid() const {
        // 全てのプールポインタが有効かチェック
        return std::apply([](auto*... pools) {
            return (... && (pools != nullptr));
        }, m_pools);
    }

    bool HasAll(Entity e) const {
        return std::apply([&](auto*... pools) {
            return (... && pools->Has(e));
        }, m_pools);
    }

    // 最小プール探索用
    template<size_t I>
    void FindSmallestPool(size_t& minSize, IComponentPool*& result) {
        if constexpr (I < sizeof...(Ts)) {
            auto* pool = std::get<I>(m_pools);
            if (pool && pool->Size() < minSize) {
                minSize = pool->Size();
                result = pool;
            }
            FindSmallestPool<I + 1>(minSize, result);
        }
    }
};

} // namespace ecs
