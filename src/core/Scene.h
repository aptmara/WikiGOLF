#pragma once
/**
 * @file Scene.h
 * @brief シーン基底クラス
*/

#include "../ecs/EntityOwner.h"
#include "../ecs/World.h"


namespace core {

// 前方宣言
struct GameContext;

/**
 * @brief シーン基底クラス
 * @details シーン固有のエンティティを管理し、シーン遷移時に自動クリーンアップ
*/
class Scene {
public:
  virtual ~Scene() = default;

  /** @brief シーン名（デバッグ用）*/
  virtual const char *GetName() const = 0;

  /** @brief シーン開始時に呼ばれる（エンティティ作成など）*/
  virtual void OnEnter(GameContext &ctx) = 0;

  /** @brief シーン終了時に呼ばれる*/
  virtual void OnExit(GameContext &ctx) { DestroyAllEntities(ctx); }

  /** @brief 毎フレーム更新（オプション）*/
  virtual void OnUpdate(GameContext &ctx) {}

  /** @brief 毎フレーム描画（オプション）*/
  virtual void Render(GameContext &ctx) {}

  /** @brief BeginFrame直後・Skybox/メインメッシュ描画前に呼ばれるオフスクリーン描画（オプション）*/
  virtual void RenderOffscreen(GameContext &ctx) {}

  /** @brief trueの場合、このシーンの背面にあるUIへの入力を遮断する。*/
  virtual bool BlocksUnderlyingInput() const { return false; }

  /** @brief 指定エンティティがこのシーンで作成されたものか。*/
  bool OwnsEntity(ecs::Entity entity) const {
    return m_entityOwner.Owns(entity);
  }

protected:
  /** @brief エンティティを作成し、追跡リストに追加*/
  ecs::Entity CreateEntity(ecs::World &world) {
    return m_entityOwner.Create(world);
  }

  /** @brief このシーンが作成した全エンティティを破棄*/
  void DestroyAllEntities(GameContext &ctx);

  ecs::EntityOwner m_entityOwner;
};

} // namespace core
