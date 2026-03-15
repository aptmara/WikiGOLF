#include "WikiGolfScene.h"
#include "../../audio/AudioSystem.h"
#include "../../core/GameContext.h"
#include "../../core/Input.h"
#include "../../core/Logger.h"
#include "../../core/SceneManager.h"
#include "../../core/StringUtils.h"
#include "../../ecs/World.h"
#include "../../graphics/GraphicsDevice.h"
#include "../../graphics/TextRenderer.h"
#include "../../graphics/WikiTextureGenerator.h"
#include "../components/Camera.h"
#include "../components/EnvironmentPresets.h"
#include "../components/MeshRenderer.h"
#include "../components/PhysicsComponents.h"
#include "../components/Skybox.h"
#include "../components/Transform.h"
#include "../components/UIButton.h"
#include "../components/UIImage.h"
#include "../components/UIText.h"
#include "../components/WikiComponents.h"
#include "../systems/PhysicsFriction.h"
#include "../systems/PhysicsSystem.h"
#include "../systems/SkyboxRenderSystem.h"
#include "../systems/WikiClient.h"
#include "../utils/JudgeFeedback.h"
#include "CupInUtils.h"
#include "ResultScene.h"
#include "TitleScene.h"
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>

// Windowsマクロ対策
#undef min
#undef max

namespace game::scenes {

using namespace DirectX;
using namespace game::components;

namespace {
constexpr float kFieldScale = 4.0f;
constexpr float kMinMapViewSpan = 5.0f; // マップビューでこれ以上縮まらない幅
} // namespace

WikiGolfScene::~WikiGolfScene() = default;

void WikiGolfScene::OnEnter(core::GameContext &ctx) {
  LOG_INFO("WikiGolf", "OnEnter");

  // フェードシステム初期化 & シーン開始フェード (ヘキサゴン)
  m_screenFade.Initialize(ctx);
  m_screenFade.FadeIn(1.5f, game::utils::FadeType::HexagonWipe,
                      {1.0f, 1.0f, 1.0f}); // 白で明ける

  // BGM再生
  if (ctx.audio) {
    ctx.audio->PlayBGM(ctx, "bgm_game.mp3", 0.3f);
  }

  // シーン遷移時にマウスカーソルを表示・ロック解除（UIモード）
  ctx.input.SetMouseCursorVisible(true);
  ctx.input.SetMouseCursorLocked(false);

  // === 残存エンティティの強制クリーンアップ ===
  // LoadingSceneなどのエンティティが残っている場合があるため、全て削除する
  // カメラ生成などの前に実行する必要がある
  std::vector<ecs::Entity> strayEntities;
  ctx.world.Query<components::Transform>().Each(
      [&](ecs::Entity e, components::Transform &) {
        strayEntities.push_back(e);
      });
  for (auto e : strayEntities) {
    if (ctx.world.IsAlive(e)) {
      ctx.world.DestroyEntity(e);
    }
  }
  LOG_INFO("WikiGolf", "Cleaned up {} stray entities", strayEntities.size());

  m_textureGenerator = std::make_unique<graphics::WikiTextureGenerator>();
  m_textureGenerator->Initialize(ctx.graphics.GetDevice());

  // カメラ（ボール追従）
  m_cameraEntity = CreateEntity(ctx.world);
  auto &t = ctx.world.Add<Transform>(m_cameraEntity);
  t.position = {
      0.0f, 15.0f * kFieldScale,
      -15.0f * kFieldScale}; // 後ろ上から（UpdateCameraで即座に更新される）
  LOG_DEBUG("WikiGolf", "Camera initial pos: ({}, {}, {})", t.position.x,
            t.position.y, t.position.z);

  LOG_DEBUG("WikiGolf", "Camera created. ID={}, Alive={}", m_cameraEntity,
            ctx.world.IsAlive(m_cameraEntity) ? "true" : "false");

  auto &camComp = ctx.world.Add<Camera>(m_cameraEntity);
  camComp.fov = XMConvertToRadians(60.0f);
  camComp.aspectRatio = 1280.0f / 720.0f;
  camComp.nearZ = 0.1f;
  camComp.farZ = 750.0f; // 描画距離を拡張（5倍）

  // カメラ初期状態（TPSオービットカメラ）
  m_cameraYaw = 0.0f;                     // 初期方向: 北（Z+方向）
  m_cameraPitch = 0.5f;                   // 初期角度: 少し見下ろし（約28.6度）
  m_cameraDistance = 15.0f * kFieldScale; // 初期距離
  m_shotDirection = {0.0f, 0.0f, 1.0f};   // 初期ショット方向

  // ミニマップ初期化
  if (!m_minimapRenderer) {
    m_minimapRenderer = std::make_unique<game::systems::MapSys>();
    if (!m_minimapRenderer->Initialize(ctx.graphics.GetDevice(), 720, 720)) {
      m_minimapRenderer.reset();
    } else {
      LOG_INFO("WikiGolf", "Minimap initialized");
    }
  }

  // 矢印（ショット予測線）
  m_arrowEntity = CreateEntity(ctx.world);
  auto &at = ctx.world.Add<Transform>(m_arrowEntity);
  at.scale = {0.0f, 0.0f, 0.0f}; // 最初は非表示

  // 方向ガイド矢印（常時表示、Idle時のみ可視）
  m_guideArrowEntity = CreateEntity(ctx.world);
  auto &gat = ctx.world.Add<Transform>(m_guideArrowEntity);
  gat.scale = {0.15f, 0.1f, 2.0f}; // 細長い矢印

  auto &gamr = ctx.world.Add<MeshRenderer>(m_guideArrowEntity);
  gamr.mesh = ctx.resource.LoadMesh("builtin/cube");
  gamr.shader = ctx.resource.LoadShader("Basic", L"Assets/shaders/BasicVS.hlsl",
                                        L"Assets/shaders/BasicPS.hlsl");
  gamr.color = {0.3f, 0.8f, 1.0f, 0.6f}; // シアン半透明
  gamr.isVisible = false;                // 初期は非表示、OnUpdateで制御

  // 軌道予測用（ドットのプール作成）
  m_trajectoryDots.clear();
  for (int i = 0; i < 30; ++i) {
    auto e = CreateEntity(ctx.world);
    auto &t = ctx.world.Add<Transform>(e);
    t.scale = {0.15f, 0.15f, 0.15f}; // 少し大きく

    auto &mr = ctx.world.Add<MeshRenderer>(e);
    mr.mesh = ctx.resource.LoadMesh("builtin/cube");
    mr.shader = ctx.resource.LoadShader("Basic", L"Assets/shaders/BasicVS.hlsl",
                                        L"Assets/shaders/BasicPS.hlsl");
    mr.color = {1.0f, 1.0f, 0.0f, 0.9f}; // 明るい黄色で高透明度
    mr.isVisible = false;

    m_trajectoryDots.push_back(e);
  }

  auto &amr = ctx.world.Add<MeshRenderer>(m_arrowEntity);
  amr.mesh = ctx.resource.LoadMesh("builtin/cube");
  amr.shader = ctx.resource.LoadShader("Basic", L"Assets/shaders/BasicVS.hlsl",
                                       L"Assets/shaders/BasicPS.hlsl");
  amr.color = {1.0f, 0.4f, 0.2f, 1.0f}; // 明るい赤橙で強調
  amr.isVisible = false;

  // クラブ初期化
  InitializeClubs(ctx);

  // クラブ3Dモデル初期化
  InitializeClubModel(ctx);

  // Game Juice System
  // （ユニークポインタはメンバ変数だが、PhysicsSystemから参照のためにGlobal登録）
  m_gameJuice = std::make_unique<game::systems::GameJuiceSystem>();
  m_gameJuice->Initialize(ctx);
  ctx.world.SetGlobal(m_gameJuice.get()); // グローバル登録

  // === Wiki Terrain システム初期化 ===
  m_terrainSystem = std::make_unique<game::systems::WikiTerrainSystem>();

  // === Skybox システム初期化 ===
  m_skyboxGenerator = std::make_unique<graphics::SkyboxTextureGenerator>();

  // スカイボックスエンティティ作成
  m_skyboxEntity = CreateEntity(ctx.world);
  auto &skyboxComp = ctx.world.Add<components::Skybox>(m_skyboxEntity);
  skyboxComp.isVisible = true;
  skyboxComp.brightness = 0.7f; // 床の文字を見やすくするため控えめ
  skyboxComp.saturation = 0.8f; // 彩度も抑えめ

  // 事前生成された静的六面スカイボックスからロード
  // ファイル形式: skybox_Default_{px,nx,py,ny,pz,nz}.png
  std::wstring skyboxBasePath =
      L"Assets/textures/runtime_skybox/skybox_Default";
  if (m_skyboxGenerator->LoadCubemapFromFiles(
          ctx.graphics.GetDevice(), skyboxBasePath, skyboxComp.cubemapSRV)) {
    LOG_INFO("WikiGolf", "Skybox loaded from static files: skybox_Default");
  } else {
    LOG_WARN("WikiGolf", "Failed to load skybox from static files");
    skyboxComp.isVisible = false;
  }
  m_mapViewSkyboxState.Reset(skyboxComp.isVisible);

  // === Environment システム初期化 ===
  m_timeOfDay.Initialize(8.0f); // 朝8時スタート
  m_postProcess.Initialize(ctx.graphics.GetDevice());
  m_postProcess.ResetToDefaults();

  // パーティクルレンダラー初期化
  m_particleRenderSystem.Initialize(ctx.graphics.GetDevice());
  // パーティクルはLoadPage時にテーマに応じて設定

  // ゲーム状態初期化
  // ターゲット記事選択（SDOWデータベース優先）
  std::string targetPage;
  int targetId = -1;
  bool isUserOverride = false;

  // 事前ロードデータの確認
  game::components::WikiGlobalData *preloadedData =
      ctx.world.GetGlobal<game::components::WikiGlobalData>();
  std::string startPage;

  if (preloadedData && preloadedData->pathSystem) {
    LOG_INFO("WikiGolf", "Using preloaded data. Start: {}, Target: {}",
             preloadedData->startPage, preloadedData->targetPage);
    m_shortestPath = std::move(preloadedData->pathSystem);
    startPage = preloadedData->startPage;
    targetPage = preloadedData->targetPage;
    targetId = preloadedData->targetPageId;
    isUserOverride = preloadedData->isUserOverride;

    // グローバルデータから削除（二重使用防止）
    // ただし ECSの実装上、コンポーネントを削除するのは面倒かもしれないので、
    // pathSystemがnullかチェックすることで再利用を防ぐ。
    // すでにmove済みなので pathSystem は null になっているはず。

    if (preloadedData->hasCachedData) {
      LOG_INFO("WikiGolf",
               "Found cached page data. Skipping initial network request.");
      m_hasPreloadedData = true;
      m_preloadedLinks = preloadedData->cachedLinks;
      m_preloadedExtract = preloadedData->cachedExtract;
    }
  } else {
    LOG_INFO("WikiGolf", "No preloaded data found or pathSystem invalid. "
                         "Falling back to sync load.");

    game::systems::WikiClient wikiClient;
    startPage = wikiClient.FetchRandomPageTitle();

    // まずSDOWデータベースを初期化して人気記事を取得
    if (!m_shortestPath) {
      m_shortestPath = std::make_unique<game::systems::WikiShortestPath>();
      if (!m_shortestPath->Initialize("Assets/data/jawiki_sdow.sqlite")) {
        LOG_WARN("WikiGolf", "SDOW DB not found for target selection");
        m_shortestPath.reset();
      }
    }

    if (m_shortestPath && m_shortestPath->IsAvailable()) {
      // 入力リンク数100以上の人気記事をターゲットに
      auto result = m_shortestPath->FetchPopularPageTitle(100);
      targetPage = result.first;
      targetId = result.second;

      if (targetPage.empty()) {
        // 閾値を下げて再試行
        result = m_shortestPath->FetchPopularPageTitle(50);
        targetPage = result.first;
        targetId = result.second;
      }
    }

    // フォールバック: Wikipedia APIから取得
    if (targetPage.empty()) {
      targetPage = wikiClient.FetchTargetPageTitle();
      // API経由の場合IDは不明（-1のまま）
    }

    if (startPage == targetPage) {
      targetPage = wikiClient.FetchTargetPageTitle();
      targetId = -1; // 再取得のためID不明
    }
  }

  // 最短距離が1記事以内のターゲットはスキップして再抽選（ユーザー指定時はスキップ）
  if (!isUserOverride && m_shortestPath && m_shortestPath->IsAvailable() && !startPage.empty() &&
      !targetPage.empty()) {
    const int maxRetry = 5;
    for (int attempt = 0; attempt < maxRetry; ++attempt) {
      game::systems::ShortestPathResult pathResult;
      if (targetId != -1) {
        pathResult = m_shortestPath->FindShortestPath(startPage, targetId, 6);
      } else {
        pathResult = m_shortestPath->FindShortestPath(startPage, targetPage, 6);
      }

      if (!pathResult.success) {
        LOG_WARN("WikiGolf",
                 "Shortest path check failed (attempt {}): {} (start={}, "
                 "target={})",
                 attempt + 1, pathResult.errorMessage, startPage, targetPage);
        break;
      }

      LOG_INFO("WikiGolf", "Shortest path to '{}' is {} hops from '{}'",
               targetPage, pathResult.degrees, startPage);

      if (pathResult.degrees > 1) {
        break; // 十分な距離
      }

      LOG_INFO("WikiGolf",
               "Target too close ({} hops). Re-selecting target... (attempt "
               "{}/{})",
               pathResult.degrees, attempt + 1, maxRetry);

      auto newTarget = m_shortestPath->FetchPopularPageTitle(100);
      if (newTarget.first.empty()) {
        newTarget = m_shortestPath->FetchPopularPageTitle(50);
      }

      if (newTarget.first.empty()) {
        game::systems::WikiClient fallbackClient;
        targetPage = fallbackClient.FetchTargetPageTitle();
        targetId = -1;
        LOG_INFO("WikiGolf",
                 "Fallback target selected via API after close-distance skip: "
                 "{}",
                 targetPage);
      } else {
        targetPage = newTarget.first;
        targetId = newTarget.second;
        LOG_INFO("WikiGolf", "New target candidate: {} (ID: {})", targetPage,
                 targetId);
      }
    }
  }

  LOG_INFO("WikiGolf", "Start: {}, Target: {} (ID: {})", startPage, targetPage,
           targetId);

  // フィールド作成

  LOG_DEBUG("WikiGolf", "After CreateField: Cam Alive={}",
            ctx.world.IsAlive(m_cameraEntity) ? "true" : "false");

  SpawnBall(ctx);
  LOG_DEBUG("WikiGolf", "After SpawnBall: Cam Alive={}",
            ctx.world.IsAlive(m_cameraEntity) ? "true" : "false");

  // マップの初期中心とズームをリセット（ボールを基準にする）
  if (auto *ballT = ctx.world.Get<Transform>(m_ballEntity)) {
    m_mapCenter = {ballT->position.x, ballT->position.z};
  } else {
    m_mapCenter = {0.0f, 0.0f};
  }
  m_mapZoom = 1.0f;
  m_targetMapZoom = 1.0f;
  m_mapPanVelocity = {0.0f, 0.0f};
  m_lastMapClickTime = 0.0f;
  m_mapBoundaryHitTime = 0.0f;
  m_markerPulseTimer = 0.0f;
  m_mapHelpVisible = false;
  m_minimapMarkerEntity = UINT32_MAX;

  GolfGameState state;
  state.currentPage = startPage;
  state.targetPage = targetPage;
  state.targetPageId = targetId; // ID保存
  state.pathHistory.clear();

  state.moveCount = 0;
  state.shotCount = 0;
  state.gameCleared = false;
  state.canShoot = true;
  state.ballEntity = m_ballEntity;
  state.windSpeed = 0.0f; // LoadPageで設定
  state.rollingFrictionScale = m_currentClub.rollingFrictionScale;

  // UI初期化
  InitializeUI(ctx, state);

  LOG_INFO("WikiGolf", "Saving global state...");
  ctx.world.SetGlobal(state);

  // ショット状態
  ShotState shotState;
  ctx.world.SetGlobal(shotState);

  // ページロード (par計算, updateHUD含む)
  LOG_DEBUG("WikiGolf", "Before LoadPage: Cam Alive={}",
            ctx.world.IsAlive(m_cameraEntity) ? "true" : "false");
  LoadPage(ctx, startPage);
  LOG_DEBUG("WikiGolf", "After LoadPage: Cam Alive={}",
            ctx.world.IsAlive(m_cameraEntity) ? "true" : "false");
}

void WikiGolfScene::CreateField(core::GameContext &ctx) {
  // 床（Wikipedia風の白背景）
  m_floorEntity = CreateEntity(ctx.world);
  auto &ft = ctx.world.Add<Transform>(m_floorEntity);
  ft.position = {0.0f, 0.0f, 0.0f};
  ft.scale = {20.0f * kFieldScale, 0.5f * kFieldScale, 30.0f * kFieldScale};

  auto &fmr = ctx.world.Add<MeshRenderer>(m_floorEntity);
  fmr.mesh = ctx.resource.LoadMesh("builtin/plane"); // 平面メッシュ
  fmr.shader = ctx.resource.LoadShader("Basic", L"Assets/shaders/BasicVS.hlsl",
                                       L"Assets/shaders/BasicPS.hlsl");
  fmr.color = {1.0f, 1.0f, 1.0f, 1.0f}; // 白
  LOG_DEBUG("WikiGolf", "Floor MeshRenderer: mesh={}, shader={}, visible={}",
            fmr.mesh.index, fmr.shader.index, fmr.isVisible);

  auto &frb = ctx.world.Add<RigidBody>(m_floorEntity);
  frb.isStatic = true;

  auto &fc = ctx.world.Add<Collider>(m_floorEntity);
  fc.type = ColliderType::Box;
  fc.size = {0.5f, 0.5f, 0.5f};
}

void WikiGolfScene::SpawnBall(core::GameContext &ctx) {
  // 古いエンティティがあれば削除（リスポーン時など）
  if (ctx.world.IsAlive(m_ballEntity)) {
    ctx.world.DestroyEntity(m_ballEntity);
  }

  m_ballEntity = CreateEntity(ctx.world);
  auto &t = ctx.world.Add<Transform>(m_ballEntity);
  t.position = {0.0f, 0.022f,
                -8.0f * kFieldScale}; // 地面(0.0) + 半径(0.02135) + マージン
  t.scale = {0.08f, 0.08f, 0.08f};
  LOG_DEBUG("WikiGolf", "Ball spawned at: ({}, {}, {})", t.position.x,
            t.position.y, t.position.z);

  auto &mr = ctx.world.Add<MeshRenderer>(m_ballEntity);
  mr.mesh = ctx.resource.LoadMesh("builtin/sphere");
  mr.shader = ctx.resource.LoadShader("Basic", L"shaders/BasicVS.hlsl",
                                      L"shaders/BasicPS.hlsl");
  mr.color = {1.0f, 0.6f, 0.2f, 1.0f}; // 少しオレンジで視認性アップ

  auto &rb = ctx.world.Add<RigidBody>(m_ballEntity);
  rb.isStatic = false;
  rb.mass = 0.0459f;      // 規定質量 45.9g
  rb.restitution = 0.35f; // 反発係数 (現実の芝との衝突)
  rb.drag = 0.30f;        // 空気抵抗係数 (Cd値)
  rb.rollingFriction =
      0.25f; // 転がり抵抗 (0.5->0.25へ低減、空気抵抗でバランスを取る)
  rb.velocity = {0, 0, 0};

  auto &c = ctx.world.Add<Collider>(m_ballEntity);
  c.type = ColliderType::Sphere;
  c.radius = 0.02135f; // 規定半径 21.35mm

  // グローバル状態のボール参照も更新
  auto *state = ctx.world.GetGlobal<GolfGameState>();
  if (state)
    state->ballEntity = m_ballEntity;
}

void WikiGolfScene::ProcessShot(core::GameContext &ctx) {
  auto *state = ctx.world.GetGlobal<GolfGameState>();
  auto *shot = ctx.world.GetGlobal<ShotState>();
  if (!state || !shot)
    return;

  // デバッグ：シーンでの入力受け取り確認
  if (ctx.input.GetMouseButtonDown(0)) {
    LOG_DEBUG("WikiGolfScene", "LMB Down Detected in ProcessShot");
  }

  const float dt = ctx.dt;

  // 判定結果表示中
  if (shot->phase == ShotState::Phase::ShowResult) {
    shot->resultDisplayTime -= dt;
    if (shot->resultDisplayTime <= 0.0f) {
      // 結果表示終了 -> カメラ復帰フェードへ
      shot->phase = ShotState::Phase::RestoringCamera;

      // サークルワイプで閉じる (黒)
      // ボール位置を中心にしたいが、スクリーン座標変換が必要。
      // 現状は簡易的に画面中央(0.5, 0.5)で閉じるか、
      // ProcessShot内で計算してSetCenterする。

      // ボールのスクリーン座標計算 (簡易版: 常に画面中央にあるため 0.5, 0.5
      // で概ねOKだが、 追従モードでボールがずれている可能性もある)
      // ここでは中央固定で実装し、後でブラッシュアップする。
      m_screenFade.SetCenter(0.5f, 0.5f);
      m_screenFade.FadeOut(0.4f, game::utils::FadeType::CircleWipe, {0, 0, 0});
    }
    return;
  }

  // ショット実行中（ボールが動いている間）
  if (shot->phase == ShotState::Phase::Executing) {
    auto *rb = ctx.world.Get<RigidBody>(m_ballEntity);
    if (rb) {
      float speed = std::sqrt(rb->velocity.x * rb->velocity.x +
                              rb->velocity.y * rb->velocity.y +
                              rb->velocity.z * rb->velocity.z);
      if (speed < 0.1f) {
        rb->velocity = {0, 0, 0};

        std::string terrainTex = "";

        // OB判定：Lavaに静止したらOB
        if (state->isOB) {
          terrainTex = "ui_judge_ob.png";

          LOG_INFO("WikiGolf", "OB! Returning to last shot position");
          state->shotCount++;
          auto *ballT = ctx.world.Get<Transform>(m_ballEntity);
          if (ballT) {
            ballT->position = state->lastShotPosition;
            ballT->position.y += 0.5f;
          }
          state->isOB = false;
          if (ctx.audio) {
            ctx.audio->PlaySE(ctx, "se_judge_ob.mp3", 0.8f);
          }
        } else {
          // 通常の地形判定
          switch (state->currentMaterial) {
          case game::components::TerrainMaterial::Fairway:
            terrainTex = "ui_terrain_fairway.png";
            break;
          case game::components::TerrainMaterial::Rough:
            terrainTex = "ui_terrain_rough.png";
            break;
          case game::components::TerrainMaterial::Bunker:
            terrainTex = "ui_terrain_bunker.png";
            break;
          case game::components::TerrainMaterial::Green:
            terrainTex = "ui_terrain_green.png";
            break;
          default:
            break;
          }
        }

        // 統合表示ロジック
        if (!terrainTex.empty() && m_terrainImageEntity != UINT32_MAX) {
          auto *ui =
              ctx.world.Get<game::components::UIImage>(m_terrainImageEntity);
          if (ui) {
            ui->texturePath = terrainTex;
            ui->visible = true;
            ui->alpha = 1.0f;

            // 初期状態: スケール0 (中央)
            ui->width = 0.0f;
            ui->height = 0.0f;
            ui->x = 1280.0f * 0.5f;
            ui->y = 720.0f * 0.5f;

            m_terrainDisplayTimer = 2.0f;

            // 統合SEロジック
            if (ctx.audio) {
              if (state->isOB) {
                // OBの場合は ProcessShot の冒頭で se_ob or se_cancel
                // を再生済みのためここでは再生しない
              } else {
                // Fairway / Green -> Good (judge_Good.mp3)
                // Rough / Bunker -> Bad (judge_Bad.wav)
                bool isGood = (state->currentMaterial ==
                                   game::components::TerrainMaterial::Fairway ||
                               state->currentMaterial ==
                                   game::components::TerrainMaterial::Green);

                if (isGood) {
                  ctx.audio->PlaySE(ctx, "judge_Good.mp3", 0.8f);
                } else {
                  ctx.audio->PlaySE(ctx, "judge_Bad.wav", 0.8f);
                }
              }
            }
          }
        }

        shot->phase = ShotState::Phase::ShowResult;
        shot->resultDisplayTime = 1.0f;
        state->canShoot = true;

        auto *infoUI = ctx.world.Get<UIText>(state->infoEntity);
        if (infoUI) {
          infoUI->text = L"クリックでショット";
        }
      }
    }
    return;
  }

  if (!state->canShoot)
    return;

  // クラブ切り替え入力 (Q/Eキーのみ、ホイールはマップビューのズームに使用)
  if (ctx.input.GetKeyUp('E')) {
    SwitchClub(ctx, 1);
  } else if (ctx.input.GetKeyUp('Q')) {
    SwitchClub(ctx, -1);
  }

  auto *camT = ctx.world.Get<Transform>(m_cameraEntity);
  auto *ballT = ctx.world.Get<Transform>(m_ballEntity);

  // === フェーズ別処理 ===

  switch (shot->phase) {
  case ShotState::Phase::Idle: {
    // 判定結果クリア
    auto *judgeUI = ctx.world.Get<UIImage>(state->judgeEntity);
    if (judgeUI)
      judgeUI->visible = false;

    // === UI誤爆防止: カーソル表示中（Altモード）はショット開始しない ===
    bool isUIMode = ctx.input.GetKey(VK_MENU);

    // 左クリックでパワーゲージ開始 (UIクリックでなく、かつUIモードでなければ)
    bool uiClicked = false;
    if (ctx.input.GetMouseButtonDown(0) && !isUIMode) {
      float mx = (float)ctx.input.GetMousePosition().x;
      float my = (float)ctx.input.GetMousePosition().y;

      for (size_t i = 0; i < m_clubUIEntities.size(); ++i) {
        auto *ui = ctx.world.Get<UIImage>(m_clubUIEntities[i]);
        if (ui) {
          if (mx >= ui->x && mx <= ui->x + ui->width && my >= ui->y &&
              my <= ui->y + ui->height) {

            // クラブ変更
            m_currentClub = m_availableClubs[i];
            uiClicked = true;
            LOG_INFO("WikiGolf", "Switched to club: {}", m_currentClub.name);

            // UI更新
            for (size_t j = 0; j < m_clubUIEntities.size(); ++j) {
              auto *uij = ctx.world.Get<UIImage>(m_clubUIEntities[j]);
              if (j == i) {
                uij->alpha = 1.0f;
              } else {
                uij->alpha = 0.5f;
              }
            }
            break;
          }
        }
      }
    }

    if (!uiClicked && ctx.input.GetMouseButtonDown(0)) {
      shot->phase = ShotState::Phase::PowerCharging;
      shot->powerGaugePos = 0.0f;
      shot->powerGaugeDir = 1.0f;
      LOG_INFO("WikiGolf", "Power charging started");
      if (ctx.audio)
        ctx.audio->PlaySE(ctx, "se_charge.mp3");

      if (ctx.audio)
        ctx.audio->PlaySE(ctx, "se_charge.mp3");

      // マーカー表示開始
      auto *markerUI = ctx.world.Get<UIImage>(state->gaugeMarkerEntity);
      if (markerUI) {
        markerUI->visible = true;
        markerUI->x = 450.0f - 8.0f; // 初期位置
      }
    }
    break;
  }

  case ShotState::Phase::PowerCharging: {
    // パワーゲージ往復
    shot->powerGaugePos += shot->powerGaugeDir * shot->powerGaugeSpeed * dt;
    if (shot->powerGaugePos >= 1.0f) {
      shot->powerGaugePos = 1.0f;
      shot->powerGaugeDir = -1.0f;
    } else if (shot->powerGaugePos <= 0.0f) {
      shot->powerGaugePos = 0.0f;
      shot->powerGaugeDir = 1.0f;
    }

    // ゲージFill更新
    auto *fillUI = ctx.world.Get<UIImage>(state->gaugeFillEntity);
    if (fillUI) {
      fillUI->width = 380.0f * shot->powerGaugePos;
    }

    // マーカー位置更新
    auto *markerUI = ctx.world.Get<UIImage>(state->gaugeMarkerEntity);
    if (markerUI) {
      markerUI->x = 450.0f - 8.0f + (380.0f * shot->powerGaugePos);
    }

    // UI更新
    auto *infoUI = ctx.world.Get<UIText>(state->infoEntity);
    if (infoUI) {
      int powerPct = (int)(shot->powerGaugePos * 100.0f);

      // 予想飛距離計算（簡易版）
      float power = shot->powerGaugePos * m_currentClub.maxPower;
      float launchAngle = XMConvertToRadians(m_currentClub.launchAngle);
      float v0 = power;
      // 空気抵抗なしの理想飛距離 = v0^2 * sin(2θ) / g
      float estimatedRange = (v0 * v0 * std::sin(2 * launchAngle)) / 9.8f;

      wchar_t buf[128];
      swprintf_s(buf, L"[パワー] %d%% / 飛距離: %.0fm", powerPct,
                 estimatedRange);
      infoUI->text = buf;
    }

    // パワー矢印の更新
    auto *arrowT = ctx.world.Get<Transform>(m_arrowEntity);
    auto *arrowMR = ctx.world.Get<MeshRenderer>(m_arrowEntity);
    if (arrowT && arrowMR && ballT) {
      arrowMR->isVisible = true;
      float length = shot->powerGaugePos * shot->maxPower * 0.15f;
      arrowT->scale = {0.2f, 0.1f, std::max(0.5f, length)};

      float yaw = std::atan2(m_shotDirection.x, m_shotDirection.z);
      XMVECTOR q = XMQuaternionRotationRollPitchYaw(0, yaw, 0);
      XMStoreFloat4(&arrowT->rotation, q);

      XMVECTOR ballPos = XMLoadFloat3(&ballT->position);
      XMVECTOR arrowPos = XMVectorAdd(ballPos, XMVectorSet(0, 0.2f, 0, 0));
      XMVECTOR offset =
          XMVectorScale(XMLoadFloat3(&m_shotDirection), length * 0.5f);
      arrowPos = XMVectorAdd(arrowPos, offset);
      XMStoreFloat3(&arrowT->position, arrowPos);

      // ゲージ更新
      auto *gauge = ctx.world.Get<UIBarGauge>(state->gaugeBarEntity);
      if (gauge) {
        gauge->isVisible = true;
        gauge->value = shot->powerGaugePos;
        gauge->showMarker = false;
        gauge->showImpactZones = false;
        // 色: パワーが上がるほど赤く
        float r = shot->powerGaugePos;
        float g = 1.0f - shot->powerGaugePos * 0.5f;
        float b = 0.2f;
        gauge->color = {r, g, b, 1.0f};
      }

      UpdateTrajectory(ctx, shot->powerGaugePos); // 予測線更新
    }

    // クリックでパワー決定
    if (ctx.input.GetMouseButtonDown(0)) {
      shot->confirmedPower = shot->powerGaugePos;
      shot->phase = ShotState::Phase::ImpactTiming;
      shot->impactGaugePos = 0.0f;
      shot->impactGaugeDir = 1.0f;

      // ゲージモード切り替え
      auto *gauge = ctx.world.Get<UIBarGauge>(state->gaugeBarEntity);
      if (gauge) {
        gauge->showImpactZones = true;
        gauge->showMarker = true;
        gauge->markerValue = 0.0f;
        gauge->value = shot->confirmedPower;
        gauge->color = {0.8f, 0.8f, 0.8f, 0.8f};
      }

      if (ctx.audio)
        ctx.audio->PlaySE(ctx, "se_shot_charge.mp3");
      LOG_INFO("WikiGolf", "Power confirmed: {:.2f}, Enter ImpactTiming",
               shot->confirmedPower);
    }

    // 右クリックでキャンセル（復元）
    if (ctx.input.GetMouseButtonDown(1)) {
      shot->phase = ShotState::Phase::Idle;
      auto *infoUI_c = ctx.world.Get<UIText>(state->infoEntity);
      if (infoUI_c)
        infoUI_c->text = L"[エイム] ドラッグで方向調整";

      auto *markerUI = ctx.world.Get<UIImage>(state->gaugeMarkerEntity);
      if (markerUI)
        markerUI->visible = false;

      auto *arrowMR_c = ctx.world.Get<MeshRenderer>(m_arrowEntity);
      if (arrowMR_c)
        arrowMR_c->isVisible = false;

      if (ctx.audio)
        ctx.audio->PlaySE(ctx, "se_cancel.mp3");
      LOG_INFO("WikiGolf", "Canceled shot");
    }
    break;
  }

  case ShotState::Phase::ImpactTiming: {
    // インパクトゲージ往復（高速）
    shot->impactGaugePos += shot->impactGaugeDir * shot->impactGaugeSpeed * dt;
    if (shot->impactGaugePos >= 1.0f) {
      shot->impactGaugePos = 1.0f;
      shot->impactGaugeDir = -1.0f;
    } else if (shot->impactGaugePos <= 0.0f) {
      shot->impactGaugePos = 0.0f;
      shot->impactGaugeDir = 1.0f;
    }

    // UI更新（インパクト位置表示）
    auto *infoUI = ctx.world.Get<UIText>(state->infoEntity);
    if (infoUI) {
      float offset = shot->impactGaugePos - 0.5f;
      std::wstring indicator;
      if (std::abs(offset) <= 0.02f)
        indicator = L"★ SPECIAL ★";
      else if (std::abs(offset) <= 0.05f)
        indicator = L"★ GREAT ★";
      else if (std::abs(offset) <= 0.15f)
        indicator = L"◎ NICE ◎";
      else
        indicator = L"○";
      infoUI->text = L"[インパクト] " + indicator;
    }

    // ゲージ・マーカー更新
    auto *gauge = ctx.world.Get<UIBarGauge>(state->gaugeBarEntity);
    if (gauge) {
      gauge->showImpactZones = true;
      gauge->showMarker = true;
      gauge->markerValue = shot->impactGaugePos; // マーカー移動

      UpdateTrajectory(ctx, shot->confirmedPower);
    }

    // クリックでインパクト確定→ショット実行
    if (ctx.input.GetMouseButtonDown(0)) {
      shot->confirmedImpact = shot->impactGaugePos;

      // 判定計算
      float impactError = std::abs(shot->confirmedImpact - 0.5f);
      if (impactError <= 0.02f) {
        shot->judgement = ShotJudgement::Special;
      } else if (impactError <= 0.05f) {
        shot->judgement = ShotJudgement::Great;
      } else if (impactError <= 0.15f) {
        shot->judgement = ShotJudgement::Nice;
      } else {
        shot->judgement = ShotJudgement::Miss;
      }

      LOG_INFO("WikiGolf", "Impact confirmed: {:.2f}, Judgement: {}",
               shot->confirmedImpact,
               shot->judgement == ShotJudgement::Great  ? "GREAT"
               : shot->judgement == ShotJudgement::Nice ? "NICE"
                                                        : "MISS");

      ExecuteShot(ctx);
    }
    break;
  }

  default:
    break;
  }
}

void WikiGolfScene::ExecuteShot(core::GameContext &ctx) {
  auto *state = ctx.world.GetGlobal<GolfGameState>();
  auto *shot = ctx.world.GetGlobal<ShotState>();
  if (!state || !shot)
    return;

  // 矢印非表示
  auto *arrowMR = ctx.world.Get<MeshRenderer>(m_arrowEntity);
  if (arrowMR)
    arrowMR->isVisible = false;

  // ショット位置を保存（OB時の復帰用）
  auto *ballT = ctx.world.Get<Transform>(m_ballEntity);
  if (ballT) {
    state->lastShotPosition = ballT->position;
  }

  // カメラ追従初期化
  auto *camT = ctx.world.Get<Transform>(m_cameraEntity);
  if (camT) {
    m_shotStartCamPos = camT->position;
  }
  m_isCameraChasing = false;

  // パワーとインパクトからショット実行
  float power = shot->confirmedPower * m_currentClub.maxPower;
  float impactError = shot->confirmedImpact - 0.5f; // -0.5〜+0.5

  // インパクト精度による補正
  float powerMultiplier = 1.0f;
  float curveAmount = 0.0f;

  switch (shot->judgement) {
  case ShotJudgement::Special:
    powerMultiplier = 1.0f; // 完璧 (予測線通り)
    curveAmount = 0.0f;
    break;
  case ShotJudgement::Great:
    powerMultiplier = 1.0f;           // 距離は完璧
    curveAmount = impactError * 0.1f; // わずかにブレる
    break;
  case ShotJudgement::Nice:
    powerMultiplier = 0.95f;          // 少しパワーダウン
    curveAmount = impactError * 0.3f; // 少し曲がる
    break;
  case ShotJudgement::Miss:
    powerMultiplier = 0.7f;           // 大きくパワーダウン
    curveAmount = impactError * 0.6f; // 大きく曲がる
    break;
  default:
    break;
  }

  power *= powerMultiplier;

  // 初速度（パワー）から追従開始閾値を計算して適用
  // powerは初速(m/s相当)なので、係数1.0なら「初速で1秒間進んだ距離」だけ待機する
  m_cameraChaseThreshold = power * 1.0f;
  // 最低距離保証（近すぎると酔うかもしれないため）
  if (m_cameraChaseThreshold < 5.0f) {
    m_cameraChaseThreshold = 5.0f;
  }
  LOG_INFO("WikiGolf", "Camera chase threshold set to: {:.1f}",
           m_cameraChaseThreshold);

  // 方向計算（曲がり適用）
  XMVECTOR dir = XMLoadFloat3(&m_shotDirection);
  XMVECTOR right = XMVector3Cross(XMVectorSet(0, 1, 0, 0), dir);
  dir = XMVectorAdd(dir, XMVectorScale(right, curveAmount));
  dir = XMVector3Normalize(dir);

  XMFLOAT3 shotDir;
  XMStoreFloat3(&shotDir, dir);

  // スピン設定（角速度 rad/s）
  // shotDir基準のローカル座標系
  XMVECTOR shotV = XMLoadFloat3(&shotDir);
  XMVECTOR up = XMVectorSet(0, 1, 0, 0);
  XMVECTOR rightV = XMVector3Normalize(XMVector3Cross(up, shotV));

  float backspinRate = 0.0f;
  float sidespinRate = -curveAmount * 100.0f; // カーブ量に応じてサイドスピン

  if (m_currentClub.name == "Wedge") {
    backspinRate = 60.0f; // 強力なバックスピン
  } else if (m_currentClub.name == "Iron") {
    backspinRate = 35.0f;
  } else if (m_currentClub.name == "Driver") {
    backspinRate = 12.0f; // 低スピン
  } else {
    backspinRate = 5.0f; // Putterなど
  }

  if (shot->judgement == ShotJudgement::Miss) {
    backspinRate *= 0.5f;
    float randomSpin = ((float)(rand() % 100) / 50.0f - 1.0f) * 20.0f;
    sidespinRate += randomSpin;
  }

  // 後でRigidBodyに設定するために計算しておく
  XMVECTOR initialAngularV = XMVectorScale(rightV, backspinRate);
  initialAngularV =
      XMVectorAdd(initialAngularV, XMVectorScale(up, sidespinRate));

  // ボールに速度を与える（打ち上げ角適用）
  auto *rb = ctx.world.Get<RigidBody>(m_ballEntity);
  if (rb) {
    float rad = XMConvertToRadians(m_currentClub.launchAngle);
    float vy = std::sin(rad) * power;
    float vxz = std::cos(rad) * power;

    rb->velocity.x = shotDir.x * vxz;
    rb->velocity.z = shotDir.z * vxz;
    rb->velocity.y = vy;

    // 風の影響：初期速度に風成分を加算
    float windInfluence = state->windSpeed * 0.5f; // 風の強さ係数
    rb->velocity.x += state->windDirection.x * windInfluence;
    rb->velocity.z += state->windDirection.y * windInfluence;

    // スピン適用
    XMStoreFloat3(&rb->angularVelocity, initialAngularV);

    LOG_INFO("WikiGolf", "Shot: power={:.1f}, club={}, angle={:.1f}", power,
             m_currentClub.name, m_currentClub.launchAngle);

    // === Game Juice: 超派手なインパクト演出 ===
    if (m_gameJuice) {
      auto *ballT = ctx.world.Get<Transform>(m_ballEntity);
      if (ballT) {
        float normalizedPower = shot->confirmedPower; // 0.0〜1.0

        // 判定から色タイプを決定
        game::systems::GameJuiceSystem::JudgeType judgeType =
            game::systems::GameJuiceSystem::JudgeType::None;
        switch (shot->judgement) {
        case ShotJudgement::Great:
          judgeType = game::systems::GameJuiceSystem::JudgeType::Great;
          break;
        case ShotJudgement::Special:
          judgeType = game::systems::GameJuiceSystem::JudgeType::Special;
          break;
        case ShotJudgement::Nice:
          judgeType = game::systems::GameJuiceSystem::JudgeType::Nice;
          break;
        case ShotJudgement::Miss:
          judgeType = game::systems::GameJuiceSystem::JudgeType::Miss;
          break;
        default:
          break;
        }

        // メインの大爆発（判定タイプ付き）
        m_gameJuice->TriggerImpactEffect(ctx, ballT->position, normalizedPower,
                                         judgeType);

        // パワーが高い場合は追加爆発
        if (normalizedPower > 0.5f) {
          XMFLOAT3 extraPos = ballT->position;
          extraPos.y += 0.5f;
          m_gameJuice->TriggerImpactEffect(ctx, extraPos,
                                           normalizedPower * 0.7f, judgeType);
        }

        // フルパワー時は三連爆発
        if (normalizedPower > 0.9f) {
          XMFLOAT3 extraPos2 = ballT->position;
          extraPos2.y += 1.0f;
          m_gameJuice->TriggerImpactEffect(ctx, extraPos2,
                                           normalizedPower * 0.5f, judgeType);
        }

        // Great判定時は特大演出
        if (shot->judgement == ShotJudgement::Great) {
          XMFLOAT3 greatPos = ballT->position;
          greatPos.y += 0.3f;
          m_gameJuice->TriggerImpactEffect(
              ctx, greatPos, 1.0f,
              game::systems::GameJuiceSystem::JudgeType::Great);
          m_gameJuice->TriggerCameraShake(0.6f, 0.3f); // 追加シェイク
        }
      }

      // カメラシェイク
      float shakeIntensity = 0.2f + shot->confirmedPower * 0.8f; // 強化
      float shakeDuration = 0.2f + shot->confirmedPower * 0.3f;  // 長め
      m_gameJuice->TriggerCameraShake(shakeIntensity, shakeDuration);

      // FOV: パワーに応じてズームアウト（スピード感）
      float targetFov = 65.0f + shot->confirmedPower * 15.0f;
      m_gameJuice->SetTargetFov(targetFov);

      // サウンド再生
      if (ctx.audio) {
        if (shot->confirmedPower > 0.8f) {
          ctx.audio->PlaySE(ctx, "se_shot_hard.mp3");
        } else if (shot->confirmedPower < 0.3f) {
          ctx.audio->PlaySE(ctx, "se_shot_soft.mp3");
        } else {
          ctx.audio->PlaySE(ctx, "se_shot.mp3",
                            0.5f + shot->confirmedPower * 0.5f);
        }
      }
    }
  }

  // 軌道予測ドットを非表示にする
  for (auto e : m_trajectoryDots) {
    auto *mr = ctx.world.Get<MeshRenderer>(e);
    if (mr)
      mr->isVisible = false;
  }

  // 状態更新
  state->shotCount++;
  state->canShoot = false;
  shot->phase = ShotState::Phase::Executing;

  // ショットしたので時間を進める (30分)
  m_timeOfDay.OnShot(0.5f);

  // マーカー非表示
  auto *markerUI = ctx.world.Get<UIImage>(state->gaugeMarkerEntity);
  if (markerUI)
    markerUI->visible = false;

  // UI更新
  auto *shotUI = ctx.world.Get<UIText>(state->shotCountEntity);
  if (shotUI) {
    std::wstring suffix = L" (推定)";
    if (m_calculatedPar > 0) {
      suffix = L" (残り最短 " + std::to_wstring(m_calculatedPar) + L" 記事)";
    }
    shotUI->text = L"打数: " + std::to_wstring(state->shotCount) + L" / Par " +
                   std::to_wstring(state->par) + suffix;
  }

  auto *infoUI = ctx.world.Get<UIText>(state->infoEntity);

  // 判定表示・サウンド
  auto feedback = game::utils::BuildJudgeFeedback(shot->judgement);

  if (infoUI) {
    switch (shot->judgement) {
    case ShotJudgement::Special:
      infoUI->text = L"★ SPECIAL ★";
      break;
    case ShotJudgement::Great:
      infoUI->text = L"★ GREAT ★";
      break;
    case ShotJudgement::Nice:
      infoUI->text = L"◎ NICE ◎";
      break;
    case ShotJudgement::Miss:
      infoUI->text = L"△ MISS △";
      break;
    default:
      break;
    }
  }

  auto *judgeUI = ctx.world.Get<UIImage>(state->judgeEntity);
  if (judgeUI && feedback.HasVisual()) {
    judgeUI->texturePath = feedback.texturePath;
    judgeUI->visible = true;
    judgeUI->width = feedback.width;
    judgeUI->height = feedback.height;
    m_judgeDisplayTimer = feedback.displaySeconds;
    LOG_INFO("WikiGolf", "Showing judge UI: {}", feedback.texturePath);
  } else if (judgeUI) {
    judgeUI->visible = false;
    m_judgeDisplayTimer = 0.0f;
  }

  if (ctx.audio && feedback.HasSound()) {
    ctx.audio->PlaySE(ctx, feedback.soundPath, feedback.soundVolume);
  }
}

// ==========================================
// 衝突判定ヘルパー
// ==========================================
static bool IntersectRayAABBSlab(float start, float dir, float minVal,
                                 float maxVal, float &tmin, float &tmax) {
  if (std::abs(dir) < 1e-6f) {
    return (start >= minVal && start <= maxVal);
  }
  float invDir = 1.0f / dir;
  float t1 = (minVal - start) * invDir;
  float t2 = (maxVal - start) * invDir;
  if (t1 > t2)
    std::swap(t1, t2);
  tmin = std::max(tmin, t1);
  tmax = std::min(tmax, t2);
  return tmin <= tmax;
}

static bool IntersectRayOBB(XMVECTOR rayOrigin, XMVECTOR rayDir, float maxDist,
                            XMVECTOR boxPos, XMVECTOR boxSize, XMVECTOR boxRot,
                            float &outDist) {
  // RayをBoxのローカル空間へ変換
  XMVECTOR relOrigin = XMVectorSubtract(rayOrigin, boxPos);
  XMVECTOR invRot = XMQuaternionInverse(boxRot);

  XMVECTOR localOrigin = XMVector3Rotate(relOrigin, invRot);
  XMVECTOR localDir = XMVector3Rotate(rayDir, invRot);

  float tMin = 0.0f;
  float tMax = maxDist;

  // X軸
  if (!IntersectRayAABBSlab(XMVectorGetX(localOrigin), XMVectorGetX(localDir),
                            -XMVectorGetX(boxSize), XMVectorGetX(boxSize), tMin,
                            tMax))
    return false;

  // Y軸
  if (!IntersectRayAABBSlab(XMVectorGetY(localOrigin), XMVectorGetY(localDir),
                            -XMVectorGetY(boxSize), XMVectorGetY(boxSize), tMin,
                            tMax))
    return false;

  // Z軸
  if (!IntersectRayAABBSlab(XMVectorGetZ(localOrigin), XMVectorGetZ(localDir),
                            -XMVectorGetZ(boxSize), XMVectorGetZ(boxSize), tMin,
                            tMax))
    return false;

  // ヒット判定
  if (tMin <= tMax && tMax >= 0.0f) {
    outDist = tMin;
    return true;
  }

  return false;
}

bool WikiGolfScene::CheckCameraCollision(core::GameContext &ctx,
                                         const DirectX::XMVECTOR &targetPos,
                                         const DirectX::XMVECTOR &lookAtPos,
                                         DirectX::XMVECTOR &outPos) {
  outPos = targetPos;
  bool collided = false;

  XMVECTOR rayVec = XMVectorSubtract(targetPos, lookAtPos);
  float dist = XMVectorGetX(XMVector3Length(rayVec));
  if (dist < 0.01f)
    return false;

  XMVECTOR rayDir = XMVectorScale(rayVec, 1.0f / dist);

  // 1. 壁・障害物による遮蔽（レイキャスト）
  // シーン内のStatic RigidBodyを持つBox Colliderを対象にする
  float closestHit = dist;
  bool hitWall = false;

  ctx.world.Query<Transform, RigidBody, Collider>().Each(
      [&](ecs::Entity e, Transform &t, RigidBody &rb, Collider &c) {
        if (!rb.isStatic || c.type != ColliderType::Box)
          return;

        // 自分（カメラ）やボール自体は除外（ボールはDynamicだが念のため）
        if (e == m_ballEntity || e == m_cameraEntity)
          return;

        // 床（Floor）は除外したい（床はレイキャストではなく高さチェックで対応するため）
        // 床エンティティIDと比較
        if (e == m_floorEntity)
          return;

        XMVECTOR boxPos = XMLoadFloat3(&t.position);
        XMVECTOR boxSize = XMLoadFloat3(&c.size);
        // ColliderサイズとTransformスケールを合わせる
        boxSize = XMVectorMultiply(boxSize, XMLoadFloat3(&t.scale));
        XMVECTOR boxRot = XMLoadFloat4(&t.rotation);

        float hitDist = 0.0f;
        if (IntersectRayOBB(lookAtPos, rayDir, closestHit, boxPos, boxSize,
                            boxRot, hitDist)) {
          // 0距離（内部からの開始）は無視するか、最小距離を設定
          if (hitDist > 0.1f) {
            closestHit = hitDist;
            hitWall = true;
          }
        }
      });

  if (hitWall) {
    // 壁の手前に配置（少しマージンを取る）
    float adjustedDist = std::max(0.5f, closestHit - 0.5f);
    outPos = XMVectorAdd(lookAtPos, XMVectorScale(rayDir, adjustedDist));
    collided = true;
  } else {
    outPos = targetPos;
  }

  // 2. 地形による高さ制限
  // 補正後の位置でチェック
  if (m_terrainSystem) {
    float camX = XMVectorGetX(outPos);
    float camZ = XMVectorGetZ(outPos);
    float terrainH = m_terrainSystem->GetHeight(camX, camZ);

    float currentY = XMVectorGetY(outPos);
    float minHeight = terrainH + 0.5f; // 地形 + 0.5m

    if (currentY < minHeight) {
      outPos = XMVectorSetY(outPos, minHeight);
      collided = true;
    }
  } else {
    // TerrainSystemがない場合も、最低限Y=0.5以下にはならないようにする
    if (XMVectorGetY(outPos) < 0.5f) {
      outPos = XMVectorSetY(outPos, 0.5f);
      collided = true;
    }
  }

  return collided;
}

void WikiGolfScene::UpdateCamera(core::GameContext &ctx) {
  if (!ctx.world.IsAlive(m_ballEntity) || !ctx.world.IsAlive(m_cameraEntity)) {
    return;
  }

  auto *ballT = ctx.world.Get<Transform>(m_ballEntity);
  auto *camT = ctx.world.Get<Transform>(m_cameraEntity);
  if (!ballT || !camT) {
    return;
  }

  // === ショット中の追従ロジック (ハイブリッド追従) ===
  auto *shotState = ctx.world.GetGlobal<ShotState>();
  bool isExecuting =
      (shotState && shotState->phase == ShotState::Phase::Executing);

  if (isExecuting) {
    XMVECTOR ballPos = XMLoadFloat3(&ballT->position);
    XMVECTOR startCamPos = XMLoadFloat3(&m_shotStartCamPos);

    // ボールと初期カメラ位置の距離
    float distFromStart =
        XMVectorGetX(XMVector3Length(XMVectorSubtract(ballPos, startCamPos)));
    float chaseThreshold =
        m_cameraChaseThreshold; // 初速度から計算された閾値を使用

    // フェーズ1: 固定注視モード（まだ距離が近い場合、または追尾開始前）
    if (!m_isCameraChasing) {
      if (distFromStart < chaseThreshold) {
        // カメラ位置は固定
        camT->position = m_shotStartCamPos;

        // ボールの方を向く (LookAt)
        XMVECTOR lookDir = XMVectorSubtract(ballPos, startCamPos);
        // 少し上を見るように調整（地面を見すぎない）
        lookDir = XMVectorAdd(lookDir, XMVectorSet(0, 2.0f, 0, 0));

        if (XMVectorGetX(XMVector3LengthSq(lookDir)) > 0.001f) {
          lookDir = XMVector3Normalize(lookDir);

          // LookDirからYaw/Pitchを計算
          float yaw = std::atan2(XMVectorGetX(lookDir), XMVectorGetZ(lookDir));
          float pitch = -std::asin(XMVectorGetY(
              lookDir)); // Pitchは下向きが正と仮定される場合が多いが、ここでは逆オフセットに合わせる

          // スムーズに視線を追う
          float lerp = 10.0f * ctx.dt;
          // 最短回転補正
          float diff = yaw - m_cameraYaw;
          while (diff > XM_PI)
            diff -= XM_2PI;
          while (diff < -XM_PI)
            diff += XM_2PI;
          m_cameraYaw += diff * lerp;

          m_cameraPitch += (pitch - m_cameraPitch) * lerp;
          m_cameraPitch = std::clamp(m_cameraPitch, -0.1f, 1.4f);

          XMVECTOR q = XMQuaternionRotationRollPitchYaw(m_cameraPitch,
                                                        m_cameraYaw, 0.0f);
          XMStoreFloat4(&camT->rotation, q);
        }
        return; // ここでリターン
      } else {
        // 閾値を超えたら追尾開始
        m_isCameraChasing = true;
      }
    }

    // フェーズ2: 追尾モード（以後ショット終了まで継続）
    if (m_isCameraChasing) {
      auto *ballRB = ctx.world.Get<RigidBody>(m_ballEntity);
      if (ballRB) {
        float vx = ballRB->velocity.x;
        float vz = ballRB->velocity.z;
        float speedHoriz = std::sqrt(vx * vx + vz * vz);

        if (speedHoriz > 1.0f) {
          // ボール進行方向（の逆）を目標Yawにする
          float targetYaw =
              std::atan2(vx, vz); // 一般的な定義: Z+が0, 時計回り? atan2(x, z)
          // ここでは m_cameraYaw の基準に合わせる。既存コードでは
          // offset = XMVectorSet(0, 0, -dist, 0) を rot(Yaw) するので
          // Yaw=0 なら (0,0,-dist) つまり +Z を見る。OK。

          // 目標Yaw補間
          float diff = targetYaw - m_cameraYaw;
          while (diff > XM_PI)
            diff -= XM_2PI;
          while (diff < -XM_PI)
            diff += XM_2PI;
          m_cameraYaw += diff * 2.0f * ctx.dt; // ゆっくり合わせる
        }
      }

      // Pitchは少し見下ろしに固定
      float targetPitch = 0.5f;
      m_cameraPitch += (targetPitch - m_cameraPitch) * 2.0f * ctx.dt;
    }
  }

  // === TPSオービットカメラ基準計算 ===
  // (Idle時、および追尾モード時にここを通る)

  XMVECTOR ballPos = XMLoadFloat3(&ballT->position);

  // カメラの回転クォータニオン（Yaw, Pitch）
  XMVECTOR camRotQ =
      XMQuaternionRotationRollPitchYaw(m_cameraPitch, m_cameraYaw, 0.0f);

  // 基準オフセット（後方 = -Z方向）からカメラまでの距離ベクトル
  XMVECTOR offset = XMVectorSet(0, 0, -m_cameraDistance, 0);
  offset = XMVector3Rotate(offset, camRotQ);

  // カメラ位置 = ボール位置 + オフセット
  XMVECTOR camPos = XMVectorAdd(ballPos, offset);

  // 衝突判定と補正
  XMVECTOR adjustedPos;
  bool collided = CheckCameraCollision(ctx, camPos, ballPos, adjustedPos);
  XMStoreFloat3(&camT->position, adjustedPos);

  // カメラの回転
  if (collided) {
    // 衝突があった場合は、補正後の位置からボールを見るように回転を修正
    // 注視点: ボールの少し上
    XMVECTOR focusPoint = XMVectorAdd(ballPos, XMVectorSet(0, 0.5f, 0, 0));
    XMVECTOR lookDir = XMVectorSubtract(focusPoint, adjustedPos);

    if (XMVectorGetX(XMVector3LengthSq(lookDir)) > 0.001f) {
      lookDir = XMVector3Normalize(lookDir);
      // LookAt quaternion
      XMMATRIX view =
          XMMatrixLookAtLH(adjustedPos, focusPoint, XMVectorSet(0, 1, 0, 0));
      XMVECTOR det;
      XMMATRIX camWorld = XMMatrixInverse(&det, view);
      XMStoreFloat4(&camT->rotation, XMQuaternionRotationMatrix(camWorld));
    } else {
      XMStoreFloat4(&camT->rotation, camRotQ);
    }
  } else {
    XMStoreFloat4(&camT->rotation, camRotQ);
  }

  // === ショット方向の同期 ===
  if (!isExecuting) { // ショット中は方向更新しない（プレイヤー操作用）
    XMVECTOR forward = XMVectorSet(0, 0, 1, 0);
    forward = XMVector3Rotate(forward, camRotQ);

    XMFLOAT3 fwd;
    XMStoreFloat3(&fwd, forward);
    fwd.y = 0.0f;
    XMVECTOR flatForward = XMLoadFloat3(&fwd);
    flatForward = XMVector3Normalize(flatForward);
    XMStoreFloat3(&m_shotDirection, flatForward);
  }
}

void WikiGolfScene::CreateLinksFromTexture(core::GameContext &ctx) {
  if (!m_wikiTexture)
    return;

  float width = m_fieldWidth;
  float depth = m_fieldDepth;
  float texW = (float)m_wikiTexture->width;
  float texH = (float)m_wikiTexture->height;

  auto *state = ctx.world.GetGlobal<GolfGameState>();

  for (const auto &link : m_wikiTexture->links) {
    float cx = link.x + link.width * 0.5f;
    float cy = link.y + link.height * 0.5f;

    // UV -> World
    float worldX = (cx / texW - 0.5f) * width;
    float worldZ = (0.5f - cy / texH) * depth;

    bool isTarget = (link.targetPage == state->targetPage);

    // SDOW距離計算
    int hops = -1;
    if (m_shortestPath && m_shortestPath->IsAvailable() &&
        state->targetPageId != -1) {
      auto result = m_shortestPath->FindShortestPath(link.targetPage,
                                                     state->targetPageId, 6);
      if (result.success) {
        hops = result.degrees;
      }
    }

    CreateHole(ctx, worldX, worldZ, link.targetPage, isTarget, hops);
  }
}

void WikiGolfScene::TransitionToPage(core::GameContext &ctx,
                                     std::string pageName) {
  LOG_INFO("WikiGolf", "Transitioning to page: {}", pageName);

  auto *state = ctx.world.GetGlobal<GolfGameState>();
  if (!state)
    return;

  state->moveCount++;
  state->shotCount = 0;
  state->canShoot = true;

  // ページ移動時間進行 (2時間)
  m_timeOfDay.OnPageTransition(2.0f);

  // カメラリセット（TPSオービットカメラ）
  m_cameraYaw = 0.0f;
  m_cameraPitch = 0.5f;
  m_cameraDistance = 15.0f * kFieldScale;
  m_shotDirection = {0, 0, 1};

  auto *ballT = ctx.world.Get<Transform>(m_ballEntity);
  if (ballT) {
    ballT->position = {0.0f, 1.0f, -8.0f * kFieldScale};
    auto *rb = ctx.world.Get<RigidBody>(m_ballEntity);
    if (rb)
      rb->velocity = {0, 0, 0};
  }
  // トレイルリセット（遷移時）
  if (m_gameJuice) {
    m_gameJuice->ResetTrail();
  }

  LoadPage(ctx, pageName);

  // カメラ位置を即座に更新（描画前に正しい位置へ）
  UpdateCamera(ctx);

  if (ctx.audio)
    ctx.audio->PlaySE(ctx, "se_warp.mp3");
}

void WikiGolfScene::OnUpdate(core::GameContext &ctx) {
  float baseDt = ctx.dt;
  if (m_gameJuice) {
    ctx.dt = baseDt * m_gameJuice->ConsumeTimeScale(baseDt);
  }
  float dt = ctx.dt;

  // フェード更新
  m_screenFade.Update(dt);

  // === Environment システム更新 ===
  // 1. 時間進行（日周変化）
  m_timeOfDay.Update(dt); // 連続進行がある場合

  // 2. 環境ステート更新
  auto envState = game::components::GetEnvironmentPreset(m_currentSkyboxTheme);
  m_timeOfDay.ApplyToEnvironment(
      envState); // 時間による変化（太陽位置など）を適用

  // 3. 環境音更新 (AudioSystem連携が必要)
  // m_soundSystem.Update(dt);

  // 4. ポストプロセスパラメータ更新
  m_postProcess.UpdateFromEnvironment(envState, m_timeOfDay.GetCurrentHour());
  // 定数バッファをGPUに転送
  m_postProcess.BindConstants(ctx.graphics.GetContext());

  // 5. パーティクル更新
  // カメラ情報取得
  if (ctx.world.IsAlive(m_cameraEntity)) {
    auto *camT = ctx.world.Get<Transform>(m_cameraEntity);
    auto *camC = ctx.world.Get<Camera>(m_cameraEntity);
    if (camT && camC) {
      // 簡易的なカメラ位置周辺での生成と更新
      // 風向きは一旦固定（将来的に動的化）
      DirectX::XMFLOAT3 windDir = {0.5f, 0.0f, 0.2f};
      m_particleSystem.Update(dt, camT->position, windDir);
    }
  }

  auto *state = ctx.world.GetGlobal<GolfGameState>();
  auto *shot = ctx.world.GetGlobal<ShotState>();

  // === OB（アウトオブバウンズ）処理 ===
  if (state && state->isOB) {
    LOG_INFO("WikiGolf", "OB! Penalty +1, resetting ball position");

    // 打数ペナルティ
    state->shotCount++;

    // ボールを最後のショット位置に戻す
    auto *ballT = ctx.world.Get<Transform>(m_ballEntity);
    auto *ballRB = ctx.world.Get<RigidBody>(m_ballEntity);
    if (ballT) {
      ballT->position = state->lastShotPosition;
      if (ballRB) {
        ballRB->velocity = {0.0f, 0.0f, 0.0f};
      }
    }

    // OB演出
    if (m_gameJuice) {
      m_gameJuice->TriggerCameraShake(0.4f, 0.3f);
    }
    if (ctx.audio) {
      ctx.audio->PlaySE(ctx, "se_shot_soft.mp3", 0.8f, 0.7f);
    }

    // フラグリセット
    state->isOB = false;

    // ショット状態をIdleに戻す
    if (shot) {
      shot->phase = ShotState::Phase::Idle;
    }
  }

  // === World3DLabel座標変換（ワールド→スクリーン） ===
  if (state && ctx.world.IsAlive(m_cameraEntity)) {
    auto *camT = ctx.world.Get<Transform>(m_cameraEntity);
    auto *camC = ctx.world.Get<Camera>(m_cameraEntity);
    if (camT && camC) {
      // ビュー行列
      XMVECTOR camPos = XMLoadFloat3(&camT->position);
      XMVECTOR camRotQ = XMLoadFloat4(&camT->rotation);
      XMMATRIX viewMat = XMMatrixLookToLH(
          camPos, XMVector3Rotate(XMVectorSet(0, 0, 1, 0), camRotQ),
          XMVectorSet(0, 1, 0, 0));

      // プロジェクション行列
      float aspectRatio =
          (float)ctx.graphics.GetWidth() / (float)ctx.graphics.GetHeight();
      XMMATRIX projMat = XMMatrixPerspectiveFovLH(
          XMConvertToRadians(camC->fov), aspectRatio, camC->nearZ, camC->farZ);

      float screenW = (float)ctx.graphics.GetWidth();
      float screenH = (float)ctx.graphics.GetHeight();

      // 各ホールのラベルを更新
      for (auto holeE : state->holes) {
        auto *hole = ctx.world.Get<GolfHole>(holeE);
        if (!hole || hole->labelEntity == 0)
          continue;

        auto *label = ctx.world.Get<World3DLabel>(hole->labelEntity);
        auto *uiText = ctx.world.Get<UIText>(hole->labelEntity);
        if (!label || !uiText)
          continue;

        // ワールド座標
        XMVECTOR worldPos = XMLoadFloat3(&label->worldPos);

        // ビュー空間へ変換
        XMVECTOR viewPos = XMVector3Transform(worldPos, viewMat);
        float viewZ = XMVectorGetZ(viewPos);

        // カメラの前方にあるか
        if (viewZ > 0.5f && label->visible) {
          // クリップ空間へ変換
          XMVECTOR clipPos = XMVector3Transform(viewPos, projMat);
          float w = XMVectorGetW(clipPos);
          if (w > 0.001f) {
            float ndcX = XMVectorGetX(clipPos) / w;
            float ndcY = XMVectorGetY(clipPos) / w;

            // NDC (-1~1) → スクリーン座標
            float screenX = (ndcX * 0.5f + 0.5f) * screenW;
            float screenY = (1.0f - (ndcY * 0.5f + 0.5f)) * screenH;

            // 距離に応じたスケール
            float scale = 1.0f / (viewZ * 0.05f + 1.0f);
            scale = std::clamp(scale, 0.3f, 1.5f);
            uiText->style.fontSize = 24.0f * scale;

            uiText->x = screenX - 20.0f;
            uiText->y = screenY - 15.0f;
            uiText->visible = true;
          } else {
            uiText->visible = false;
          }
        } else {
          uiText->visible = false;
        }
      }
    }
  }

  // === カメラ復帰シーケンス ===
  if (shot && shot->phase == ShotState::Phase::RestoringCamera) {
    if (m_screenFade.IsFadeOutComplete()) {
      // 暗転完了 -> カメラリセット

      if (ctx.world.IsAlive(m_ballEntity) &&
          ctx.world.IsAlive(m_cameraEntity)) {
        auto *ballT = ctx.world.Get<Transform>(m_ballEntity);
        auto *camT = ctx.world.Get<Transform>(m_cameraEntity);

        if (ballT && camT) {
          // カメラをボールの後方に再配置 (アイドル状態のデフォルト位置)
          // UpdateCamera で計算される位置に強制的に合わせるため、
          // m_cameraYaw, m_cameraPitch は維持しつつ、位置をリセットする。
          // あるいは、ショット方向(m_shotDirection)をリセットする場合はここで。

          // ここでは「構え」に戻すため、カメラ位置を計算済みの理想位置に戻す。
          // UpdateCamera(ctx) を呼べば通常ロジックで位置が決まるが、
          // 補間にしている箇所(lerp)が邪魔をする可能性があるため、
          // 強制的に初期位置へワープさせる。

          // ボール位置
          XMVECTOR ballPos = XMLoadFloat3(&ballT->position);

          // 現在の方向 (m_cameraYaw) を維持
          XMVECTOR camRotQ = XMQuaternionRotationRollPitchYaw(
              m_cameraPitch, m_cameraYaw, 0.0f);
          XMVECTOR offset = XMVectorSet(0, 0, -m_cameraDistance, 0);
          offset = XMVector3Rotate(offset, camRotQ);
          XMVECTOR camPos = XMVectorAdd(ballPos, offset);

          // 衝突回避
          XMVECTOR adjustedPos;
          CheckCameraCollision(ctx, camPos, ballPos, adjustedPos);

          XMStoreFloat3(&camT->position, adjustedPos);
          XMStoreFloat4(&camT->rotation, camRotQ);

          // 追尾フラグオフ
          m_isCameraChasing = false;

          // ショット開始時カメラ位置も更新
          XMStoreFloat3(&m_shotStartCamPos, adjustedPos);
        }
      }

      // 次のフェーズへ
      shot->phase = ShotState::Phase::Idle;
      shot->judgement = ShotJudgement::None;

      m_screenFade.FadeIn(0.4f, game::utils::FadeType::CircleWipe, {0, 0, 0});
    }
  }

  // ガイドUI更新
  UpdateGuideUI(ctx);

  // 判定UIのタイマー更新
  if (m_judgeDisplayTimer > 0.0f) {
    m_judgeDisplayTimer -= ctx.dt;
    if (m_judgeDisplayTimer <= 0.0f) {
      auto *judgeUI = ctx.world.Get<UIImage>(state->judgeEntity);
      if (judgeUI)
        judgeUI->visible = false;
    }
  }

  // 地形表示UIの更新
  if (m_terrainDisplayTimer > 0.0f) {
    m_terrainDisplayTimer -= ctx.dt;
    if (m_terrainImageEntity != UINT32_MAX) {
      auto *ui = ctx.world.Get<game::components::UIImage>(m_terrainImageEntity);
      if (ui) {
        if (m_terrainDisplayTimer <= 0.0f) {
          ui->visible = false;
        } else {
          // 演出: 最初の0.2秒でズームイン、最後0.5秒でフェードアウト
          float lifeTime = 2.0f - m_terrainDisplayTimer;
          float s = 1.0f; // Scale factor

          if (lifeTime < 0.2f) {
            float t = lifeTime / 0.2f;
            // バウンスアウト気味のイージング
            s = std::sin(t * 3.14159f * 0.5f + 0.2f) * 1.2f;
            if (t >= 1.0f)
              s = 1.0f;
            s = std::min(s, 1.2f); // Cap
          } else if (m_terrainDisplayTimer < 0.5f) {
            ui->alpha = m_terrainDisplayTimer / 0.5f;
            s = 1.0f;
          } else {
            ui->alpha = 1.0f;
            s = 1.0f;
          }

          // スケール適用 (基準サイズ: 512x256)
          float baseW = 512.0f;
          float baseH = 256.0f;
          ui->width = baseW * s;
          ui->height = baseH * s;

          // 中央寄せ (1280x720)
          ui->x = (1280.0f - ui->width) * 0.5f;
          ui->y = (720.0f - ui->height) * 0.5f;
        }
      }
    }
  }

  // マップビュー切り替え (Mキー)
  if (ctx.input.GetKeyDown('M')) {
    m_isMapView = !m_isMapView;
    LOG_INFO("WikiGolf", "Map view: {}", m_isMapView ? "ON" : "OFF");
    if (m_isMapView) {
      SyncMapCenterToBall(ctx, 0.0f, true);
    }
    m_mapZoom =
        game::utils::ClampMapZoom(m_mapZoom, m_minMapZoom, m_maxMapZoom);
  }

  if (auto *skybox = ctx.world.Get<Skybox>(m_skyboxEntity)) {
    m_mapViewSkyboxState.Sync(m_isMapView, *skybox);
  }

  // === カーソル制御とマウス入力 ===
  // Alt押下でUIモード（カーソル表示）、それ以外はエイムモード（カーソルロック）
  bool isUIMode = ctx.input.GetKey(VK_MENU); // Alt キー

  int mouseX = ctx.input.GetMousePosition().x;
  int mouseY = ctx.input.GetMousePosition().y;

  if (m_isMapView) {
    // マップビュー: カーソル表示、ドラッグでパン
    ctx.input.SetMouseCursorVisible(true);
    ctx.input.SetMouseCursorLocked(false);

    float mapExtent = std::max(m_fieldWidth, m_fieldDepth);
    float currentViewSpan = std::clamp(mapExtent / std::max(0.01f, m_mapZoom),
                                       kMinMapViewSpan, mapExtent * 6.0f);
    float zoomSpeedFactor =
        std::clamp((kMinMapViewSpan / currentViewSpan) * 3.0f, 0.5f, 3.0f);

    // === 左/右ドラッグでパン（慣性スクロール付き） ===
    static bool wasDragging = false;
    bool isDragging =
        ctx.input.GetMouseButton(0) || ctx.input.GetMouseButton(1);

    if (isDragging) {
      int deltaX = mouseX - m_prevMouseX;
      int deltaY = mouseY - m_prevMouseY;

      float sensitivity = 0.12f * zoomSpeedFactor;

      // 速度を更新
      m_mapPanVelocity.x = -deltaX * sensitivity / std::max(0.001f, ctx.dt);
      m_mapPanVelocity.y = deltaY * sensitivity / std::max(0.001f, ctx.dt);

      wasDragging = true;
    } else if (wasDragging) {
      // ドラッグ終了→慣性開始
      wasDragging = false;
    }

    // 慣性スクロール適用（常時）
    m_mapCenter.x += m_mapPanVelocity.x * ctx.dt;
    m_mapCenter.y += m_mapPanVelocity.y * ctx.dt;

    // 減衰
    float damping = 0.92f;
    m_mapPanVelocity.x *= damping;
    m_mapPanVelocity.y *= damping;

    // 閾値以下で停止
    if (std::abs(m_mapPanVelocity.x) < 0.1f)
      m_mapPanVelocity.x = 0.0f;
    if (std::abs(m_mapPanVelocity.y) < 0.1f)
      m_mapPanVelocity.y = 0.0f;

    // === ダブルクリックで中心移動 ===
    if (ctx.input.GetMouseButtonDown(0)) {
      float currentTime = ctx.time;
      float timeDiff = currentTime - m_lastMapClickTime;
      int clickDist =
          abs(mouseX - m_lastMapClickX) + abs(mouseY - m_lastMapClickY);

      if (timeDiff < 0.3f && clickDist < 10) {
        // ダブルクリック検出→その位置を中心に
        // TODO: マウス座標→ワールド座標変換の実装
        // 現在は単純にボール中心に移動
        SyncMapCenterToBall(ctx, 0.0f, true);
      }

      m_lastMapClickTime = currentTime;
      m_lastMapClickX = mouseX;
      m_lastMapClickY = mouseY;
    }

    // WASD/矢印キーでパン（速度ベース）
    float panSpeed = 20.0f * ctx.dt * zoomSpeedFactor;
    if (ctx.input.GetKey(VK_LEFT) || ctx.input.GetKey('A')) {
      m_mapCenter.x -= panSpeed;
      m_mapPanVelocity.x = 0.0f; // キー入力時は慣性リセット
    }
    if (ctx.input.GetKey(VK_RIGHT) || ctx.input.GetKey('D')) {
      m_mapCenter.x += panSpeed;
      m_mapPanVelocity.x = 0.0f;
    }
    if (ctx.input.GetKey(VK_UP) || ctx.input.GetKey('W')) {
      m_mapCenter.y -= panSpeed;
      m_mapPanVelocity.y = 0.0f;
    }
    if (ctx.input.GetKey(VK_DOWN) || ctx.input.GetKey('S')) {
      m_mapCenter.y += panSpeed;
      m_mapPanVelocity.y = 0.0f;
    }

    // === 新しいキーボードショートカット ===

    // ESCキー: マップビュー終了
    if (ctx.input.GetKeyDown(VK_ESCAPE)) {
      m_isMapView = false;
      LOG_INFO("WikiGolf", "Map view closed (ESC)");
    }

    // Spaceキー: ボール中心（Cキーと同じ）
    if (ctx.input.GetKeyDown(VK_SPACE) || ctx.input.GetKeyDown('C')) {
      SyncMapCenterToBall(ctx, 0.0f, true);
      m_targetMapZoom =
          std::clamp(m_fieldWidth / std::max(10.0f, m_fieldWidth * 0.25f),
                     m_minMapZoom, m_maxMapZoom);
    }

    // Fキー: 全体表示
    if (ctx.input.GetKeyDown('F')) {
      SyncMapCenterToBall(ctx, 0.0f, true);
      // フィールド全体が収まる最小ズーム（少し余裕を持たせて90%）
      float extent = std::max(m_fieldWidth, m_fieldDepth);
      m_targetMapZoom = extent / 220.0f * 0.9f; // ミニマップサイズ基準
      m_targetMapZoom = std::clamp(m_targetMapZoom, m_minMapZoom, m_maxMapZoom);
    }

    // 0キー: ズームリセット
    if (ctx.input.GetKeyDown('0')) {
      m_targetMapZoom = 1.0f;
    }

    // ?キー: ヘルプパネルトグル
    if (ctx.input.GetKeyDown(VK_OEM_2)) { // '/' or '?'
      m_mapHelpVisible = !m_mapHelpVisible;
    }

    // === スムーズズーム ===
    float wheel = ctx.input.GetMouseScrollDelta();
    if (wheel != 0.0f) {
      // 目標ズームを即座に変更
      m_targetMapZoom *= std::pow(1.12f, wheel);
      m_targetMapZoom = game::utils::ClampMapZoom(m_targetMapZoom, m_minMapZoom,
                                                  m_maxMapZoom);
    }
    if (ctx.input.GetKeyDown(VK_OEM_PLUS) || ctx.input.GetKeyDown(VK_ADD)) {
      m_targetMapZoom *= 1.12f;
      m_targetMapZoom = game::utils::ClampMapZoom(m_targetMapZoom, m_minMapZoom,
                                                  m_maxMapZoom);
    }
    if (ctx.input.GetKeyDown(VK_OEM_MINUS) ||
        ctx.input.GetKeyDown(VK_SUBTRACT)) {
      m_targetMapZoom /= 1.12f;
      m_targetMapZoom = game::utils::ClampMapZoom(m_targetMapZoom, m_minMapZoom,
                                                  m_maxMapZoom);
    }

    // 現在のズームを補間（0.1秒で90%到達）
    float zoomLerp = 1.0f - std::exp(-10.0f * ctx.dt);
    m_mapZoom += (m_targetMapZoom - m_mapZoom) * zoomLerp;

    // === マップ中心の境界チェック（バウンスエフェクト付き） ===
    DirectX::XMFLOAT2 beforeClamp = m_mapCenter;
    m_mapCenter = game::utils::ClampMapCenter(m_mapCenter, m_fieldWidth,
                                              m_fieldDepth, 2.0f);

    if (beforeClamp.x != m_mapCenter.x || beforeClamp.y != m_mapCenter.y) {
      // 境界到達→バウンス
      m_mapBoundaryHitTime = ctx.time;

      if (beforeClamp.x != m_mapCenter.x) {
        m_mapPanVelocity.x *= -0.3f; // 30%で反転
      }
      if (beforeClamp.y != m_mapCenter.y) {
        m_mapPanVelocity.y *= -0.3f;
      }
    }
  } else {
    // 通常ビュー: UIモード時はカーソル表示、エイムモード時はロック
    auto *shotStateCheck = ctx.world.GetGlobal<ShotState>();
    bool canAim =
        shotStateCheck && shotStateCheck->phase == ShotState::Phase::Idle;

    if (isUIMode || !canAim) {
      // UIモード または ショット不可時: カーソル表示
      ctx.input.SetMouseCursorVisible(true);
      ctx.input.SetMouseCursorLocked(false);
    } else {
      // エイムモード: カーソルロック、マウス移動でカメラ回転（TPSオービット）
      ctx.input.SetMouseCursorVisible(false);
      ctx.input.SetMouseCursorLocked(true);

      // ロック時は画面中央からの差分を取得
      // （カーソルは毎フレーム中央に戻されるため）
      int screenCenterX = 640; // TODO: 画面サイズから取得が望ましい
      int screenCenterY = 360;
      int deltaX = mouseX - screenCenterX;
      int deltaY = mouseY - screenCenterY;

      // === TPSオービットカメラ: マウス移動 → Yaw/Pitch更新 ===
      if (deltaX != 0 || deltaY != 0) {
        // 精密エイム: Shift押下で感度を1/3に
        float sensitivity = 0.005f;
        if (ctx.input.GetKey(VK_SHIFT)) {
          sensitivity *= 0.33f; // 精密モード
        }

        // 水平回転（Yaw）
        m_cameraYaw += deltaX * sensitivity;

        // 垂直回転（Pitch）
        m_cameraPitch += deltaY * sensitivity;

        // Pitch制限（上: -1.5rad, 下: 1.5rad ≒ 85度）
        m_cameraPitch = std::clamp(m_cameraPitch, -1.5f, 1.5f);
      }

      // マウスホイールでズーム
      float wheel = ctx.input.GetMouseScrollDelta();
      if (wheel != 0.0f) {
        m_cameraDistance -= wheel * 2.0f * kFieldScale; // ホイール感度
        m_cameraDistance =
            std::clamp(m_cameraDistance, 1.2f * kFieldScale,
                       35.0f * kFieldScale); // 距離制限（よりズーム可）
      }
    }
  }

  // マップセンター追従（マップビューでなければボール中心へ緩やかに寄せる）
  if (!m_isMapView) {
    SyncMapCenterToBall(ctx, ctx.dt, false);
  }

  m_prevMouseX = mouseX;
  m_prevMouseY = mouseY;

  // 物理更新
  game::systems::PhysicsSystem(ctx, ctx.dt);

  // === バリアエフェクト（壁衝突） ===
  auto *events = ctx.world.GetGlobal<game::components::CollisionEvents>();
  if (events) {
    for (const auto &ev : events->events) {
      bool isWallA = ctx.world.Has<game::components::Wall>(ev.entityA);
      bool isBallA = (ev.entityA == m_ballEntity);
      bool isWallB = ctx.world.Has<game::components::Wall>(ev.entityB);
      bool isBallB = (ev.entityB == m_ballEntity);

      if ((isWallA && isBallB) || (isWallB && isBallA)) {
        // エフェクト生成
        auto *ballT = ctx.world.Get<game::components::Transform>(m_ballEntity);
        if (ballT) {
          auto effectEntity = CreateEntity(ctx.world);
          auto &t = ctx.world.Add<game::components::Transform>(effectEntity);
          t.position = ballT->position;
          t.scale = {0.1f, 0.1f, 0.1f};

          auto &mr =
              ctx.world.Add<game::components::MeshRenderer>(effectEntity);
          mr.mesh = ctx.resource.LoadMesh("builtin/sphere");
          mr.shader =
              ctx.resource.LoadShader("Basic", L"Assets/shaders/BasicVS.hlsl",
                                      L"Assets/shaders/BasicPS.hlsl");
          mr.color = {0.0f, 1.0f, 1.0f, 0.5f}; // Cyan semi-transparent

          auto &re =
              ctx.world.Add<game::components::RippleEffect>(effectEntity);
          re.maxScale = 4.0f;
          re.duration = 0.4f;

          LOG_INFO("WikiGolf", "Wall Barrier Effect Spawned");
        }
      }
    }
  }

  // === エフェクト更新 ===
  std::vector<ecs::Entity> deadEffects;
  ctx.world
      .Query<game::components::RippleEffect, game::components::Transform,
             game::components::MeshRenderer>()
      .Each([&](ecs::Entity e, game::components::RippleEffect &re,
                game::components::Transform &t,
                game::components::MeshRenderer &mr) {
        re.timer += ctx.dt;
        float progress = re.timer / re.duration;
        if (progress >= 1.0f) {
          deadEffects.push_back(e);
        } else {
          float s = re.maxScale * std::sin(progress * 1.570796327f); // PI/2
          t.scale = {s, s, s};
          mr.color.w = 0.5f * (1.0f - progress);
        }
      });
  for (auto e : deadEffects) {
    ctx.world.DestroyEntity(e);
  }

  // ショット処理
  ProcessShot(ctx);

  // ボール速度チェック（停止したらショット可能に）
  if (ctx.world.IsAlive(m_ballEntity)) {
    auto *rb = ctx.world.Get<RigidBody>(m_ballEntity);
    auto *ballT = ctx.world.Get<Transform>(m_ballEntity);

    // 落下チェック（Y < -5 でリスポーン）
    if (ballT && ballT->position.y < -5.0f) {
      ballT->position = {0.0f, 1.0f, -8.0f * kFieldScale}; // スタート位置に戻す
      if (rb) {
        rb->velocity = {0, 0, 0};
      }
      state->canShoot = true;
      state->shotCount++; // ペナルティ

      LOG_INFO("WikiGolf", "Ball respawned (fell off)");
    }

    if (rb) {
      float speed = std::sqrt(rb->velocity.x * rb->velocity.x +
                              rb->velocity.y * rb->velocity.y +
                              rb->velocity.z * rb->velocity.z);
      if (speed < 0.1f && !state->canShoot) {
        state->canShoot = true;
        rb->velocity = {0, 0, 0};

        // 地形表示ロジック
        if (m_terrainImageEntity != UINT32_MAX) {
          auto *ui =
              ctx.world.Get<game::components::UIImage>(m_terrainImageEntity);
          if (ui) {
            std::string texPath = "";
            switch (state->currentMaterial) {
            case game::components::TerrainMaterial::Fairway:
              texPath = "ui_terrain_fairway.png";
              break;
            case game::components::TerrainMaterial::Rough:
              texPath = "ui_terrain_rough.png";
              break;
            case game::components::TerrainMaterial::Bunker:
              texPath = "ui_terrain_bunker.png";
              break;
            case game::components::TerrainMaterial::Green:
              texPath = "ui_terrain_green.png";
              break;
            default:
              break;
            }

            if (!texPath.empty()) {
              ui->texturePath = texPath;
              ui->visible = true;
              ui->alpha = 1.0f;

              // 初期状態: スケール0 (中央)
              ui->width = 0.0f;
              ui->height = 0.0f;
              ui->x = 1280.0f * 0.5f;
              ui->y = 720.0f * 0.5f;

              m_terrainDisplayTimer = 2.0f; // 2秒間表示
            }
          }
        }
      }
    }
  }

  // カメラ更新（マップビュー/通常ビュー）
  if (m_isMapView) {
    UpdateMapCamera(ctx);
  } else {
    UpdateCamera(ctx);
  }

  // ミニマップ更新
  UpdateMinimap(ctx);

  // === Game Juice 更新 ===
  if (m_gameJuice) {
    // パワーチャージ中はFOVを狭くする（緊張感）
    auto *shotCheck = ctx.world.GetGlobal<ShotState>();
    if (shotCheck && shotCheck->phase == ShotState::Phase::PowerCharging) {
      m_gameJuice->SetTargetFov(55.0f - shotCheck->powerGaugePos * 10.0f);
    } else if (shotCheck && shotCheck->phase == ShotState::Phase::Executing) {
      // ボール移動中は少し広くする（スピード感）
      m_gameJuice->SetTargetFov(65.0f);
    } else {
      m_gameJuice->ResetFov();
    }

    m_gameJuice->Update(ctx, m_cameraEntity, m_ballEntity);
  }

  // ボール位置取得（スクロールとホールイン判定で使用）
  auto *ballT = ctx.world.Get<Transform>(m_ballEntity);

  // クラブアニメーション更新
  UpdateClubAnimation(ctx, ctx.dt);

  // === 方向ガイド矢印の更新 ===
  {
    auto *shotCheck = ctx.world.GetGlobal<ShotState>();
    auto *guideT = ctx.world.Get<Transform>(m_guideArrowEntity);
    auto *guideMR = ctx.world.Get<MeshRenderer>(m_guideArrowEntity);

    if (guideT && guideMR && ballT && shotCheck) {
      // Idle時のみ表示
      if (shotCheck->phase == ShotState::Phase::Idle && state->canShoot) {
        guideMR->isVisible = true;

        // ボールの前方に配置
        float yaw = std::atan2(m_shotDirection.x, m_shotDirection.z);
        XMVECTOR q = XMQuaternionRotationRollPitchYaw(0, yaw, 0);
        XMStoreFloat4(&guideT->rotation, q);

        XMVECTOR ballPos = XMLoadFloat3(&ballT->position);
        XMVECTOR offset =
            XMVectorScale(XMLoadFloat3(&m_shotDirection), 1.0f); // 1m前方
        XMVECTOR arrowPos = XMVectorAdd(ballPos, offset);
        arrowPos = XMVectorSetY(arrowPos,
                                XMVectorGetY(ballPos) + 0.2f); // 少し浮かせる
        XMStoreFloat3(&guideT->position, arrowPos);
      } else {
        guideMR->isVisible = false;
      }
    }
  }

  // カップイン判定
  CheckCupIn(ctx);

  // フェード描画 (最前面)
}

void WikiGolfScene::Render(core::GameContext &ctx) {
  // === パーティクル描画 (加算合成なので不透明描画の後) ===
  // カメラ行列取得（View/Proj）
  if (ctx.world.IsAlive(m_cameraEntity)) {
    auto *camT = ctx.world.Get<Transform>(m_cameraEntity);
    auto *camC = ctx.world.Get<Camera>(m_cameraEntity);
    if (camT && camC) {
      XMMATRIX view = camC->GetViewMatrix(*camT);
      XMMATRIX proj = camC->GetProjectionMatrix();

      // ビルボード用カメラベクトル
      XMFLOAT3 camRight, camUp;
      XMVECTOR up = XMVectorSet(0, 1, 0, 0);
      XMVECTOR fwd =
          XMLoadFloat3(&m_shotDirection); // 簡易的にショット方向を使用
      // 本来はカメラの回転行列から取得すべき
      XMVECTOR cRight = XMVector3Cross(up, fwd); // 近似
      // より正確にTransformから取得
      XMMATRIX world = camT->GetWorldMatrix();
      XMVECTOR cRightV = world.r[0];
      XMVECTOR cUpV = world.r[1];
      XMStoreFloat3(&camRight, cRightV);
      XMStoreFloat3(&camUp, cUpV);

      m_particleRenderSystem.Render(ctx.graphics.GetContext(),
                                    m_particleSystem.GetParticles(), view, proj,
                                    camRight, camUp);
    }
  }

  // フェード描画 (最前面)
  m_screenFade.Render(ctx);
}

void WikiGolfScene::SyncMapCenterToBall(core::GameContext &ctx, float dt,
                                        bool forceSnap) {
  DirectX::XMFLOAT2 targetCenter{0.0f, 0.0f};
  if (auto *ballT = ctx.world.Get<Transform>(m_ballEntity)) {
    targetCenter = {ballT->position.x, ballT->position.z};
  }

  targetCenter = game::utils::ClampMapCenter(targetCenter, m_fieldWidth,
                                             m_fieldDepth, 2.0f);

  if (forceSnap || dt <= 0.0f) {
    m_mapCenter = targetCenter;
    return;
  }

  float lerp = 1.0f - std::exp(-m_mapFollowLerp * dt);
  m_mapCenter.x += (targetCenter.x - m_mapCenter.x) * lerp;
  m_mapCenter.y += (targetCenter.y - m_mapCenter.y) * lerp;
}

void WikiGolfScene::UpdateMapCamera(core::GameContext &ctx) {
  if (!ctx.world.IsAlive(m_cameraEntity))
    return;

  auto *camT = ctx.world.Get<Transform>(m_cameraEntity);
  if (!camT)
    return;

  // フィールド中央の真上から見下ろす
  float extent = std::max(m_fieldWidth, m_fieldDepth);
  float viewSpan = extent / std::max(0.01f, m_mapZoom);
  viewSpan = std::clamp(viewSpan, kMinMapViewSpan, extent * 6.0f);
  float height = std::max(viewSpan * 1.6f, 5.0f);

  // 目標位置: オフセット適用
  XMVECTOR targetPos = XMVectorSet(m_mapCenter.x, height, m_mapCenter.y, 0.0f);

  // 少し手前に引く (Zマイナス方向)
  targetPos = XMVectorAdd(targetPos, XMVectorSet(0, 0, -height * 0.3f, 0));

  // 現在位置から滑らかに補間
  XMVECTOR currentPos = XMLoadFloat3(&camT->position);
  XMVECTOR newPos =
      XMVectorLerp(currentPos, targetPos, 10.0f * ctx.dt); // 少し速く
  XMStoreFloat3(&camT->position, newPos);

  // 斜め下を向く（ピッチ70度）
  XMVECTOR q =
      XMQuaternionRotationRollPitchYaw(XMConvertToRadians(70.0f), 0.0f, 0.0f);
  XMStoreFloat4(&camT->rotation, q);
}
void WikiGolfScene::UpdateMinimap(core::GameContext &ctx) {
  if (!m_minimapRenderer)
    return;

  // カメラ外でも現在地がわかるよう、マーカーをUI上にプロット
  UIImage *ui = ctx.world.Get<UIImage>(m_minimapEntity);
  UIText *marker = ctx.world.Get<UIText>(m_minimapMarkerEntity);
  Transform *ballT = ctx.world.Get<Transform>(m_ballEntity);

  game::systems::MapRenderParams params;
  params.center = {m_mapCenter.x, 0.0f, m_mapCenter.y};
  params.extent = std::max(m_fieldWidth, m_fieldDepth);
  params.zoom = m_mapZoom;
  params.heightScale = m_isMapView ? 1.8f : 2.2f;
  params.orthoPadding = 1.3f;
  params.highlightBall = true;
  m_minimapRenderer->Render(ctx, params);

  if (ui && marker && ballT) {
    // MapSysの射影計算と同一の値を使ってUI座標に変換する
    float viewSpan = std::clamp(params.extent / std::max(0.01f, params.zoom),
                                params.extent * 0.01f, params.extent * 6.0f);
    float orthoWidth =
        std::max(viewSpan * params.orthoPadding, viewSpan * 0.5f);
    // GetProjMatrixが幅・高さに1.2倍を掛ける点を反映
    float clipWidth = orthoWidth * 1.2f;

    float dx = ballT->position.x - params.center.x;
    float dz = ballT->position.z - params.center.z;
    float u = dx / clipWidth; // 実験的補正: 0.5f + ... を削除
    float v = 0.5f - dz / clipWidth;

    // DEBUG: マップオフセット調査
    static int logCounter = 0;
    if (logCounter++ % 60 == 0) {
      LOG_DEBUG("WikiGolfMap",
                "Ball:({:.1f},{:.1f}) Center:({:.1f},{:.1f}) d:({:.1f},{:.1f}) "
                "ClipW:{:.1f} UV:({:.2f},{:.2f})",
                ballT->position.x, ballT->position.z, params.center.x,
                params.center.z, dx, dz, clipWidth, u, v);
    }

    // uの範囲を調整（補正により0.0中心になるため、表示範囲外に出る可能性があるがとりあえずそのまま）
    // マーカーがImage基準で 0..1
    // に収まるようにするには、Imageの座標系も考慮必要だが ここでは u を 0..1
    // にクランプする処理が下にある。 もし u がマイナスなら 0.02f
    // に張り付く。これでは左端に張り付くだけ。
    // ユーザーの言う「右にずれてる」が「中央にあるべきものが右端(1.0)にある」なら、
    // 0.5引けば中央(0.5)に来る。

    // しかし、もし u = dx/W なら、中央(dx=0)で u=0 になる。
    // この場合、marker->x = ui->x + 0 = 左端。
    // これでは「左にずれる」。
    // ユーザーは「右にずれてる」と言った。マーカーが右にある。
    // これを左（中央）に戻したい。
    // u=1.0 -> u=0.5. (-0.5).
    // u=0.5 -> u=0.0.

    // とりあえず dx/clipWidth にしてみる。

    u = std::clamp(u, 0.0f, 1.0f); // 範囲を 0..1 に変更してみる
    v = std::clamp(v, 0.02f, 0.98f);
    marker->x = ui->x + u * ui->width - 10.0f;
    marker->y = ui->y + v * ui->height - 10.0f;
    marker->visible = ui->visible;
  }

  // === Phase 2: マーカーパルスアニメーション ===
  if (marker && ui && ui->visible) {
    m_markerPulseTimer += ctx.dt;
    float pulse = 1.0f + 0.2f * std::sin(m_markerPulseTimer * 4.0f);
    marker->style.fontSize = 22.0f * pulse;

    // 色もパルス
    float alphaPulse = 0.8f + 0.2f * std::sin(m_markerPulseTimer * 4.0f);
    marker->style.color.w = alphaPulse;
  }

  // === Phase 3: 座標・距離表示（マップビュー時のみ） ===
  if (m_isMapView && ui && ballT) {
    int mouseX = ctx.input.GetMousePosition().x;
    int mouseY = ctx.input.GetMousePosition().y;

    // マウスがマップ内かチェック
    bool inMap = (mouseX >= ui->x && mouseX <= ui->x + ui->width &&
                  mouseY >= ui->y && mouseY <= ui->y + ui->height);

    auto *coordTxt = ctx.world.Get<UIText>(m_mapCoordText);
    auto *distTxt = ctx.world.Get<UIText>(m_mapDistanceText);

    if (inMap && coordTxt && distTxt) {
      float u = (mouseX - ui->x) / ui->width;
      float v = (mouseY - ui->y) / ui->height;

      // UV→ワールド座標
      float clipWidth = params.extent / std::max(0.01f, params.zoom);
      clipWidth = std::clamp(clipWidth, 5.0f, params.extent * 6.0f);

      float worldX = params.center.x + (u - 0.5f) * clipWidth;
      float worldZ = params.center.z - (v - 0.5f) * clipWidth;

      // 座標表示（ミニマップ内固定位置）
      coordTxt->x = ui->x + 10.0f;
      coordTxt->y = ui->y + ui->height - 35.0f;
      coordTxt->text = std::format(L"座標: ({:.1f}, {:.1f})", worldX, worldZ);
      coordTxt->visible = true;

      // ボールからの距離
      float dx = worldX - ballT->position.x;
      float dz = worldZ - ballT->position.z;
      float distance = std::sqrt(dx * dx + dz * dz);

      distTxt->x = ui->x + 10.0f;
      distTxt->y = ui->y + ui->height - 20.0f;
      distTxt->text = std::format(L"距離: {:.1f}m", distance);
      distTxt->visible = true;
    } else {
      if (coordTxt)
        coordTxt->visible = false;
      if (distTxt)
        distTxt->visible = false;
    }
  } else {
    // 非マップビューでは非表示
    if (auto *coordTxt = ctx.world.Get<UIText>(m_mapCoordText))
      coordTxt->visible = false;
    if (auto *distTxt = ctx.world.Get<UIText>(m_mapDistanceText))
      distTxt->visible = false;
  }

  // === Phase 2: ズームインジケーター（マップビュー時のみ） ===
  auto *zoomBg = ctx.world.Get<UIImage>(m_mapZoomIndicatorBg);
  auto *zoomTxt = ctx.world.Get<UIText>(m_mapZoomIndicatorText);

  if (m_isMapView && zoomBg && zoomTxt) {
    // ズーム率を計算（基準1.0 = 100%）
    int zoomPercent = (int)(m_mapZoom * 100.0f);
    zoomTxt->text = std::format(L"{}%", zoomPercent);

    zoomBg->visible = true;
    zoomTxt->visible = true;
  } else {
    if (zoomBg)
      zoomBg->visible = false;
    if (zoomTxt)
      zoomTxt->visible = false;
  }

  // === Phase 3: ヘルプパネルフェードイン/アウト ===
  static float helpFadeAlpha = 0.0f;
  float targetHelpAlpha = m_mapHelpVisible ? 1.0f : 0.0f;
  float fadeSpeed = 5.0f; // 0.2秒で完了
  helpFadeAlpha += (targetHelpAlpha - helpFadeAlpha) * fadeSpeed * ctx.dt;

  bool shouldShowHelp = helpFadeAlpha > 0.01f;

  auto *helpBg = ctx.world.Get<UIImage>(m_mapHelpPanelBg);
  auto *helpTitle = ctx.world.Get<UIText>(m_mapHelpTitle);

  if (helpBg) {
    helpBg->visible = shouldShowHelp;
    helpBg->alpha = helpFadeAlpha * 0.85f;
  }

  if (helpTitle) {
    helpTitle->visible = shouldShowHelp;
    helpTitle->style.color.w = helpFadeAlpha;
  }

  for (auto lineE : m_mapHelpLines) {
    if (auto *line = ctx.world.Get<UIText>(lineE)) {
      line->visible = shouldShowHelp;
      line->style.color.w = helpFadeAlpha;
    }
  }
}

void WikiGolfScene::UpdateTrajectory(core::GameContext &ctx, float powerRatio) {
  // 矢印非表示 (軌道線と重複するため)
  if (powerRatio > 0.0f) {
    auto *arrowMR = ctx.world.Get<MeshRenderer>(m_arrowEntity);
    if (arrowMR)
      arrowMR->isVisible = false;
  }

  if (m_trajectoryDots.empty())
    return;

  // ボールが動いている場合は予測線を表示しない
  auto *ballRB = ctx.world.Get<RigidBody>(m_ballEntity);
  if (ballRB) {
    float ballSpeed = std::sqrt(ballRB->velocity.x * ballRB->velocity.x +
                                ballRB->velocity.y * ballRB->velocity.y +
                                ballRB->velocity.z * ballRB->velocity.z);
    if (ballSpeed > 0.1f) {
      // ボールが動いている: 全ての予測線ドットを非表示
      for (auto e : m_trajectoryDots) {
        auto *mr = ctx.world.Get<MeshRenderer>(e);
        if (mr) {
          mr->isVisible = false;
        }
      }
      return;
    }
  }

  // 現在のクラブ設定
  float maxPower = m_currentClub.maxPower;
  float launchAngleDeg = m_currentClub.launchAngle;

  float initialSpeed = maxPower * powerRatio;

  // 発射ベクトル計算
  // m_shotDirection は水平（XZ平面）正規化ベクトル
  XMVECTOR dirXZ = XMLoadFloat3(&m_shotDirection);

  // 打ち上げ角度 (ラジアン)
  float rad = XMConvertToRadians(launchAngleDeg);

  // 垂直成分(Vy) = Speed * sin(angle)
  float vy = std::sin(rad) * initialSpeed;
  // 水平成分(Vxz) = Speed * cos(angle)
  float vxz = std::cos(rad) * initialSpeed;

  // 3D速度ベクトル
  XMVECTOR vel = XMVectorScale(dirXZ, vxz);
  vel = XMVectorSetY(vel, vy);

  // 開始位置
  auto *ballT = ctx.world.Get<Transform>(m_ballEntity);
  if (!ballT)
    return;
  XMVECTOR pos = XMLoadFloat3(&ballT->position);

  // 開始位置を地面高さに合わせて調整（位置ずれ防止）
  if (m_terrainSystem) {
    float startX = XMVectorGetX(pos);
    float startZ = XMVectorGetZ(pos);
    float startGroundY = m_terrainSystem->GetHeight(startX, startZ);
    // ボール半径分だけ地面より上に設定
    float ballRadius = 0.2f; // ゴルフボールの半径
    pos = XMVectorSetY(pos, startGroundY + ballRadius);
  }

  // RigidBody設定（ボールと同じ値を使う）
  auto *rb = ctx.world.Get<RigidBody>(m_ballEntity);
  float drag = rb ? rb->drag : 0.30f;
  float restitution = rb ? rb->restitution : 0.35f;
  float mass = rb ? rb->mass : 0.0459f;

  // 風情報取得
  auto *golfState = ctx.world.GetGlobal<GolfGameState>();

  // 地形データ取得
  auto terrainData =
      m_terrainSystem ? m_terrainSystem->GetTerrainData() : nullptr;

  // マテリアル取得ヘルパー
  auto GetMaterial = [&](float x, float z) -> TerrainMaterial {
    if (!terrainData || terrainData->materialMap.empty())
      return TerrainMaterial::Fairway; // Default
    float u = x / terrainData->config.worldWidth + 0.5f;
    float v = 0.5f - z / terrainData->config.worldDepth;
    int ix = (int)(u * (terrainData->config.resolutionX - 1));
    int iz = (int)(v * (terrainData->config.resolutionZ - 1));
    if (ix < 0 || ix >= terrainData->config.resolutionX || iz < 0 ||
        iz >= terrainData->config.resolutionZ)
      return TerrainMaterial::Fairway;
    uint8_t id =
        terrainData->materialMap[iz * terrainData->config.resolutionX + ix];
    return static_cast<TerrainMaterial>(id);
  };

  // 初期位置（ボール位置）
  XMVECTOR prevPos = pos;

  auto SafeLength = [](XMVECTOR v) {
    float lenSq = XMVectorGetX(XMVector3LengthSq(v));
    if (lenSq < 0.0f || !std::isfinite(lenSq))
      return 0.0f;
    return std::sqrt(lenSq);
  };

  const XMVECTOR gravity = XMVectorSet(0.0f, -9.8f, 0.0f, 0.0f);
  float frameDt = std::clamp(ctx.dt, 0.008f, 0.033f);
  const int physicsSubSteps = 4;
  float subDt = frameDt / static_cast<float>(physicsSubSteps);
  const int simSubStepsPerDot = 12;

  for (size_t i = 0; i < m_trajectoryDots.size(); ++i) {
    auto e = m_trajectoryDots[i];
    auto *t = ctx.world.Get<Transform>(e);
    auto *mr = ctx.world.Get<MeshRenderer>(e);

    if (!mr || !t)
      continue;

    XMVECTOR currentPos = prevPos;

    // --- 物理ステップ (PhysicsSystemのロジックを正確に模倣) ---
    for (int step = 0; step < simSubStepsPerDot; ++step) {
      XMVECTOR acc = gravity;

      // 地面との接触判定
      float groundY = 0.0f;
      XMVECTOR groundNormal = XMVectorSet(0, 1, 0, 0);
      bool isGrounded = false;

      if (m_terrainSystem) {
        float px = XMVectorGetX(currentPos);
        float pz = XMVectorGetZ(currentPos);
        groundY = m_terrainSystem->GetHeight(px, pz);

        float hX = m_terrainSystem->GetHeight(px + 0.1f, pz);
        float hZ = m_terrainSystem->GetHeight(px, pz + 0.1f);
        float gradX = (hX - groundY) / 0.1f;
        float gradZ = (hZ - groundY) / 0.1f;
        XMVECTOR slopeVec = XMVectorSet(-gradX, 1.0f, -gradZ, 0.0f);
        groundNormal = XMVector3Normalize(slopeVec);

        float ballBottom = XMVectorGetY(currentPos);
        float penetration = groundY - ballBottom;

        if (penetration > 0.0f) {
          // 着地
          currentPos = XMVectorSetY(currentPos, groundY);

          // 速度のY成分を反転・減衰 (バウンド)
          float vn = XMVectorGetX(XMVector3Dot(vel, groundNormal));
          if (vn < 0.0f) {
            vel = XMVectorSubtract(
                vel, XMVectorScale(groundNormal, vn * (1.0f + restitution)));
          }

          isGrounded = true;
        } else if (penetration > -0.1f) {
          isGrounded = true;
        }
      }

      // 接地時は法線方向の加速度を除去し、斜面方向の重力のみを残す
      if (isGrounded) {
        XMVECTOR normalComponent = XMVectorScale(
            groundNormal, XMVectorGetX(XMVector3Dot(acc, groundNormal)));
        acc = XMVectorSubtract(acc, normalComponent);
      }

      // 接地時の摩擦と斜面処理
      if (isGrounded) {
        // 法線成分を除去
        float vn = XMVectorGetX(XMVector3Dot(vel, groundNormal));
        if (vn < 0.0f) {
          vel = XMVectorSubtract(vel, XMVectorScale(groundNormal, vn));
        }

        float currentSpeed = SafeLength(vel);
        float terrainScale = terrainData ? terrainData->config.friction : 1.0f;
        TerrainMaterial mat =
            GetMaterial(XMVectorGetX(currentPos), XMVectorGetZ(currentPos));
        float ny = std::clamp(XMVectorGetY(groundNormal), 0.0f, 1.0f);
        float frictionAccel = game::systems::ComputeGrassRollingAcceleration(
            currentSpeed, ny, mat, terrainScale);

        // クラブ固有のrollingFrictionScaleを適用 (PhysicsSystemと同じ)
        if (golfState) {
          float scale = golfState->rollingFrictionScale;
          if (!std::isfinite(scale) || scale < 0.05f) {
            scale = 1.0f;
          }
          frictionAccel *= scale;
        }

        float tangentialAcc = SafeLength(acc);
        float staticLimit = frictionAccel * 1.2f;

        if (currentSpeed < 0.05f && tangentialAcc < staticLimit) {
          // ほぼ停止 & 重力に勝てる摩擦がある -> 完全停止
          vel = XMVectorZero();
          acc = XMVectorZero();
        } else if (currentSpeed > 0.0001f) {
          // 動摩擦減衰
          float drop = frictionAccel * subDt;
          if (currentSpeed <= drop) {
            vel = XMVectorZero();
          } else {
            vel = XMVectorScale(vel, (currentSpeed - drop) / currentSpeed);
          }
        }

        // 極低速時の微細振動カット
        float speedAfter = SafeLength(vel);
        float slopeFlatness = XMVectorGetY(groundNormal);
        if (speedAfter < 0.03f && slopeFlatness > 0.90f) {
          vel = XMVectorZero();
        }
      }

      // 空気抵抗 (PhysicsSystemと同じタイミングで適用)
      float speed = SafeLength(vel);
      if (speed > 0.001f) {
        float K = 0.000876f;
        float dragForce = K * drag * speed * speed;
        float dragAccMag = dragForce / std::max(mass, 0.001f);

        XMVECTOR dragDir = XMVectorScale(vel, -1.0f / speed);
        XMVECTOR dragAcc = XMVectorScale(dragDir, dragAccMag);

        acc = XMVectorAdd(acc, dragAcc);
      }

      // 風の影響
      if (golfState && golfState->windSpeed > 0.0f) {
        float windForce = golfState->windSpeed * 0.1f;
        XMVECTOR windVec = XMVectorSet(golfState->windDirection.x, 0,
                                       golfState->windDirection.y, 0);
        acc = XMVectorAdd(acc, XMVectorScale(windVec, windForce));
      }

      // オイラー積分
      vel = XMVectorAdd(vel, XMVectorScale(acc, subDt));
      currentPos = XMVectorAdd(currentPos, XMVectorScale(vel, subDt));

      // 最終停止判定（PhysicsSystemと同じ閾値）
      float speedFinal = SafeLength(vel);
      float slopeFlatnessFinal = XMVectorGetY(groundNormal);
      if (speedFinal < 0.008f && isGrounded && slopeFlatnessFinal > 0.98f) {
        vel = XMVectorZero();
      }
    }

    // === 停止判定: 速度がゼロかつ地面接地で予測終了 ===
    float finalSpeed = SafeLength(vel);

    // 地面高さを再計算
    float currentGroundY = 0.0f;
    if (m_terrainSystem) {
      float px = XMVectorGetX(currentPos);
      float pz = XMVectorGetZ(currentPos);
      currentGroundY = m_terrainSystem->GetHeight(px, pz);
    }

    bool isOnGround = XMVectorGetY(currentPos) <= currentGroundY + 0.01f;

    // PhysicsSystemと同じ閾値(0.008f)を使用
    if (finalSpeed < 0.008f && isOnGround) {
      // このドットは停止位置なので非表示化
      mr->isVisible = false;

      // 残りの未使用ドットも非表示化
      for (size_t j = i + 1; j < m_trajectoryDots.size(); ++j) {
        auto *remainMR = ctx.world.Get<MeshRenderer>(m_trajectoryDots[j]);
        if (remainMR) {
          remainMR->isVisible = false;
        }
      }

      // 予測完了: ループを抜ける
      break;
    }

    // セグメント計算
    XMVECTOR segmentVec = XMVectorSubtract(currentPos, prevPos);
    float length = XMVectorGetX(XMVector3Length(segmentVec));

    // 長さが極端に短い場合は表示しない（重なっている）
    if (length < 0.001f) {
      mr->isVisible = false;
      continue;
    }

    // 中点
    XMVECTOR midPoint = XMVectorAdd(prevPos, XMVectorScale(segmentVec, 0.5f));

    // 中点が地面より下にある場合は、地面より少し上に調整
    if (m_terrainSystem) {
      float midX = XMVectorGetX(midPoint);
      float midZ = XMVectorGetZ(midPoint);
      float midGroundY = m_terrainSystem->GetHeight(midX, midZ);

      float midY = XMVectorGetY(midPoint);
      if (midY < midGroundY + 0.1f) {
        // 地面より下または地面すれすれなら、少し上に調整
        midPoint = XMVectorSetY(midPoint, midGroundY + 0.1f);
      }
    }

    XMStoreFloat3(&t->position, midPoint);

    // 視認性調整
    float baseThickness = 0.15f;
    if (m_isMapView) {
      baseThickness *= 2.0f; // マップビュー時は太く
    }

    // スケール (Z軸方向に引き伸ばす)
    t->scale = {baseThickness, baseThickness, length};

    // 回転 (Z軸を進行方向に向ける)
    XMVECTOR dir = XMVector3Normalize(segmentVec);

    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    if (std::abs(XMVectorGetY(dir)) > 0.99f) {
      up = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
    }

    XMVECTOR zAxis = dir;
    XMVECTOR xAxis = XMVector3Normalize(XMVector3Cross(up, zAxis));
    XMVECTOR yAxis = XMVector3Cross(zAxis, xAxis);

    XMMATRIX rotMat = XMMatrixIdentity();
    rotMat.r[0] = xAxis;
    rotMat.r[1] = yAxis;
    rotMat.r[2] = zAxis;

    XMStoreFloat4(&t->rotation, XMQuaternionRotationMatrix(rotMat));

    // 地面より下なら非表示
    if (XMVectorGetY(midPoint) < 0.0f) {
      mr->isVisible = false;
    } else {
      mr->isVisible = true;
    }

    // 次のステップへ
    prevPos = currentPos;
  }
}

void WikiGolfScene::InitializeClubs(core::GameContext &ctx) {
  m_availableClubs.clear();
  m_availableClubs.push_back({"Driver", 65.0f, 12.0f, "icon_driver.png", 3.0f});
  m_availableClubs.push_back({"Iron", 48.0f, 18.0f, "icon_iron.png", 1.30f});
  m_availableClubs.push_back({"Wedge", 35.0f, 26.0f, "icon_wedge.png", 2.5f});
  m_availableClubs.push_back({"Putter", 10.0f, 0.0f, "icon_putter.png", 1.00f});

  // UI作成
  for (size_t i = 0; i < m_availableClubs.size(); ++i) {
    auto e = CreateEntity(ctx.world);

    // アイコン画像
    auto &img = ctx.world.Add<UIImage>(e);
    // texturePathを設定
    img = UIImage::Create(m_availableClubs[i].iconTexture, 0, 0);

    // 画面右側、垂直に配置（左に移動）
    float startY = 720.0f / 2.0f - (m_availableClubs.size() * 100.0f) / 2.0f;
    img.x = 20.0f; // 左側に変更
    img.y = startY + i * 100.0f;
    img.width = 80.0f;
    img.height = 80.0f;
    img.layer = 20; // 手前に表示

    // 選択状態枠（初期はDriver）
    if (i == 0) {
      img.alpha = 1.0f; // 選択中は不透明
      // ※枠線はUIImageでサポートされていないため省略（必要なら別途矩形UIを追加）
      m_currentClub = m_availableClubs[i];
    } else {
      img.alpha = 0.5f; // 未選択は半透明
    }

    m_clubUIEntities.push_back(e);
  }
  // クラブUI初期描画
  // ...
}

void WikiGolfScene::InitializeUI(core::GameContext &ctx,
                                 game::components::GolfGameState &state) {
  LOG_INFO("WikiGolf", "Initializing UI elements (Browser HUD v2)...");

  // =========================================================
  // ミニマップUI (右上 x=1040, y=20, 220x220)
  // =========================================================
  if (m_minimapRenderer) {
    m_minimapEntity = CreateEntity(ctx.world);
    auto &ui = ctx.world.Add<UIImage>(m_minimapEntity);
    ui.textureSRV = m_minimapRenderer->GetSRV();
    ui.width = 220.0f;
    ui.height = 220.0f;
    ui.x = 1040.0f;
    ui.y = 20.0f;
    ui.visible = true;
    ui.layer = 100;

    // 現在地マーカー（パルスアニメーション対応）
    m_minimapMarkerEntity = CreateEntity(ctx.world);
    auto &marker = ctx.world.Add<UIText>(m_minimapMarkerEntity);
    marker.text = L"◎";
    marker.x = ui.x + ui.width * 0.5f - 10.0f;
    marker.y = ui.y + ui.height * 0.5f - 10.0f;
    marker.style = graphics::TextStyle::Guide();
    marker.style.fontSize = 22.0f;
    marker.style.color = {1.0f, 0.9f, 0.2f, 1.0f};
    marker.layer = ui.layer + 1;
  }

  // =========================================================
  // 提案2: 風カード (右上 ミニマップ下: x=1040, y=248)
  // カード背景はUITextのbgColorで実現
  // =========================================================
  constexpr float kWindCardX = 1040.0f;
  constexpr float kWindCardY = 248.0f;
  constexpr float kWindCardW = 220.0f;

  // "WIND" ラベル（カード背景込み）
  {
    auto e = CreateEntity(ctx.world);
    auto &t = ctx.world.Add<UIText>(e);
    t.text = L"WIND";
    t.x = kWindCardX + 10.0f;
    t.y = kWindCardY + 8.0f;
    t.width = kWindCardW - 20.0f;
    t.height = 20.0f;
    t.style = graphics::TextStyle::CardLabel();
    // カード背景（このラベルで代表して背景描画）
    t.style.bgColor = {0.059f, 0.090f, 0.165f, 0.88f};
    t.style.cornerRadius = 10.0f;
    t.style.borderWidth = 1.0f;
    t.style.borderColor = {0.220f, 0.380f, 0.600f, 0.4f};
    t.visible = true;
    t.layer = 100;
    state.windCardLabelEntity = e;
  }

  // 風速数値（大きく）
  {
    auto e = CreateEntity(ctx.world);
    auto &t = ctx.world.Add<UIText>(e);
    t.text = L"-- m/s";
    t.x = kWindCardX + 10.0f;
    t.y = kWindCardY + 30.0f;
    t.width = 120.0f;
    t.height = 38.0f;
    t.style = graphics::TextStyle::CardValue();
    t.visible = true;
    t.layer = 101;
    state.windCardValueEntity = e;
  }

  // 方向矢印 + 向き（右側に配置）
  {
    auto e = CreateEntity(ctx.world);
    auto &t = ctx.world.Add<UIText>(e);
    t.text = L"→";
    t.x = kWindCardX + 130.0f;
    t.y = kWindCardY + 30.0f;
    t.width = 80.0f;
    t.height = 38.0f;
    t.style = graphics::TextStyle::CardValue();
    t.style.color = {0.220f, 0.745f, 0.973f, 1.0f}; // 風矢印はスカイブルー
    t.style.fontSize = 32.0f;
    t.style.align = graphics::TextAlign::Center;
    t.visible = true;
    t.layer = 101;
    state.windCardUnitEntity = e;
  }

  // 既存互換エンティティ（windEntityはLoadPage側で使う参照が残るため空で保持）
  {
    auto windE = CreateEntity(ctx.world);
    auto &wt = ctx.world.Add<UIText>(windE);
    wt.visible = false; // 新UIに移行したため非表示
    state.windEntity = windE;
  }

  // 既存互換: 風矢印画像（LoadPage側でrotation設定される参照が残るため保持）
  {
    auto windArrowE = CreateEntity(ctx.world);
    auto &wa = ctx.world.Add<UIImage>(windArrowE);
    wa = UIImage::Create("", 0.0f, 0.0f);
    wa.width = 0.0f;
    wa.height = 0.0f;
    wa.visible = false; // 新UIでは不使用
    state.windArrowEntity = windArrowE;
  }

  // =========================================================
  // 提案1: ブラウザ風HUD (左上)
  // 構成:
  //  🌐 [現在ページ]  →  [ターゲットページ(金)]   (URLバー風)
  //  打数: X / Par Y (残り最短 N 記事)            (副情報)
  //  History: A > B > ...                         (パンくずリスト)
  // =========================================================
  constexpr float kHudX = 14.0f;
  constexpr float kHudY = 14.0f;

  // タブアイコン
  {
    auto e = CreateEntity(ctx.world);
    auto &t = ctx.world.Add<UIText>(e);
    t.text = L"\U0001f310"; // 🌐
    t.x = kHudX;
    t.y = kHudY + 2.0f;
    t.width = 32.0f;
    t.height = 30.0f;
    t.style = graphics::TextStyle::Guide();
    t.style.fontSize = 20.0f;
    t.style.color = {0.220f, 0.745f, 0.973f, 1.0f};
    t.style.align = graphics::TextAlign::Center;
    // タブアイコンの背景 = URLバー全体の背景をここで描画
    t.style.bgColor = {0.059f, 0.090f, 0.165f, 0.88f};
    t.style.cornerRadius = 10.0f;
    t.style.borderWidth = 1.0f;
    t.style.borderColor = {0.220f, 0.380f, 0.600f, 0.4f};
    t.visible = true;
    t.layer = 10;
    state.browserTabIconEntity = e;
  }

  // 現在ページ名（URLバー風 白テキスト）
  {
    auto e = CreateEntity(ctx.world);
    auto &t = ctx.world.Add<UIText>(e);
    t.text = L"Loading...";
    t.x = kHudX + 36.0f;
    t.y = kHudY + 2.0f;
    t.width = 500.0f;
    t.height = 30.0f;
    t.style = graphics::TextStyle::BrowserURL();
    t.style.bgColor = {0.0f, 0.0f, 0.0f, 0.0f}; // 背景はタブアイコン側で描画
    t.style.borderWidth = 0.0f;
    t.visible = true;
    t.layer = 11;
    state.browserCurrentPageEntity = e;
    // 後方互換: headerEntityも同一エンティティ
    state.headerEntity = e;
  }

  // 矢印セパレーター + ターゲットページ名（金色）
  {
    auto e = CreateEntity(ctx.world);
    auto &t = ctx.world.Add<UIText>(e);
    t.text = L"→ 目標ページ...";
    t.x = kHudX + 36.0f;
    t.y = kHudY + 32.0f;
    t.width = 600.0f;
    t.height = 28.0f;
    t.style = graphics::TextStyle::GoalHighlight();
    t.style.fontSize = 18.0f;
    t.visible = true;
    t.layer = 11;
    state.browserTargetEntity = e;
  }

  // 打数/Par + 最短距離（副情報テキスト）
  {
    auto e = CreateEntity(ctx.world);
    auto &t = ctx.world.Add<UIText>(e);
    t.text = L"打数: 0 / Par ?";
    t.x = kHudX + 36.0f;
    t.y = kHudY + 58.0f;
    t.width = 600.0f;
    t.height = 24.0f;
    t.style = graphics::TextStyle::BrowserSub();
    t.visible = true;
    t.layer = 11;
    state.browserShotInfoEntity = e;
    // 後方互換: shotCountEntityも同一エンティティ
    state.shotCountEntity = e;
  }

  // 経路ブレッドクラム（履歴）
  {
    auto e = CreateEntity(ctx.world);
    auto &t = ctx.world.Add<UIText>(e);
    t.text = L"";
    t.x = kHudX + 36.0f;
    t.y = kHudY + 80.0f;
    t.width = 700.0f;
    t.height = 22.0f;
    t.style = graphics::TextStyle::BrowserSub();
    t.style.fontSize = 14.0f;
    t.style.color = {0.569f, 0.639f, 0.729f, 0.9f}; // より薄い
    t.visible = true;
    t.layer = 11;
    state.browserHistoryEntity = e;
    // 後方互換: pathEntityも同一エンティティ
    state.pathEntity = e;
  }

  // 後方互換: infoEntityは空エンティティ（古いコードの参照が残るため）
  {
    auto e = CreateEntity(ctx.world);
    auto &t = ctx.world.Add<UIText>(e);
    t.visible = false;
    state.infoEntity = e;
  }

  // =========================================================
  // 提案5: ショットパネル (画面下中央)
  // 構成: POWER [████░░░] XX% | ACCURACY [●] | CLUB: Driver
  // 位置: x=300, y=620, 幅680
  // =========================================================
  constexpr float kPanelX = 300.0f;
  constexpr float kPanelY = 622.0f;
  constexpr float kPanelW = 680.0f;

  // POWER ラベル
  {
    auto e = CreateEntity(ctx.world);
    auto &t = ctx.world.Add<UIText>(e);
    t.text = L"POWER";
    t.x = kPanelX;
    t.y = kPanelY;
    t.width = 100.0f;
    t.height = 22.0f;
    t.style = graphics::TextStyle::ShotPanelLabel();
    // パネル背景をここで描画
    t.style.bgColor = {0.059f, 0.090f, 0.165f, 0.82f};
    t.style.cornerRadius = 12.0f;
    t.style.borderWidth = 1.0f;
    t.style.borderColor = {0.220f, 0.380f, 0.600f, 0.4f};
    t.visible = false; // ショット時のみ表示
    t.layer = 50;
    state.shotPanelPowerLabelEntity = e;
  }

  // POWER値（パーセント表示）
  {
    auto e = CreateEntity(ctx.world);
    auto &t = ctx.world.Add<UIText>(e);
    t.text = L"0%";
    t.x = kPanelX + kPanelW - 70.0f;
    t.y = kPanelY;
    t.width = 65.0f;
    t.height = 22.0f;
    t.style = graphics::TextStyle::ShotPanelValue();
    t.visible = false;
    t.layer = 51;
    state.shotPanelPowerValueEntity = e;
  }

  // ACCURACY ラベル
  {
    auto e = CreateEntity(ctx.world);
    auto &t = ctx.world.Add<UIText>(e);
    t.text = L"ACCURACY";
    t.x = kPanelX;
    t.y = kPanelY + 50.0f;
    t.width = 120.0f;
    t.height = 22.0f;
    t.style = graphics::TextStyle::ShotPanelLabel();
    t.visible = false;
    t.layer = 50;
    state.shotPanelAccuracyLabelEntity = e;
  }

  // ACCURACY値（評価テキスト）
  {
    auto e = CreateEntity(ctx.world);
    auto &t = ctx.world.Add<UIText>(e);
    t.text = L"---";
    t.x = kPanelX + kPanelW - 160.0f;
    t.y = kPanelY + 50.0f;
    t.width = 155.0f;
    t.height = 22.0f;
    t.style = graphics::TextStyle::ShotPanelValue();
    t.visible = false;
    t.layer = 51;
    state.shotPanelAccuracyValueEntity = e;
  }

  // CLUB表示（クラブ名）
  {
    auto e = CreateEntity(ctx.world);
    auto &t = ctx.world.Add<UIText>(e);
    t.text = L"CLUB: Driver";
    t.x = kPanelX;
    t.y = kPanelY + 95.0f;
    t.width = kPanelW;
    t.height = 22.0f;
    t.style = graphics::TextStyle::ClubName();
    t.style.align = graphics::TextAlign::Center;
    t.style.fontSize = 14.0f;
    t.style.color = {0.569f, 0.639f, 0.729f, 0.9f};
    t.visible = false;
    t.layer = 50;
    state.shotPanelClubLabelEntity = e;
  }

  // --- パワーゲージ (D2D UIBarGauge) - 提案5準拠の大型中央配置 ---
  state.gaugeBarEntity = CreateEntity(ctx.world);
  auto &gauge = ctx.world.Add<UIBarGauge>(state.gaugeBarEntity);
  gauge.x = kPanelX;
  gauge.y = kPanelY + 22.0f;
  gauge.width = kPanelW;
  gauge.height = 24.0f;

  gauge.value = 0.0f;
  gauge.maxValue = 1.0f;
  gauge.color = {0.133f, 0.773f, 0.369f, 1.0f}; // #22C55E 成功色デフォルト
  gauge.bgColor = {0.020f, 0.039f, 0.090f, 0.9f}; // 濃紺背景
  gauge.borderColor = {0.220f, 0.380f, 0.600f, 0.6f};
  gauge.borderWidth = 1.5f;
  gauge.isVisible = false;

  gauge.showImpactZones = false;
  gauge.impactCenter = 0.5f;
  gauge.impactWidthGreat = 0.10f; // Great幅を少し広く
  gauge.impactWidthNice = 0.28f;  // Nice幅
  gauge.showMarker = false;

  // ACCURACY ゲージ行（インパクトゲージ）
  // 既存gaugeBarEntityをフェーズで切り替えして使用するため単一ゲージを維持

  state.gaugeFillEntity = 0;
  state.gaugeMarkerEntity = 0;

  // =========================================================
  // 判定結果 (中央に大きく)
  // =========================================================
  auto judgeE = CreateEntity(ctx.world);
  auto &ji = ctx.world.Add<UIImage>(judgeE);
  ji = UIImage::Create("ui_judge_great.png", 540.0f, 280.0f);
  ji.width = 200.0f;
  ji.height = 80.0f;
  ji.visible = false;
  state.judgeEntity = judgeE;

  // =========================================================
  // ガイドUI (下部)
  // =========================================================
  auto guideE = CreateEntity(ctx.world);
  auto &gt = ctx.world.Add<UIText>(guideE);
  gt.text = L"";
  gt.x = 200.0f;
  gt.y = 688.0f;
  gt.width = 880.0f;
  gt.style = graphics::TextStyle::Guide();
  gt.style.fontSize = 20.0f;
  gt.style.color = {0.792f, 0.835f, 0.886f, 0.9f};
  gt.visible = true;
  gt.layer = 100;
  state.guideEntity = guideE;

  // 後方互換: guideBgEntityは空
  {
    auto e = CreateEntity(ctx.world);
    auto &im = ctx.world.Add<UIImage>(e);
    im.visible = false;
    state.guideBgEntity = e;
  }

  // =========================================================
  // マップビュー強化UI
  // =========================================================

  // ズームインジケーター背景
  m_mapZoomIndicatorBg = CreateEntity(ctx.world);
  auto &zoomBg = ctx.world.Add<UIImage>(m_mapZoomIndicatorBg);
  zoomBg = UIImage::Create("", 1040.0f, 680.0f);
  zoomBg.width = 100.0f;
  zoomBg.height = 20.0f;
  zoomBg.alpha = 0.7f;
  zoomBg.visible = false;
  zoomBg.layer = 105;

  m_mapZoomIndicatorText = CreateEntity(ctx.world);
  auto &zoomTxt = ctx.world.Add<UIText>(m_mapZoomIndicatorText);
  zoomTxt.text = L"100%";
  zoomTxt.x = 1065.0f;
  zoomTxt.y = 683.0f;
  zoomTxt.style = graphics::TextStyle::BrowserSub();
  zoomTxt.style.fontSize = 14.0f;
  zoomTxt.visible = false;
  zoomTxt.layer = 106;

  m_mapCoordText = CreateEntity(ctx.world);
  auto &coordTxt = ctx.world.Add<UIText>(m_mapCoordText);
  coordTxt.text = L"";
  coordTxt.x = 1050.0f;
  coordTxt.y = 205.0f;
  coordTxt.style = graphics::TextStyle::BrowserSub();
  coordTxt.style.fontSize = 13.0f;
  coordTxt.visible = false;
  coordTxt.layer = 102;

  m_mapDistanceText = CreateEntity(ctx.world);
  auto &distTxt = ctx.world.Add<UIText>(m_mapDistanceText);
  distTxt.text = L"";
  distTxt.x = 1050.0f;
  distTxt.y = 220.0f;
  distTxt.style = graphics::TextStyle::BrowserSub();
  distTxt.style.fontSize = 13.0f;
  distTxt.style.color = {0.133f, 0.773f, 0.369f, 0.9f}; // 成功色
  distTxt.visible = false;
  distTxt.layer = 102;

  // ヘルプパネル
  m_mapHelpPanelBg = CreateEntity(ctx.world);
  auto &helpBg = ctx.world.Add<UIImage>(m_mapHelpPanelBg);
  helpBg = UIImage::Create("", 870.0f, 240.0f);
  helpBg.width = 360.0f;
  helpBg.height = 340.0f;
  helpBg.alpha = 0.85f;
  helpBg.visible = false;
  helpBg.layer = 200;

  m_mapHelpTitle = CreateEntity(ctx.world);
  auto &helpTitle = ctx.world.Add<UIText>(m_mapHelpTitle);
  helpTitle.text = L"マップ操作";
  helpTitle.x = 890.0f;
  helpTitle.y = 260.0f;
  helpTitle.style = graphics::TextStyle::CardValue();
  helpTitle.style.fontSize = 28.0f;
  helpTitle.visible = false;
  helpTitle.layer = 201;

  const wchar_t *helpTexts[] = {L"左/右ドラッグ : マップ移動",
                                L"ホイール / +/- : ズーム",
                                L"WASD / 矢印 : マップ移動",
                                L"Space / C : ボール中心",
                                L"F : 全体表示",
                                L"0 : ズームリセット",
                                L"ESC / M : マップ終了",
                                L"? : ヘルプ表示/非表示"};

  float helpStartY = 300.0f;
  for (int i = 0; i < 8; ++i) {
    auto helpLineE = CreateEntity(ctx.world);
    auto &helpLine = ctx.world.Add<UIText>(helpLineE);
    helpLine.text = helpTexts[i];
    helpLine.x = 890.0f;
    helpLine.y = helpStartY + i * 30.0f;
    helpLine.style = graphics::TextStyle::BrowserSub();
    helpLine.style.fontSize = 16.0f;
    helpLine.visible = false;
    helpLine.layer = 201;
    m_mapHelpLines.push_back(helpLineE);
  }

  // =========================================================
  // 地形表示UI
  // =========================================================
  if (m_terrainImageEntity == UINT32_MAX) {
    auto e = CreateEntity(ctx.world);
    auto &ui = ctx.world.Add<game::components::UIImage>(e);
    ui.texturePath = "";
    ui.x = (1280.0f - 512.0f) * 0.5f;
    ui.y = (720.0f - 256.0f) * 0.5f;
    ui.width = 512.0f;
    ui.height = 256.0f;
    ui.visible = false;
    ui.layer = 20;
    m_terrainImageEntity = e;
  }
}




void WikiGolfScene::UpdateGuideUI(core::GameContext &ctx) {
  auto *state = ctx.world.GetGlobal<game::components::GolfGameState>();
  auto *shot = ctx.world.GetGlobal<game::components::ShotState>();
  if (!state || !shot)
    return;

  auto *guideUI = ctx.world.Get<UIText>(state->guideEntity);
  auto *guideBg = ctx.world.Get<UIImage>(state->guideBgEntity);

  if (!guideUI)
    return;

  std::wstring text = L"";

  if (state->gameCleared) {
    text = L"[クリック] 次へ";
  } else if (m_isMapView) {
    text = L"[左/右ドラッグ] 移動  [WASD] パン  [Wheel/+/-] ズーム  [Space/C] "
           L"中心  [F] 全体  [?] ヘルプ  [ESC/M] 戻る";
  } else {
    // カーソルモード判定
    bool isUIMode = ctx.input.GetKey(VK_MENU);
    bool isPrecision = ctx.input.GetKey(VK_SHIFT);

    switch (shot->phase) {
    case game::components::ShotState::Phase::Idle:
      if (isUIMode) {
        text = L"[UIモード] クラブ選択可  [Alt] 解除";
      } else if (isPrecision) {
        text = L"[精密エイム] マウスでゆっくり狙う";
      } else {
        text = L"[左クリック] ショット  [マウス] エイム  [Shift] 精密  [Q/E] "
               L"クラブ  [M] マップ  [Alt] UI";
      }
      break;
    case game::components::ShotState::Phase::PowerCharging:
      text = L"[クリック] パワー決定  [右] キャンセル";
      break;
    case game::components::ShotState::Phase::ImpactTiming:
      text = L"[クリック] インパクト！";
      break;
    case game::components::ShotState::Phase::Executing:
      text = L"ショット中...";
      break;
    case game::components::ShotState::Phase::ShowResult:
      text = L"判定中...";
      break;
    }
  }

  guideUI->text = text;

  // 背景も同期して表示
  if (guideBg) {
    guideBg->visible = guideUI->visible;
    // テキストが空なら背景も消す？（ただしtext=""でもvisible=trueなら枠が出るかも）
    // 現状textは常に何かしらセットされるのでOK
  }

  // 色の動的変更（エラー時は赤くするなど）
  if (text.find(L"OB") != std::wstring::npos) {
    guideUI->style.color = {1.0f, 0.2f, 0.2f, 1.0f}; // 赤
  } else {
    guideUI->style.color = {1.0f, 1.0f, 1.0f, 1.0f}; // 白
  }
}

void WikiGolfScene::LoadPage(core::GameContext &ctx,
                             const std::string &pageName) {
  auto *state = ctx.world.GetGlobal<game::components::GolfGameState>();
  if (!state) {
    LOG_ERROR("WikiGolf", "LoadPage: GameState not found!");
    return;
  }

  LOG_INFO("WikiGolf", "Loading page: {}", pageName);

  // 1. 古いホールを削除
  // Queryを使って削除リストを作成（イテレーション中の削除は危険なため）
  std::vector<ecs::Entity> holesToDelete;
  std::vector<ecs::Entity> relatedToDelete; // ラベル・光柱等
  ctx.world.Query<game::components::GolfHole>().Each(
      [&](ecs::Entity e, game::components::GolfHole &hole) {
        holesToDelete.push_back(e);
        // ラベルエンティティも削除対象に
        if (hole.labelEntity != 0) {
          relatedToDelete.push_back(ecs::Entity(hole.labelEntity));
        }
        // 光柱エンティティも削除対象に
        if (hole.pillarEntity != 0) {
          relatedToDelete.push_back(ecs::Entity(hole.pillarEntity));
        }
      });
  for (auto e : relatedToDelete) {
    if (ctx.world.IsAlive(e)) {
      ctx.world.DestroyEntity(e);
    }
  }
  for (auto e : holesToDelete) {
    ctx.world.DestroyEntity(e);
  }
  std::vector<ecs::Entity> flagsToDelete;
  ctx.world.Query<game::components::HoleFlag>().Each(
      [&](ecs::Entity e, game::components::HoleFlag &) {
        flagsToDelete.push_back(e);
      });
  for (auto e : flagsToDelete) {
    ctx.world.DestroyEntity(e);
  }
  state->holes.clear();
  LOG_DEBUG("WikiGolf", "LoadPage (after delete holes): Cam Alive={}",
            ctx.world.IsAlive(m_cameraEntity) ? "true" : "false");

  // 2. 記事データ取得
  game::systems::WikiClient wikiClient;
  std::vector<game::WikiLink> allLinks;
  std::string articleText;

  // キャッシュは初回かつページ名が一致する場合のみ使用可能にする（簡易チェック）
  // ただし初回LoadPage以外でm_hasPreloadedDataがtrueになることはほぼない
  if (m_hasPreloadedData) {
    LOG_INFO("WikiGolf", "Using preloaded links and text for {}", pageName);
    allLinks = std::move(m_preloadedLinks);
    articleText = std::move(m_preloadedExtract);
    m_hasPreloadedData = false; // 使い終わったらフラグを下ろす
  } else {
    LOG_INFO("WikiGolf", "Fetching live data for {}", pageName);
    // リンク取得（多めに取得してフィルタリング）
    allLinks = wikiClient.FetchPageLinks(pageName, 0);
    // 記事テキスト取得
    articleText = wikiClient.FetchPageExtract(pageName, 5000);
  }

  // 3. リンクのフィルタリング
  std::vector<std::pair<std::string, std::wstring>> validLinks;

  // フィルタリング（年・月・日・数値のみを除外）
  auto isIgnored = [](const std::string &t) {
    if (t.empty())
      return true;
    // 末尾チェック (UTF-8)
    if (t.size() >= 3) {
      std::string suffix = t.substr(t.size() - 3);
      if (suffix == "年" || suffix == "月" || suffix == "日")
        return true;
    }
    // 数値のみ
    if (std::all_of(t.begin(), t.end(),
                    [](unsigned char c) { return std::isdigit(c); }))
      return true;
    return false;
  };

  // リンク数の動的上限を計算（本文2000文字につき5リンク、固定上限50）
  const size_t kLinksPerChars = 5;
  const size_t kCharsPerUnit = 2000;
  const size_t kMaxLinks = 50;
  size_t dynamicLimit =
      (articleText.length() / kCharsPerUnit + 1) * kLinksPerChars;
  size_t linkLimit = (dynamicLimit < kMaxLinks) ? dynamicLimit : kMaxLinks;

  for (const auto &link : allLinks) {
    if (isIgnored(link.title))
      continue;

    // ターゲットページは別枠で処理するのでスキップ
    if (link.title == state->targetPage)
      continue;

    // 本文に含まれているかチェック
    if (articleText.find(link.title) != std::string::npos) {
      validLinks.push_back({link.title, core::ToWString(link.title)});
    }

    if (validLinks.size() >= linkLimit)
      break;
  }

  // ターゲットページは本文に含まれていれば無条件追加
  if (!state->targetPage.empty() &&
      articleText.find(state->targetPage) != std::string::npos) {
    bool targetExists = false;
    for (const auto &v : validLinks) {
      if (v.first == state->targetPage) {
        targetExists = true;
        break;
      }
    }
    if (!targetExists) {
      validLinks.push_back(
          {state->targetPage, core::ToWString(state->targetPage)});
      LOG_INFO("WikiGolf", "Target page '{}' added (in article)",
               state->targetPage);
    }
  }

  // リンク不足時の補充
  if (validLinks.size() < 3) {
    for (const auto &link : allLinks) {
      bool exists = false;
      for (const auto &v : validLinks)
        if (v.first == link.title)
          exists = true;
      if (!exists && !isIgnored(link.title)) {
        validLinks.push_back({link.title, core::ToWString(link.title)});
        if (validLinks.size() >= 5)
          break;
      }
    }
  }

  // ラムダ式内で使うisIgnoredをここでも定義する必要があったので、
  // 上記の補充ループ内のisIgnoredはコンパイルエラーになる可能性がある。
  // まじめに実装しなおす。

  // 4. フィールドサイズ計算 (フレキシブル化)
  const float minFieldWidth = 20.0f * kFieldScale;
  const float minFieldDepth = 30.0f * kFieldScale;

  // 記事の長さを基準にする (1500文字を1ユニット程度と想定)
  float articleLengthFactor = (float)articleText.length() / 1500.0f;
  if (articleLengthFactor < 1.0f)
    articleLengthFactor = 1.0f;

  // 横幅も適度にスケールさせつつ、縦長になりすぎないよう抑制する
  float fieldWidth = minFieldWidth * std::pow(articleLengthFactor, 0.45f);
  // 高さ（奥行き）は後で逆算するため、ここでは最小値を設定
  float fieldDepth = minFieldDepth;

  // 安全策: 最小サイズ保証 + 幅の上限
  fieldWidth = std::clamp(fieldWidth, minFieldWidth, minFieldWidth * 4.0f);
  fieldDepth = std::max(fieldDepth, minFieldDepth);

  // 5. テクスチャ生成
  // 幅は16384制限があるが、高さはタイリングで無限に対応可能なので制限しない
  const uint32_t kMaxTexWidth = 16384;
  float texScale = 1.0f;

  uint32_t texWidth = static_cast<uint32_t>(fieldWidth * 100.0f);
  uint32_t texHeight = static_cast<uint32_t>(fieldDepth * 100.0f);

  if (texWidth > kMaxTexWidth) {
    texScale = (float)kMaxTexWidth / (float)texWidth;
    texWidth = kMaxTexWidth;
    texHeight =
        (uint32_t)(texHeight *
                   texScale); // アスペクト比維持で高さ解像度も一応下げる

    LOG_INFO("WikiGolf", "Width capped to {}. Scale: {:.2f}", kMaxTexWidth,
             texScale);
  }

  std::vector<std::pair<std::wstring, std::string>> linkPairs;
  for (const auto &link : validLinks) {
    linkPairs.push_back({link.second, link.first});
  }

  // GenerateTexture呼び出し
  auto texResult = m_textureGenerator->GenerateTexture(
      core::ToWString(pageName), core::ToWString(articleText), linkPairs,
      state->targetPage, texWidth, texHeight);

  // 実態に合わせてフィールドサイズを計算（1m = 100px基準）
  float actualFieldDepth = (float)texResult.height / (100.0f * texScale);
  float actualFieldWidth = (float)texResult.width / (100.0f * texScale);

  // アスペクト比を維持しつつ、最小サイズ(minFieldWidth/Depth)を満たすようにスケーリング
  // 独立して max() をかけるとアスペクト比が崩れて文字が歪むため
  float scaleFix = 1.0f;
  if (actualFieldWidth < minFieldWidth) {
    scaleFix = std::max(scaleFix, minFieldWidth / actualFieldWidth);
  }
  if (actualFieldDepth < minFieldDepth) {
    scaleFix = std::max(scaleFix, minFieldDepth / actualFieldDepth);
  }

  fieldWidth = actualFieldWidth * scaleFix;
  fieldDepth = actualFieldDepth * scaleFix;

  m_wikiTexture =
      std::make_unique<graphics::WikiTextureResult>(std::move(texResult));

  // スカイボックスをページテーマに応じて動的ロード
  auto *skyboxComp = ctx.world.Get<components::Skybox>(m_skyboxEntity);
  if (skyboxComp && m_skyboxGenerator) {
    graphics::SkyboxTheme theme =
        m_skyboxGenerator->DetermineTheme(pageName, articleText);
    std::wstring themeName =
        graphics::SkyboxTextureGenerator::GetThemeFileName(theme);
    std::wstring skyboxBasePath =
        L"Assets/textures/runtime_skybox/skybox_" + themeName;

    if (m_skyboxGenerator->LoadCubemapFromFiles(
            ctx.graphics.GetDevice(), skyboxBasePath, skyboxComp->cubemapSRV)) {
      LOG_INFO("WikiGolf", "Skybox loaded for theme: {}, SRV valid: {}",
               core::ToString(themeName),
               skyboxComp->cubemapSRV ? "yes" : "no");
      skyboxComp->isVisible = true;

      // === 環境設定の適用 ===
      m_currentSkyboxTheme = theme;
      auto preset = game::components::GetEnvironmentPreset(theme);

      // パーティクル設定
      auto particleConfig =
          game::systems::GetParticleConfig(preset.particlePreset);
      m_particleSystem.Configure(particleConfig);

      // 環境状態を反映（TimeOfDayシステムなどへ）
      m_timeOfDay.SetTime(preset.timeOfDay); // テーマに応じた初期時間

      // 環境音切り替え（AudioSystemが必要）
      // TODO: AudioSystem連携

      // ポストプロセス初期設定（霧など）
      m_postProcess.SetFog(preset.fogColor, preset.fogDensity, preset.fogStart,
                           preset.fogEnd);
      m_postProcess.SetColorGrading(preset.colorTint, preset.brightness,
                                    preset.saturation, preset.contrast);

    } else {
      // Fallback to Default
      std::wstring defaultPath =
          L"Assets/textures/runtime_skybox/skybox_Default";
      if (m_skyboxGenerator->LoadCubemapFromFiles(
              ctx.graphics.GetDevice(), defaultPath, skyboxComp->cubemapSRV)) {
        LOG_INFO("WikiGolf", "Skybox fallback to Default");
        skyboxComp->isVisible = true;
      } else {
        LOG_WARN("WikiGolf", "Failed to load any skybox");
        skyboxComp->isVisible = false;
      }
    }
  }

  // 異常な巨大値を防止しつつ、超長文でも収まるよう高めの上限を設定
  const float kMaxSafeDepth = 20000.0f; // 20km相当
  const float kMaxSafeWidth = 20000.0f;

  fieldDepth = std::min(fieldDepth, kMaxSafeDepth);
  fieldWidth = std::min(fieldWidth, kMaxSafeWidth);

  LOG_INFO("WikiGolf", "Final field size: {}x{}", fieldWidth, fieldDepth);

  // 再計上したフィールド寸法を保存
  m_fieldWidth = fieldWidth;
  m_fieldDepth = fieldDepth;
  state->fieldWidth = fieldWidth;
  state->fieldDepth = fieldDepth;
  float fieldExtent = std::max(m_fieldWidth, m_fieldDepth);
  m_maxMapZoom = game::utils::CalculateMaxMapZoom(fieldExtent, kMinMapViewSpan,
                                                  m_baseMaxMapZoom);
  m_mapZoom = game::utils::ClampMapZoom(m_mapZoom, m_minMapZoom, m_maxMapZoom);
  m_targetMapZoom =
      game::utils::ClampMapZoom(m_targetMapZoom, m_minMapZoom, m_maxMapZoom);

  // カメラの描画距離（farZ）をフィールド奥行きに合わせて拡張
  auto *cam = ctx.world.Get<components::Camera>(m_cameraEntity);
  if (cam) {
    cam->farZ = std::max(1000.0f, fieldDepth * 2.5f);
  }

  // 6. 地形（フィールド）再構築
  LOG_DEBUG("WikiGolf", "Building field size: {}x{}", fieldWidth, fieldDepth);
  if (m_terrainSystem) {
    m_terrainSystem->BuildField(ctx, pageName, *m_wikiTexture, fieldWidth,
                                fieldDepth);
    m_floorEntity = m_terrainSystem->GetFloorEntity(); // カメラ追従などに必要
  }

  // 6.5 ボール位置をフィールドサイズに合わせて再配置
  auto *ballT = ctx.world.Get<Transform>(m_ballEntity);
  auto *ballRB = ctx.world.Get<RigidBody>(m_ballEntity);
  if (ballT) {
    // フィールド手前（-Z方向）の80%地点、中央X、床より少し上
    ballT->position = {0.0f, 1.0f, -fieldDepth * 0.4f};
    LOG_DEBUG("WikiGolf", "Ball repositioned to: ({}, {}, {})",
              ballT->position.x, ballT->position.y, ballT->position.z);
    if (ballRB) {
      ballRB->velocity = {0.0f, 0.0f, 0.0f}; // 速度リセット
    }
    SyncMapCenterToBall(ctx, 0.0f, true);
  } else {
    LOG_ERROR("WikiGolf", "Ball transform not found!");
  }

  // 7. ホール配置
  // 同じ座標に複数のホールを作らないよう追跡（座標ベース）
  std::vector<std::pair<float, float>> createdHolePositions;
  const float texWidthF = (float)m_wikiTexture->width;
  const float texHeightF = (float)m_wikiTexture->height;
  const float minHoleDistance = 0.2f; // ホール間の最小距離

  LOG_INFO("WikiGolf",
           "Hole placement: texture links count = {}, validLinks = {}, "
           "fieldSize = {}x{}",
           m_wikiTexture->links.size(), validLinks.size(), fieldWidth,
           fieldDepth);

  for (const auto &linkRegion : m_wikiTexture->links) {
    float texCenterX = linkRegion.x + linkRegion.width * 0.5f;
    float texCenterY = linkRegion.y + linkRegion.height * 0.5f;
    float worldX = (texCenterX / texWidthF - 0.5f) * fieldWidth;
    float worldZ = (0.5f - texCenterY / texHeightF) * fieldDepth;

    // 既に近い位置にホールがあるかチェック
    bool tooClose = false;
    for (const auto &pos : createdHolePositions) {
      float dx = worldX - pos.first;
      float dz = worldZ - pos.second;
      if (dx * dx + dz * dz < minHoleDistance * minHoleDistance) {
        tooClose = true;
        break;
      }
    }
    if (tooClose) {
      continue;
    }
    createdHolePositions.push_back({worldX, worldZ});

    // SDOW距離計算
    int hops = -1;
    if (m_shortestPath && m_shortestPath->IsAvailable() &&
        state->targetPageId != -1) {
      auto result = m_shortestPath->FindShortestPath(linkRegion.targetPage,
                                                     state->targetPageId, 6);
      if (result.success) {
        hops = result.degrees;
      }
    }

    LOG_DEBUG("WikiGolf",
              "Creating hole at ({}, {}) for '{}', isTarget={}, hops={}",
              worldX, worldZ, linkRegion.targetPage, linkRegion.isTarget, hops);

    CreateHole(ctx, worldX, worldZ, linkRegion.targetPage, linkRegion.isTarget,
               hops);
  }

  LOG_INFO("WikiGolf", "Total holes created: {}", createdHolePositions.size());

  // 8. 風設定
  float windSpeed = 0.0f;
  if (articleText.length() > 2000) {
    windSpeed = 3.0f + (float)(rand() % 20) / 10.0f;
  } else if (articleText.length() > 500) {
    windSpeed = 1.0f + (float)(rand() % 20) / 10.0f;
  }
  float windAngle = (float)(rand() % 360) * 3.14159f / 180.0f;
  DirectX::XMFLOAT2 windDir = {cosf(windAngle), sinf(windAngle)};

  state->windSpeed = windSpeed;
  state->windDirection = windDir;

  // 風UI更新
  auto *waUI = ctx.world.Get<UIImage>(state->windArrowEntity);
  if (waUI) {
    float angle = std::atan2(windDir.y, windDir.x) * 180.0f / 3.14159f;
    waUI->rotation = angle;
  }

  int dir8 = (int)((windAngle + 3.14159f / 8.0f) / (3.14159f / 4.0f)) % 8;
  const wchar_t *arrows[] = {L"→", L"↗", L"↑", L"↖", L"←", L"↙", L"↓", L"↘"};
  std::wstring windArrowStr =
      L"🌬️ " + std::to_wstring((int)(windSpeed * 10) / 10) + L"." +
      std::to_wstring((int)(windSpeed * 10) % 10) + L"m/s " + arrows[dir8];

  auto *windUI = ctx.world.Get<UIText>(state->windEntity);
  if (windUI) {
    windUI->text = windArrowStr;
  }

  // 9. その他HUD更新
  auto *headerUI = ctx.world.Get<UIText>(state->headerEntity);
  if (headerUI) {
    headerUI->text = L"📍 " + core::ToWString(pageName) + L" → 🎯 " +
                     core::ToWString(state->targetPage);
  }

  state->currentPage = pageName;
  state->pathHistory.push_back(pageName);

  auto *pathUI = ctx.world.Get<UIText>(state->pathEntity);
  if (pathUI) {
    std::wstring historyText = L"History: ";
    // 最新の5件くらいを表示するか、全部表示するか。一旦全部。
    // 長すぎるとあふれるので注意が必要だが、現状維持。
    // Historyの構築ロジックが必要。
    // state->pathHistoryを使って再構築
    for (size_t i = 0; i < state->pathHistory.size(); ++i) {
      if (i > 0)
        historyText += L" > ";
      historyText += core::ToWString(state->pathHistory[i]);
    }
    pathUI->text = historyText;
  }

  // Par計算
  int calculatedPar = -1;
  if (m_shortestPath) {
    game::systems::ShortestPathResult result;
    if (state->targetPageId != -1) {
      result =
          m_shortestPath->FindShortestPath(pageName, state->targetPageId, 20);
    } else {
      result =
          m_shortestPath->FindShortestPath(pageName, state->targetPage, 20);
    }
    if (result.success)
      calculatedPar = result.degrees;
  }
  m_calculatedPar = calculatedPar; // メンバ変数に保存（HUD更新用）

  // フォールバックとPar設定
  int par =
      (calculatedPar > 0) ? calculatedPar : (int)validLinks.size() / 2 + 2;
  state->par = par;

  // 最短パスとHUD更新
  // 最短パスとHUD更新
  std::wstring suffix = L" (推定)";
  if (calculatedPar > 0) {
    suffix = L" (残り最短 " + std::to_wstring(calculatedPar) + L" 記事)";
    LOG_INFO("WikiGolf", "Path found! Degrees: {}", calculatedPar);
  } else {
    LOG_INFO("WikiGolf", "Path calc failed or fallback used.");
  }

  // 表示更新
  auto *shotUI = ctx.world.Get<UIText>(state->shotCountEntity);
  if (shotUI) {
    shotUI->text = L"打数: " + std::to_wstring(state->shotCount) + L" / Par " +
                   std::to_wstring(state->par) + suffix;
    LOG_INFO("WikiGolf", "Updated HUD text: {}", core::ToString(shotUI->text));
  }
}

void WikiGolfScene::CreateHole(core::GameContext &ctx, float x, float z,
                               const std::string &linkTarget, bool isTargetHole,
                               int hopsToTarget) {
  auto *state = ctx.world.GetGlobal<game::components::GolfGameState>();
  if (!state)
    return;

  // 地形の高さを取得
  float terrainHeight = 0.0f;
  if (m_terrainSystem) {
    terrainHeight = m_terrainSystem->GetHeight(x, z);
  }

  auto e = CreateEntity(ctx.world);
  auto &t = ctx.world.Add<Transform>(e);
  // カップ表示：地形より上に配置して見えるようにする
  t.position = {x, terrainHeight + 0.05f, z};
  t.scale = {0.5f, 0.08f, 0.5f}; // ホール径（ビジュアル）

  auto &mr = ctx.world.Add<MeshRenderer>(e);
  mr.mesh = ctx.resource.LoadMesh("builtin/cylinder");
  mr.shader = ctx.resource.LoadShader("Basic", L"Assets/shaders/BasicVS.hlsl",
                                      L"Assets/shaders/BasicPS.hlsl");
  // ターゲットは目立つ色、他は黒（穴）
  if (isTargetHole) {
    mr.color = {0.8f, 0.0f, 0.0f, 1.0f}; // 赤
  } else {
    mr.color = {0.0f, 0.0f, 0.0f, 1.0f}; // 黒
  }

  auto &h = ctx.world.Add<GolfHole>(e);
  h.radius = 2.0f;  // 吸い込み有効半径を大きく
  h.gravity = 0.0f; // 吸引力OFF（テスト用）
  h.linkTarget = linkTarget;
  h.isTarget = isTargetHole;
  h.hopsToTarget = hopsToTarget;

  // 旗モデル（旗の根本が原点）
  auto flagE = CreateEntity(ctx.world);
  auto &flagT = ctx.world.Add<Transform>(flagE);
  flagT.position = {x, terrainHeight + 0.15f,
                    z};             // 少し上げて地面に埋もれないように
  flagT.scale = {1.2f, 1.2f, 1.2f}; // 見やすいサイズに調整

  auto &flagMr = ctx.world.Add<MeshRenderer>(flagE);
  flagMr.mesh = ctx.resource.LoadMesh("Assets/models/flag.obj");
  flagMr.shader = ctx.resource.LoadShader(
      "Basic", L"Assets/shaders/BasicVS.hlsl", L"Assets/shaders/BasicPS.hlsl");

  // 旗の色をホップ数に応じて設定
  // ターゲット: 赤、1ホップ: 金色、2ホップ: オレンジ、3+ホップ: 白、未計算:
  // 灰色
  if (isTargetHole) {
    flagMr.color = {1.0f, 0.2f, 0.2f, 1.0f}; // 赤（ターゲット）
  } else if (hopsToTarget == 1) {
    flagMr.color = {1.0f, 0.85f, 0.0f, 1.0f}; // 金色（1ホップ）
  } else if (hopsToTarget == 2) {
    flagMr.color = {1.0f, 0.6f, 0.2f, 1.0f}; // オレンジ（2ホップ）
  } else if (hopsToTarget >= 3 && hopsToTarget <= 5) {
    flagMr.color = {0.95f, 0.95f, 0.95f, 1.0f}; // 白（3-5ホップ）
  } else if (hopsToTarget > 5) {
    flagMr.color = {0.6f, 0.6f, 0.6f, 1.0f}; // 灰色（遠い）
  } else {
    flagMr.color = {0.5f, 0.5f, 0.5f, 1.0f}; // 暗い灰色（未計算）
  }

  auto &flagTag = ctx.world.Add<HoleFlag>(flagE);
  flagTag.holeEntity = e;

  // 3Dワールド座標ラベル（毎フレームスクリーン座標に変換）
  auto labelE = CreateEntity(ctx.world);

  // UITextエンティティ
  auto &labelUI = ctx.world.Add<UIText>(labelE);
  if (isTargetHole) {
    labelUI.text = L"🎯";
  } else {
    labelUI.text = L""; // 星表示なし
  }
  labelUI.style = graphics::TextStyle::Guide();
  labelUI.style.fontSize = 24.0f;
  if (isTargetHole) {
    labelUI.style.color = {1.0f, 0.3f, 0.3f, 1.0f};
  } else if (hopsToTarget == 1) {
    labelUI.style.color = {1.0f, 0.85f, 0.0f, 1.0f};
  } else if (hopsToTarget == 2) {
    labelUI.style.color = {1.0f, 0.6f, 0.2f, 1.0f};
  } else {
    labelUI.style.color = {0.9f, 0.9f, 0.9f, 1.0f};
  }
  labelUI.visible = false; // 初期は非表示（OnUpdateで表示制御）
  labelUI.layer = 60;

  // World3DLabelコンポーネント
  auto &label3D = ctx.world.Add<World3DLabel>(labelE);
  label3D.worldPos = {x, terrainHeight + 3.0f, z};
  label3D.uiTextEntity = labelE;
  label3D.offsetY = 3.0f;
  label3D.visible = true;

  h.labelEntity = labelE;

  // ターゲットホールと1ホップホールに光柱エフェクト追加
  if (isTargetHole || hopsToTarget == 1) {
    auto pillarE = CreateEntity(ctx.world);
    auto &pillarT = ctx.world.Add<Transform>(pillarE);
    float pillarHeight = isTargetHole ? 15.0f : 8.0f;
    pillarT.position = {x, terrainHeight + pillarHeight * 0.5f, z};
    pillarT.scale = {0.3f, pillarHeight, 0.3f};

    auto &pillarMr = ctx.world.Add<MeshRenderer>(pillarE);
    pillarMr.mesh = ctx.resource.LoadMesh("builtin/cylinder");
    pillarMr.shader =
        ctx.resource.LoadShader("Basic", L"Assets/shaders/BasicVS.hlsl",
                                L"Assets/shaders/BasicPS.hlsl");

    if (isTargetHole) {
      pillarMr.color = {1.0f, 0.3f, 0.3f, 0.4f}; // 赤い半透明光柱
    } else {
      pillarMr.color = {1.0f, 0.85f, 0.2f, 0.3f}; // 金色半透明光柱
    }

    // 光柱をホールに紐付け（ホール削除時に一緒に削除）
    h.pillarEntity = static_cast<uint32_t>(pillarE);
  }

  // ホールエンティティをリストに追加
  state->holes.push_back(e);

  LOG_DEBUG(
      "WikiGolf",
      "Created hole at ({}, {}, {}) for target '{}', isTarget={}, hops={}", x,
      t.position.y, z, linkTarget, isTargetHole, hopsToTarget);
}

void WikiGolfScene::CheckCupIn(core::GameContext &ctx) {
  auto *rb = ctx.world.Get<RigidBody>(m_ballEntity);
  auto *t = ctx.world.Get<Transform>(m_ballEntity);
  if (!rb || !t)
    return;

  // 速度チェック（ほぼ止まっているか）
  float speedSq = rb->velocity.x * rb->velocity.x +
                  rb->velocity.y * rb->velocity.y +
                  rb->velocity.z * rb->velocity.z;

  // カップ判定
  auto *state = ctx.world.GetGlobal<GolfGameState>();
  if (!state)
    return;

  for (auto holeEntity : state->holes) {
    auto *holeT = ctx.world.Get<Transform>(holeEntity);
    auto *hole = ctx.world.Get<GolfHole>(holeEntity);
    if (!holeT || !hole)
      continue;

    bool readyForCupIn = cupin::IsBallReadyForCupIn(
        t->position, holeT->position, hole->radius, speedSq);

    if (readyForCupIn) {
      // カップイン！
      LOG_INFO("WikiGolf", "Cup In! Target: {}", hole->linkTarget);

      // カップイン時間進行 (1時間)
      m_timeOfDay.OnCupIn(1.0f);

      // === 超派手なホールイン演出 ===
      if (m_gameJuice) {
        // 巨大カメラシェイク
        m_gameJuice->TriggerCameraShake(0.8f, 0.5f);

        // 複数回のパーティクル爆発（タイミングをずらして）
        XMFLOAT3 effectPos = holeT->position;
        effectPos.y += 0.5f; // ホールの上から発火

        m_gameJuice->TriggerSlowMotion(0.8f, 0.25f);

        // 1回目：中央大爆発
        m_gameJuice->TriggerImpactEffect(ctx, effectPos, 1.0f);

        // 2回目以降は高さを変えて（連続爆発風）
        effectPos.y += 1.0f;
        m_gameJuice->TriggerImpactEffect(ctx, effectPos, 0.8f);

        effectPos.y += 1.0f;
        m_gameJuice->TriggerImpactEffect(ctx, effectPos, 0.6f);

        // FOV変化（ズームイン→アウト）
        m_gameJuice->SetTargetFov(40.0f); // 一瞬ズームイン
        m_gameJuice->TriggerConfetti(ctx, effectPos, 1.0f);
      }

      // 音楽と効果音
      if (ctx.audio) {
        ctx.audio->PlaySE(ctx, "se_cupin.mp3");
        // ターゲット到達ならさらに派手な音
        if (hole->isTarget) {
          ctx.audio->PlaySE(ctx, "se_shot_hard.mp3"); // ボーナス音
        }
      }

      // ターゲット到達時は超派手に追加演出
      if (hole->isTarget && m_gameJuice) {
        // 追加のカメラシェイク
        m_gameJuice->TriggerCameraShake(1.0f, 1.0f);

        // 四方向にパーティクル発射
        XMFLOAT3 pos = holeT->position;
        pos.y += 0.5f;
        for (int i = 0; i < 4; ++i) {
          XMFLOAT3 offset = {std::cos((float)i * XM_PIDIV2) * 2.0f, 0.0f,
                             std::sin((float)i * XM_PIDIV2) * 2.0f};
          XMFLOAT3 effectPosExtra = {pos.x + offset.x, pos.y + 1.5f,
                                     pos.z + offset.z};
          m_gameJuice->TriggerImpactEffect(ctx, effectPosExtra, 1.0f);
        }
        m_gameJuice->TriggerConfetti(ctx, pos, 1.2f);
      }

      // ターゲットホールに入った場合はリザルト画面へ遷移
      if (hole->isTarget) {
        LOG_INFO("WikiGolf", "GAME CLEAR! Reached target page!");

        if (ctx.audio) {
          ctx.audio->PlaySE(ctx, "se_goal", 1.0f);
        }

        // ResultSceneへ遷移
        ResultData data;
        data.targetPage = state->targetPage;
        data.shotCount = state->shotCount;
        data.par = state->par;
        data.pathHistory = state->pathHistory;
        data.isNewRecord = false; // TODO: スコア保存ロジック

        if (ctx.sceneManager) {
          ctx.sceneManager->ChangeScene(std::make_unique<ResultScene>(data));
        }
        return;
      }

      TransitionToPage(ctx, hole->linkTarget);
      return; // 1フレームに1回だけ遷移
    }
  }
}

void WikiGolfScene::SwitchClub(core::GameContext &ctx, int direction) {
  m_currentClubIndex += direction;
  if (m_currentClubIndex < 0)
    m_currentClubIndex = m_availableClubs.size() - 1;
  if (m_currentClubIndex >= m_availableClubs.size())
    m_currentClubIndex = 0;

  m_currentClub = m_availableClubs[m_currentClubIndex];
  if (auto *state = ctx.world.GetGlobal<GolfGameState>()) {
    state->rollingFrictionScale = m_currentClub.rollingFrictionScale;
  }

  // カメラ設定更新
  if (m_currentClub.name == "Putter") {
    m_targetCameraDistance = 4.0f * kFieldScale;
    m_targetCameraHeight = 8.0f * kFieldScale; // 真上から見下ろす
  } else if (m_currentClub.name == "Wedge") {
    m_targetCameraDistance = 10.0f * kFieldScale;
    m_targetCameraHeight = 6.0f * kFieldScale;
  } else {
    m_targetCameraDistance = 15.0f * kFieldScale;
    m_targetCameraHeight = 5.0f * kFieldScale;
  }

  LOG_INFO("WikiGolf", "Switched Club: {}", m_currentClub.name);

  // UI更新
  for (size_t i = 0; i < m_clubUIEntities.size(); ++i) {
    auto *ui = ctx.world.Get<UIImage>(m_clubUIEntities[i]);
    if (ui) {
      ui->alpha = (i == m_currentClubIndex) ? 1.0f : 0.5f;
    }
  }

  // 軌道予測更新
  auto *shot = ctx.world.GetGlobal<ShotState>();
  if (shot) {
    UpdateTrajectory(ctx, shot->confirmedPower > 0 ? shot->confirmedPower
                                                   : shot->powerGaugePos);
  }
}

void WikiGolfScene::InitializeClubModel(core::GameContext &ctx) {
  // ゴルフクラブ3Dモデルエンティティ作成
  m_clubModelEntity = CreateEntity(ctx.world);

  auto &tr = ctx.world.Add<Transform>(m_clubModelEntity);
  tr.position = {0.0f, 0.5f, 0.0f};
  tr.scale = {0.5f, 0.5f, 0.5f}; // モデルサイズ調整

  auto &mr = ctx.world.Add<MeshRenderer>(m_clubModelEntity);
  mr.mesh = ctx.resource.LoadMesh("Assets/models/golf_club.fbx");
  mr.shader = ctx.resource.LoadShader("Basic", L"Assets/shaders/BasicVS.hlsl",
                                      L"Assets/shaders/BasicPS.hlsl");
  mr.color = {0.9f, 0.9f, 0.9f, 1.0f}; // 銀色っぽい
  mr.isVisible = false;                // 最初は非表示

  // アニメーション状態初期化
  m_clubAnimPhase = ClubAnimPhase::Idle;
  m_clubSwingAngle = 0.0f;
  m_clubSwingSpeed = 0.0f;
  m_clubAnimTimer = 0.0f;

  LOG_INFO("WikiGolf", "Golf club model initialized");
}

void WikiGolfScene::UpdateClubAnimation(core::GameContext &ctx, float dt) {
  if (!ctx.world.IsAlive(m_clubModelEntity) ||
      !ctx.world.IsAlive(m_ballEntity)) {
    return;
  }

  auto *clubTr = ctx.world.Get<Transform>(m_clubModelEntity);
  auto *clubMr = ctx.world.Get<MeshRenderer>(m_clubModelEntity);
  auto *ballTr = ctx.world.Get<Transform>(m_ballEntity);
  auto *shot = ctx.world.GetGlobal<ShotState>();

  if (!clubTr || !clubMr || !ballTr || !shot) {
    return;
  }

  // クラブの基準位置: ボールの横
  const float clubOffsetX = -0.8f; // ボールの左側
  const float clubOffsetY = 0.3f;  // 少し上
  const float clubOffsetZ = -0.3f; // 少し後ろ

  // ショット方向に基づいてクラブ位置を回転
  XMVECTOR shotDir = XMLoadFloat3(&m_shotDirection);
  shotDir = XMVector3Normalize(shotDir);

  float yaw = std::atan2(m_shotDirection.x, m_shotDirection.z);

  // ローカルオフセットを回転して適用
  XMVECTOR localOffset = XMVectorSet(clubOffsetX, clubOffsetY, clubOffsetZ, 0);
  XMMATRIX rotMatrix = XMMatrixRotationY(yaw);
  XMVECTOR worldOffset = XMVector3Transform(localOffset, rotMatrix);

  XMVECTOR clubBasePos =
      XMVectorAdd(XMLoadFloat3(&ballTr->position), worldOffset);

  // フェーズに基づいてアニメーション制御
  switch (shot->phase) {
  case ShotState::Phase::Idle:
    // 待機状態：クラブ非表示
    clubMr->isVisible = false;
    m_clubAnimPhase = ClubAnimPhase::Idle;
    m_clubSwingAngle = 0.0f;
    break;

  case ShotState::Phase::PowerCharging: {
    // パワーチャージ中：バックスイング（振りかぶり）
    clubMr->isVisible = true;
    m_clubAnimPhase = ClubAnimPhase::Backswing;

    // パワーゲージに応じてバックスイング角度を増加
    // 0% → 0度, 100% → -90度（後方に振りかぶる）
    float targetAngle = -shot->powerGaugePos * 90.0f;
    m_clubSwingAngle += (targetAngle - m_clubSwingAngle) * 8.0f * dt;
    break;
  }

  case ShotState::Phase::ImpactTiming: {
    // インパクトタイミング中：バックスイング維持
    clubMr->isVisible = true;
    float targetAngle = -shot->confirmedPower * 90.0f;
    m_clubSwingAngle += (targetAngle - m_clubSwingAngle) * 8.0f * dt;
    break;
  }

  case ShotState::Phase::Executing: {
    // ショット実行：ダウンスイング→フォロースルー
    clubMr->isVisible = true;

    if (m_clubAnimPhase != ClubAnimPhase::Downswing &&
        m_clubAnimPhase != ClubAnimPhase::FollowThrough) {
      m_clubAnimPhase = ClubAnimPhase::Downswing;
      m_clubSwingSpeed = 800.0f; // 高速スイング開始
      m_clubAnimTimer = 0.0f;
    }

    if (m_clubAnimPhase == ClubAnimPhase::Downswing) {
      // ダウンスイング: 急速に前方へ
      m_clubSwingAngle += m_clubSwingSpeed * dt;
      m_clubSwingSpeed *= 0.92f; // 減速

      if (m_clubSwingAngle >= 60.0f) {
        m_clubAnimPhase = ClubAnimPhase::FollowThrough;
        m_clubSwingAngle = 60.0f;
      }
    } else if (m_clubAnimPhase == ClubAnimPhase::FollowThrough) {
      // フォロースルー: 徐々に戻る
      m_clubAnimTimer += dt;
      if (m_clubAnimTimer > 0.4f) {
        m_clubSwingAngle += (0.0f - m_clubSwingAngle) * 3.0f * dt;
      }

      // 1秒後に非表示
      if (m_clubAnimTimer > 1.0f) {
        clubMr->isVisible = false;
        m_clubAnimPhase = ClubAnimPhase::Idle;
      }
    }
    break;
  }

  case ShotState::Phase::ShowResult:
    // 結果表示中：フォロースルー継続
    if (m_clubAnimPhase == ClubAnimPhase::FollowThrough) {
      m_clubAnimTimer += dt;
      m_clubSwingAngle += (0.0f - m_clubSwingAngle) * 3.0f * dt;
      if (m_clubAnimTimer > 1.0f) {
        clubMr->isVisible = false;
        m_clubAnimPhase = ClubAnimPhase::Idle;
      }
    } else {
      clubMr->isVisible = false;
    }
    break;

  default:
    clubMr->isVisible = false;
    break;
  }

  // スイング角度をX軸回転として適用（ピッチ）
  // Y軸はショット方向に合わせる
  float pitchRad = XMConvertToRadians(m_clubSwingAngle);
  XMVECTOR q = XMQuaternionRotationRollPitchYaw(pitchRad, yaw, 0);
  XMStoreFloat4(&clubTr->rotation, q);

  // クラブの変形（位置）を適用
  // モデルの原点(ヘッド終端)が原点のため、グリップ位置(clubBasePos)から逆算して配置
  // モデル座標系でY軸プラス方向がシャフト(グリップ側)と仮定
  float clubModelLength = 2.0f; // モデル上の長さ（スケール前）
  float scaledLen = clubModelLength * clubTr->scale.y;

  // HeadPos = GripPos - (Rotation * GripOffsetFromHead)
  // グリップはヘッドから見て (0, Length, 0) にあると仮定
  XMVECTOR gripOffset = XMVectorSet(0.0f, scaledLen, 0.0f, 0.0f);
  gripOffset = XMVector3Rotate(gripOffset, q);

  XMVECTOR headPos = XMVectorSubtract(clubBasePos, gripOffset);
  XMStoreFloat3(&clubTr->position, headPos);
}

void WikiGolfScene::OnExit(core::GameContext &ctx) {
  if (m_terrainSystem) {
    m_terrainSystem->Clear(ctx);
  }
  m_screenFade.Shutdown(ctx);
  DestroyAllEntities(ctx);
  LOG_INFO("WikiGolf", "Exiting WikiGolfScene");
  Scene::OnExit(ctx);
}

} // namespace game::scenes
