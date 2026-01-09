#pragma once
/**
 * @file Pipeline.h
 * @brief システム実行パイプライン（依存関係管理・実行順序制御）
 */

#include "../core/GameContext.h"
#include "../core/Logger.h"
#include "ComponentRegistry.h"
#include <vector>
#include <string>
#include <functional>
#include <algorithm>
#include <cassert>

namespace ecs {

enum class Phase {
    PreUpdate,
    Update,
    PostUpdate,
    Render,
    Cleanup
};

using SystemFunc = std::function<void(core::GameContext&)>;

struct SystemDesc {
    std::string name;
    SystemFunc func;
    Phase phase = Phase::Update;
    std::vector<ComponentTypeId> reads;
    std::vector<ComponentTypeId> writes;
};

class Pipeline;

/// @brief システム構築用ビルダークラス
class SystemBuilder {
public:
    SystemBuilder(Pipeline& pipeline, std::string name, SystemFunc func)
        : m_pipeline(pipeline) {
        m_desc.name = std::move(name);
        m_desc.func = std::move(func);
    }

    SystemBuilder& Phase(Phase p) {
        m_desc.phase = p;
        return *this;
    }

    template<typename T>
    SystemBuilder& Reads() {
        m_desc.reads.push_back(GetComponentTypeId<T>());
        return *this;
    }

    template<typename T>
    SystemBuilder& Writes() {
        m_desc.writes.push_back(GetComponentTypeId<T>());
        return *this;
    }

    void Build();

private:
    Pipeline& m_pipeline;
    SystemDesc m_desc;
};

class Pipeline {
public:
    SystemBuilder Register(const std::string& name, SystemFunc func) {
        return SystemBuilder(*this, name, func);
    }

    void AddSystem(SystemDesc&& desc) {
        m_systems.push_back(std::move(desc));
    }

    /// @brief 依存関係を検証し、実行順序を確定する
    void Build() {
        // フェーズ順にソート
        std::sort(m_systems.begin(), m_systems.end(), 
            [](const SystemDesc& a, const SystemDesc& b) {
                return a.phase < b.phase;
            });

        // 簡易的な依存チェック（ログ出力）
        LOG_INFO("Pipeline", "Pipeline Built: {} systems", m_systems.size());
        for (const auto& system : m_systems) {
            LOG_INFO("Pipeline", "  - [Phase:{}] {}", (int)system.phase, system.name.c_str());
            // 将来的にここで競合チェックロジックを追加可能
        }
    }

    void Run(core::GameContext& ctx) {
        for (const auto& system : m_systems) {
            system.func(ctx);
        }
    }

private:
    std::vector<SystemDesc> m_systems;
};

inline void SystemBuilder::Build() {
    m_pipeline.AddSystem(std::move(m_desc));
}

} // namespace ecs
