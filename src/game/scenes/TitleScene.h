#pragma once
/**
 * @file TitleScene.h
 * @brief タイトル画面シーン
 */

#include "../../core/Scene.h"
#include "../../ecs/Entity.h"
#include "../../graphics/TextStyle.h"
#include <DirectXMath.h>
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
  // --- エンティティ ---
  ecs::Entity m_cameraEntity = 0; ///< カメラ
  ecs::Entity m_skyboxEntity = 0; ///< スカイボックス
  ecs::Entity m_floorEntity = 0;  ///< 地面
  ecs::Entity m_ballEntity = 0;   ///< 演出用ボール

  // --- 豪華演出用 ---
  std::vector<ecs::Entity> m_clubs;       ///< 周回するクラブ
  std::vector<ecs::Entity> m_ringObjects; ///< 背景の巨大リング
  std::vector<ecs::Entity> m_reflections; ///< 鏡面反射用ダミー
  ecs::Entity m_globeEntity = 0;          ///< Wikipedia地球儀

  // --- UI管理 ---
  struct UIElement {
    ecs::Entity entity;
    float baseX, baseY;          ///< 初期位置
    float currentScale;          ///< 現在のスケール
    float targetScale;           ///< 目標スケール
    bool isHovered;              ///< ホバー状態
    bool isClicked;              ///< クリック状態（一瞬）
    std::wstring text;           ///< 表示テキスト
    DirectX::XMFLOAT4 baseColor; ///< ベース色
  };
  std::vector<UIElement> m_uiElements;

  // --- パーティクル演出 ---
  struct Particle {
    ecs::Entity entity;
    DirectX::XMFLOAT3 basePos;
    float phase;
    float speed;
  };
  std::vector<Particle> m_particles;

  // --- 演出制御 ---
  float m_time = 0.0f;

  // カメラ演出用
  float m_cameraPitch = 0.0f;
  float m_cameraDist = 0.0f;
  float m_cameraHeight = 0.0f;
};

} // namespace game::scenes
