#pragma once
/**
 * @file Scene.h
 * @brief シーン基底クラス
 */

#include <vector>
#include "../ecs/Entity.h"
#include "../ecs/World.h"

namespace core {

// 前方宣言
struct GameContext;

/// @brief シーン基底クラス
/// @details シーン固有のエンティティを管理し、シーン遷移時に自動クリーンアップ
class Scene {
public:
    virtual ~Scene() = default;

    /// @brief シーン名（デバッグ用）
    virtual const char* GetName() const = 0;

    /// @brief シーン開始時に呼ばれる（エンティティ作成など）
    virtual void OnEnter(GameContext& ctx) = 0;

    /// @brief シーン終了時に呼ばれる
    virtual void OnExit(GameContext& ctx) {
        DestroyAllEntities(ctx);
    }

    /// @brief 毎フレーム更新（オプション）
    virtual void OnUpdate(GameContext& ctx) {}

protected:
    /// @brief エンティティを作成し、追跡リストに追加
    ecs::Entity CreateEntity(ecs::World& world) {
        auto e = world.CreateEntity();
        m_entities.push_back(e);
        return e;
    }

    /// @brief このシーンが作成した全エンティティを破棄
    void DestroyAllEntities(GameContext& ctx);

    std::vector<ecs::Entity> m_entities;
};

} // namespace core
