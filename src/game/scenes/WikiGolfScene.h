#pragma once
/**
 * @file WikiGolfScene.h
 * @brief WikiGolfのメインゲームシーン
 */

#include "../../core/Scene.h"
#include "../../ecs/Entity.h"
#include "../../graphics/SkyboxTextureGenerator.h"
#include "../../graphics/WikiTextureGenerator.h"
#include "../systems/GameJuiceSystem.h"
#include "../systems/MapSys.h"
#include "../systems/ParticleRenderSystem.h"
#include "../controllers/TrajectoryPredictor.h"
#include "../controllers/CameraController.h"
#include "../controllers/ClubController.h"
#include "../controllers/MinimapController.h"
#include "../controllers/ShotController.h"
#include "../controllers/ArticleTransitionController.h"
#include "../controllers/WikiGolfHUD.h"
#include "WikiPageLoader.h"
#include "../controllers/TutorialOverlayController.h"

#include "../systems/ParticleSystem.h"
#include "../systems/PostProcessSystem.h"
#include "../systems/TimeOfDaySystem.h"
#include "../systems/WikiClient.h"
#include "../systems/WikiShortestPath.h"
#include "../systems/WikiTerrainSystem.h"
#include "../utils/MapViewState.h"
#include "../utils/ScreenFade.h"
#include <DirectXMath.h>
#include <memory>
#include <string>
#include <vector>

namespace core {
struct GameContext;
}

namespace game::components {
struct GolfGameState;
}

namespace game::scenes {

/**
 * @brief WikiGolfのゲームプレイシーンクラス
 */
class WikiGolfScene : public core::Scene {
public:
  const char *GetName() const override { return "WikiGolfScene"; }
  explicit WikiGolfScene(bool isTutorial = false);
  ~WikiGolfScene() override;

  void OnEnter(core::GameContext &ctx) override;
  void OnUpdate(core::GameContext &ctx) override;
  void Render(core::GameContext &ctx) override;
  
  void OnExit(core::GameContext &ctx) override;

private:
  /// @brief ボールをスポーン
  void SpawnBall(core::GameContext &ctx);

  /// @brief フィールド（床・壁）作成
  void CreateField(core::GameContext &ctx);

  /// @brief ページ遷移（値渡し：ホール削除後も安全に使用するため）
  void TransitionToPage(core::GameContext &ctx, const std::string &pageName);

  /// @brief カップイン判定（ボールがホール内で静止したか）
  /// @return 遷移が発生した場合はtrue
  bool CheckCupIn(core::GameContext &ctx);

  ecs::Entity m_ballEntity = UINT32_MAX;
  ecs::Entity m_floorEntity = UINT32_MAX;
  ecs::Entity m_cameraEntity = UINT32_MAX;
  ecs::Entity m_arrowEntity = UINT32_MAX; // 矢印表示用（パワーチャージ時）
  ecs::Entity m_guideArrowEntity = UINT32_MAX; // 方向ガイド（アイドル時常時表示）
  ecs::Entity m_clubModelEntity = UINT32_MAX;
  
  std::unique_ptr<game::controllers::CameraController>    m_cameraController;
  std::unique_ptr<game::controllers::MinimapController>   m_minimapController;
  std::unique_ptr<game::controllers::WikiGolfHUD>          m_hud;
  std::unique_ptr<game::controllers::ClubController>       m_clubController;
  std::unique_ptr<game::controllers::TrajectoryPredictor>  m_trajectoryPredictor;
  std::unique_ptr<game::controllers::ShotController>       m_shotController;
  std::unique_ptr<WikiPageLoader>                          m_pageLoader;
  std::unique_ptr<game::controllers::ArticleTransitionController> m_transitionController;
  std::unique_ptr<game::controllers::TutorialOverlayController>   m_tutorialOverlay;

  bool m_isTutorial = false;

  enum class ScenePhase { Playing, Transitioning };
  ScenePhase m_phase = ScenePhase::Playing;

  /// @brief カメラの衝突判定（地形・壁）を行い、位置を補正する
  /// @param ctx ゲームコンテキスト
  /// @param targetPos カメラの目標位置
  /// @param lookAtPos 注視点（ボール位置）
  /// @param outPos 補正後のカメラ位置
  /// @return 補正が行われたか
  bool CheckCameraCollision(core::GameContext &ctx,
                            const DirectX::XMVECTOR &targetPos,
                            const DirectX::XMVECTOR &lookAtPos,
                            DirectX::XMVECTOR &outPos);

  // === Game Juice システム（演出効果） ===
  std::unique_ptr<game::systems::GameJuiceSystem> m_gameJuice;

  // === Wiki Terrain システム（地形生成） ===
  std::unique_ptr<game::systems::WikiTerrainSystem> m_terrainSystem;

  // === Skybox システム（背景スカイボックス） ===
  std::unique_ptr<graphics::SkyboxTextureGenerator> m_skyboxGenerator;
  ecs::Entity m_skyboxEntity = UINT32_MAX; ///< スカイボックスエンティティ

  // === Environment システム（環境効果） ===
  game::systems::EnvironmentParticleSystem m_particleSystem;
  game::systems::ParticleRenderSystem m_particleRenderSystem;
  game::systems::PostProcessSystem m_postProcess;
  game::systems::TimeOfDaySystem m_timeOfDay;
  graphics::SkyboxTheme m_currentSkyboxTheme = graphics::SkyboxTheme::Default;

  // === テクスチャ・パス探索 ===
  std::unique_ptr<graphics::WikiTextureGenerator> m_textureGenerator;
  std::unique_ptr<game::systems::WikiShortestPath> m_shortestPath;
  
  game::utils::ScreenFade m_screenFade;
  
  ecs::Entity m_terrainImageEntity = UINT32_MAX;
  float m_terrainDisplayTimer = 0.0f;
  float m_hudUpdateTimer = 0.0f;     ///< HUD静的表示の更新間引きタイマーです。山内陽
  float m_minimapUpdateTimer = 0.0f; ///< ミニマップ描画の更新間引きタイマーです。山内陽
  bool m_prevTutorialInputLocked = false; ///< チュートリアル演出入力ロックの解除検知用です。山内陽
  /// @brief チュートリアル中にカップイン SE が既に再生済みかどうか。
  /// @details 毎フレーム CheckCupIn が走るためボールがカップに留まり続けると連打される。
  ///          このフラグで1回だけ処理させる。
  bool m_tutorialCupInFired = false;

  /// @brief ページ読み込み（WikiPageLoader へ委譲）
  
};

} // namespace game::scenes
