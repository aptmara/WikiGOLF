#pragma once
/**
 * @file TitleScene.h
 * @brief タイトル画面シーン
 */

#include "../../core/Scene.h"
#include "../../ecs/Entity.h"

namespace game::scenes {

/**
 * @brief ゲームのタイトル画面シーンクラス
 */
class TitleScene : public core::Scene {
public:
  const char *GetName() const override { return "TitleScene"; }

  void OnEnter(core::GameContext &ctx) override;
  void OnUpdate(core::GameContext &ctx) override;
  void OnExit(core::GameContext &ctx) override;

private:
  ecs::Entity m_bgEntity = 0;        ///< 背景エンティティ
  ecs::Entity m_titleTextEntity = 0; ///< タイトルテキスト
  ecs::Entity m_startEntity = 0;     ///< スタートボタン
  ecs::Entity m_exitEntity = 0;      ///< 終了ボタン
};

} // namespace game::scenes
