#pragma once
/**
 * @file World.h
 * @brief ECSデータストア（Entity, Component, Global Data）
 */

#include "Entity.h"
#include "ComponentPool.h"
#include "ComponentRegistry.h"
#include "View.h"
#include "../core/Logger.h"
#include <vector>
#include <queue>
#include <unordered_map>
#include <memory>
#include <any>
#include <type_traits>

namespace ecs {

/// @brief ECSデータ管理クラス
class World {
public:
    World() = default;
    ~World() { Reset(); }

    // コピー禁止
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    /// @brief 全データを破棄し、初期状態に戻す
    void Reset() {
        for (auto& pool : m_componentPools) {
            if (pool) pool->Clear();
        }
        m_globals.clear();
        
        m_generations.clear();
        m_freeIndices = {};
    }

    //==========================================================================
    // Entity管理
    //==========================================================================

    /// @brief 新しいエンティティを生成
    /// @return 生成されたEntity ID
    Entity CreateEntity() {
        Entity entity;
        if (!m_freeIndices.empty()) {
            const uint16_t index = m_freeIndices.front();
            m_freeIndices.pop();
            const uint16_t generation = m_generations[index];
            entity = MakeEntity(index, generation);
        } else {
            const uint16_t index = static_cast<uint16_t>(m_generations.size());
            m_generations.push_back(0);
            entity = MakeEntity(index, 0);
        }
        return entity;
    }

    /// @brief エンティティを破棄
    /// @param entity 破棄するEntity ID
    void DestroyEntity(Entity entity) {
        if (!IsAlive(entity)) return;

        for (auto& pool : m_componentPools) {
            if (pool) pool->Remove(entity);
        }

        const uint16_t index = GetEntityIndex(entity);
        m_generations[index]++; // 世代を進める
        m_freeIndices.push(index);
    }

    bool IsAlive(Entity entity) const {
        if (!IsValidEntity(entity)) return false;
        const uint16_t index = GetEntityIndex(entity);
        if (index >= m_generations.size()) return false;
        return m_generations[index] == GetEntityGeneration(entity);
    }

    //==========================================================================
    // Component管理
    //==========================================================================

    /// @brief コンポーネントを追加（または上書き）
    /// @tparam T コンポーネント型
    /// @return 追加されたコンポーネントへの参照
    template<typename T, typename... Args>
    T& Add(Entity entity, Args&&... args) {
        return GetOrCreatePool<T>().Add(entity, std::forward<Args>(args)...);
    }

    /// @brief コンポーネントを取得
    /// @return コンポーネントへのポインタ（存在しなければnullptr）
    template<typename T>
    T* Get(Entity entity) {
        auto* pool = GetPool<T>();
        return pool ? pool->Get(entity) : nullptr;
    }

    template<typename T>
    const T* Get(Entity entity) const {
        auto* pool = GetPool<T>();
        return pool ? pool->Get(entity) : nullptr;
    }

    /// @brief コンポーネントを削除
    template<typename T>
    void Remove(Entity entity) {
        if (auto* pool = GetPool<T>()) {
            pool->Remove(entity);
        }
    }

    template<typename T>
    bool Has(Entity entity) const {
        auto* pool = GetPool<T>();
        return pool && pool->Has(entity);
    }

    //==========================================================================
    // Global Data管理
    //==========================================================================

    template<typename T>
    void SetGlobal(T&& value) {
        using Decayed = std::decay_t<T>;
        ComponentTypeId id = GetComponentTypeId<Decayed>();
        if constexpr (std::is_copy_constructible_v<Decayed>) {
            m_globals[id] = std::make_any<Decayed>(std::forward<T>(value));
        } else {
            m_globals[id] = std::make_any<std::shared_ptr<Decayed>>(
                std::make_shared<Decayed>(std::forward<T>(value)));
        }
    }

    template<typename T>
    T* GetGlobal() {
        using Decayed = std::decay_t<T>;
        ComponentTypeId id = GetComponentTypeId<Decayed>();
        auto it = m_globals.find(id);
        if (it == m_globals.end()) return nullptr;
        if constexpr (std::is_copy_constructible_v<Decayed>) {
            return std::any_cast<Decayed>(&it->second);
        } else {
            auto shared = std::any_cast<std::shared_ptr<Decayed>>(&it->second);
            return (shared && *shared) ? shared->get() : nullptr;
        }
    }

    //==========================================================================
    // Query & View
    //==========================================================================

    template<typename... Ts>
    View<Ts...> Query() {
        return View<Ts...>(GetPool<Ts>()...);
    }

    //==========================================================================
    // 統計情報 (Health Check)
    //==========================================================================

    void DumpStatistics() const {
        // アクティブなエンティティ数をカウント
        size_t activeCount = 0;
        size_t totalSlots = m_generations.size();
        // freeIndicesにあるものは非アクティブ
        // ただし queue なので size() で取れるが、本来は generations.size() - freeIndices.size() 
        // m_freeIndices は std::queue なので size() がある
        activeCount = totalSlots - m_freeIndices.size();

        LOG_INFO("WorldStats", "=== World Statistics ===");
        LOG_INFO("WorldStats", "Entities: {} active / {} total slots", activeCount, totalSlots);
        LOG_INFO("WorldStats", "Global Data: {} entries", m_globals.size());
        LOG_INFO("WorldStats", "Component Pools: {} types", m_componentPools.size());

        for (size_t i = 0; i < m_componentPools.size(); ++i) {
            if (auto& pool = m_componentPools[i]) {
                LOG_INFO("WorldStats", "  - [ID:{}] {}: {} entities", 
                    i, pool->GetTypeName(), pool->Size());
            }
        }
        LOG_INFO("WorldStats", "========================");
    }

private:
    template<typename T>
    ComponentPool<T>& GetOrCreatePool() {
        ComponentTypeId id = GetComponentTypeId<T>();
        if (id >= m_componentPools.size()) {
            m_componentPools.resize(id + 1);
        }
        if (!m_componentPools[id]) {
            m_componentPools[id] = std::make_unique<ComponentPool<T>>();
        }
        return *static_cast<ComponentPool<T>*>(m_componentPools[id].get());
    }

    template<typename T>
    ComponentPool<T>* GetPool() {
        ComponentTypeId id = GetComponentTypeId<T>();
        if (id >= m_componentPools.size()) return nullptr;
        return static_cast<ComponentPool<T>*>(m_componentPools[id].get());
    }

    template<typename T>
    const ComponentPool<T>* GetPool() const {
        ComponentTypeId id = GetComponentTypeId<T>();
        if (id >= m_componentPools.size()) return nullptr;
        return static_cast<const ComponentPool<T>*>(m_componentPools[id].get());
    }

    // Data Storage
    std::vector<uint16_t> m_generations;
    std::queue<uint16_t> m_freeIndices;
    std::vector<std::unique_ptr<IComponentPool>> m_componentPools; // Optimized: map -> vector
    std::unordered_map<ComponentTypeId, std::any> m_globals;
};

} // namespace ecs
