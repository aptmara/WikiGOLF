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
#include "../systems/WikiClient.h"
#include "../systems/WikiShortestPath.h"
#include "../systems/WikiTerrainSystem.h"
#include "../utils/MapViewState.h"
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
  ~WikiGolfScene() override;

  void OnEnter(core::GameContext &ctx) override;
  void OnUpdate(core::GameContext &ctx) override;
  void OnExit(core::GameContext &ctx) override;

private:
  /// @brief ボールをスポーン
  void SpawnBall(core::GameContext &ctx);

  /// @brief フィールド（床・壁）作成
  void CreateField(core::GameContext &ctx);

  /// @brief ホール（リンク）作成
  void CreateHole(core::GameContext &ctx, float x, float z,
                  const std::string &linkTarget, bool isTargetHole);

  /// @brief 記事テキスト背景UIセットアップ
  void SetupArticleBackground(core::GameContext &ctx);

  /// @brief ショット処理（パワーゲージ）
  void ProcessShot(core::GameContext &ctx);

  /// @brief ショット実行（判定確定後）
  void ExecuteShot(core::GameContext &ctx);

  /// @brief ページ遷移（値渡し：ホール削除後も安全に使用するため）
  void TransitionToPage(core::GameContext &ctx, std::string pageName);

  /// @brief カップイン判定（ボールがホール内で静止したか）
  void CheckCupIn(core::GameContext &ctx);

  /// @brief クラブ切り替え
  /// @param direction 1: 次へ, -1: 前へ
  void SwitchClub(core::GameContext &ctx, int direction);

  /// @brief テクスチャからリンク領域を作成
  void CreateLinksFromTexture(core::GameContext &ctx);

  /// @brief カメラ更新（ボール追従）
  void UpdateCamera(core::GameContext &ctx);

  /// @brief ハイスコア保存
  void SaveHighScore(const std::string &targetPage, int shots);

  /// @brief ハイスコア読み込み
  int LoadHighScore(const std::string &targetPage);

  ecs::Entity m_ballEntity = UINT32_MAX; // 無効値で初期化（ID競合防止）
  ecs::Entity m_floorEntity = UINT32_MAX;
  ecs::Entity m_cameraEntity = UINT32_MAX;
  ecs::Entity m_arrowEntity = UINT32_MAX; // 矢印表示用（パワーチャージ時）
  ecs::Entity m_guideArrowEntity =
      UINT32_MAX; // 方向ガイド（アイドル時常時表示）
  ecs::Entity m_clubModelEntity = UINT32_MAX; // ゴルフクラブ3Dモデル

  // === クラブアニメーション制御 ===
  enum class ClubAnimPhase {
    Idle,         ///< 待機（クラブ非表示または構え）
    Backswing,    ///< バックスイング（振りかぶり）
    Downswing,    ///< ダウンスイング（振り下ろし）
    FollowThrough ///< フォロースルー
  };
  ClubAnimPhase m_clubAnimPhase = ClubAnimPhase::Idle;
  float m_clubSwingAngle = 0.0f; ///< 現在のスイング角度（度）
  float m_clubSwingSpeed = 0.0f; ///< スイング速度
  float m_clubAnimTimer = 0.0f;  ///< アニメーションタイマー

  /// @brief クラブモデルの初期化
  void InitializeClubModel(core::GameContext &ctx);
  /// @brief クラブアニメーション更新
  void UpdateClubAnimation(core::GameContext &ctx, float dt);

  // カメラ制御（TPSオービットカメラ）
  float m_cameraYaw = 0.0f;   // 水平回転角度（ラジアン）
  float m_cameraPitch = 0.5f; // 垂直回転角度（ラジアン、初期値: 少し見下ろし）
  float m_cameraDistance = 60.0f;       // カメラ距離
  float m_targetCameraDistance = 60.0f; // クラブ別の推奨カメラ距離
  float m_targetCameraHeight = 20.0f;   // クラブ別の推奨カメラ高さ
  DirectX::XMFLOAT3 m_shotDirection = {
      0, 0, 1}; // ショット方向（カメラ前方ベクトルから自動計算）

  // クラブ定義
  struct Club {
    std::string name;
    float maxPower;          // 最大パワー
    float launchAngle;       // 打ち出し角度 (度)
    std::string iconTexture; // アイコンテクスチャ名
  };

  Club m_currentClub = {"Driver", 30.0f, 30.0f,
                        "icon_driver.png"}; // デフォルト
  int m_currentClubIndex = 0;               // 現在のクラブインデックス
  std::vector<Club> m_availableClubs;
  std::vector<ecs::Entity> m_clubUIEntities;

  void InitializeClubs(core::GameContext &ctx);

  // 軌道予測
  std::vector<ecs::Entity> m_trajectoryDots;
  void UpdateTrajectory(core::GameContext &ctx, float powerRatio);

  // 初回ロード用キャッシュ（シーン遷移時のラグ解消用）
  bool m_hasPreloadedData = false;
  std::vector<game::WikiLink> m_preloadedLinks;
  std::string m_preloadedExtract;

  // テクスチャ関連
  std::unique_ptr<graphics::WikiTextureGenerator> m_textureGenerator;
  std::unique_ptr<graphics::WikiTextureResult> m_wikiTexture;

  // 最短パス計算（SDOW）
  std::unique_ptr<game::systems::WikiShortestPath> m_shortestPath;
  int m_calculatedPar = -1; ///< API計算されたパー（-1=未計算/DB未使用）

  // 俯瞰マップビュー
  float m_fieldWidth = 80.0f;
  float m_fieldDepth = 120.0f;
  float m_mapZoom = 1.0f;
  DirectX::XMFLOAT3 m_mapCenterOffset = {0.0f, 0.0f, 0.0f};
  bool m_isMapView = false;
  game::utils::MapViewSkyboxState
      m_mapViewSkyboxState; ///< マップビュー時のスカイボックス制御
  int m_prevMouseX = 0;
  int m_prevMouseY = 0; // Yも追加

  float m_judgeDisplayTimer = 0.0f; // 判定表示タイマー

  // カメラ追従用
  DirectX::XMFLOAT3 m_shotStartCamPos = {0, 0, 0}; // ショット瞬間のカメラ位置
  bool m_isCameraChasing = false;                  // 現在追尾モードに入ったか

  /// @brief 俯瞰カメラ更新
  void UpdateMapCamera(core::GameContext &ctx);

  // ミニマップ（右上常時表示）
  std::unique_ptr<game::systems::MapSys> m_minimapRenderer;

  ecs::Entity m_minimapEntity; ///< ミニマップ表示用UIエンティティ

  /// @brief ミニマップ更新・描画
  void UpdateMinimap(core::GameContext &ctx);

  // === Game Juice システム（演出効果） ===
  std::unique_ptr<game::systems::GameJuiceSystem> m_gameJuice;

  // === Wiki Terrain システム（地形生成） ===
  std::unique_ptr<game::systems::WikiTerrainSystem> m_terrainSystem;

  // === Skybox システム（背景スカイボックス） ===
  std::unique_ptr<graphics::SkyboxTextureGenerator> m_skyboxGenerator;
  ecs::Entity m_skyboxEntity = UINT32_MAX; ///< スカイボックスエンティティ

  /// @brief UI要素の初期化（エンティティ作成）
  void InitializeUI(core::GameContext &ctx,
                    game::components::GolfGameState &state);

  /// @brief ガイドUI更新
  void UpdateGuideUI(core::GameContext &ctx);

  /// @brief ページ読み込み・テクスチャ生成・フィールド更新
  void LoadPage(core::GameContext &ctx, const std::string &pageName);
};

} // namespace game::scenes
