#pragma once
/**
 * @file TitleScene.h
 * @brief タイトル画面シーン
 */

#include "../../core/Scene.h"
#include "../../ecs/Entity.h"
#include "../../graphics/TextStyle.h"
#include <vector>

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
  ecs::Entity m_subTextEntity = 0;   ///< サブタイトル
  ecs::Entity m_startEntity = 0;     ///< スタートボタン
  ecs::Entity m_hintEntity = 0;      ///< ヒントテキスト
  ecs::Entity m_footerEntity = 0;    ///< フッターメッセージ
  ecs::Entity m_cameraEntity = 0;    ///< タイトル用カメラ
  ecs::Entity m_ballEntity = 0;      ///< デコボール
  ecs::Entity m_clubEntity = 0;      ///< デコクラブ
  ecs::Entity m_floorEntity = 0;     ///< 床
  std::vector<ecs::Entity> m_decorEntities; ///< 装飾用エンティティ
  std::vector<ecs::Entity> m_lightBeams;    ///< 背景ライト
  std::vector<ecs::Entity> m_badgeEntities; ///< 特徴表示プレート

  graphics::TextStyle m_titleStyle{};
  graphics::TextStyle m_subStyle{};
  graphics::TextStyle m_startStyle{};
  graphics::TextStyle m_hintStyle{};
  graphics::TextStyle m_badgeStyle{};

  float m_time = 0.0f;
  float m_lightPhase = 0.0f;
};

} // namespace game::scenes
