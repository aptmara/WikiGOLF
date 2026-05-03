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

/**
 * @brief シーンに侵入した際の初期化処理を行います。
 */
void WikiGolfScene::OnEnter(core::GameContext &ctx) {
  LOG_INFO("WikiGolf", "OnEnter");

  m_screenFade.Initialize(ctx);
  m_screenFade.FadeIn(1.5f, game::utils::FadeType::HexagonWipe,
                      {1.0f, 1.0f, 1.0f}); // 白で明ける

  if (ctx.audio) {
    ctx.audio->PlayBGM(ctx, "bgm_game.mp3", 0.3f);
  }

  ctx.input.SetMouseCursorVisible(true);
  ctx.input.SetMouseCursorLocked(false);

  // 残存エンティティのクリーンアップ
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

  // 地形システムの初期化
  m_terrainSystem = std::make_unique<game::systems::WikiTerrainSystem>();

  // ページローダーの初期化
  m_pageLoader = std::make_unique<WikiPageLoader>();
  
  // フィールドの初期化
  CreateField(ctx);

  // 各種エンティティの初期化
  m_cameraEntity = CreateEntity(ctx.world);
  auto &t = ctx.world.Add<Transform>(m_cameraEntity);
  t.position = {0.0f, 15.0f * kFieldScale, -15.0f * kFieldScale};

  auto &camComp = ctx.world.Add<Camera>(m_cameraEntity);
  camComp.fov = XMConvertToRadians(60.0f);
  camComp.aspectRatio = 1280.0f / 720.0f;
  camComp.nearZ = 0.1f;
  camComp.farZ = 750.0f;

  m_arrowEntity = CreateEntity(ctx.world);
  auto &at = ctx.world.Add<Transform>(m_arrowEntity);
  at.scale = {0.0f, 0.0f, 0.0f};

  m_guideArrowEntity = CreateEntity(ctx.world);
  auto &gat = ctx.world.Add<Transform>(m_guideArrowEntity);
  gat.scale = {0.15f, 0.1f, 2.0f};

  auto &gamr = ctx.world.Add<MeshRenderer>(m_guideArrowEntity);
  gamr.mesh = ctx.resource.LoadMesh("builtin/cube");
  gamr.shader = ctx.resource.LoadShader("Basic", L"Assets/shaders/BasicVS.hlsl", L"Assets/shaders/BasicPS.hlsl");
  gamr.color = {0.3f, 0.8f, 1.0f, 0.6f};
  gamr.isTransparent = true;
  gamr.isVisible = false;

  auto &amr = ctx.world.Add<MeshRenderer>(m_arrowEntity);
  amr.mesh = ctx.resource.LoadMesh("builtin/cube");
  amr.shader = ctx.resource.LoadShader("Basic", L"Assets/shaders/BasicVS.hlsl", L"Assets/shaders/BasicPS.hlsl");
  amr.color = {1.0f, 0.4f, 0.2f, 1.0f};
  amr.isVisible = false;

  m_gameJuice = std::make_unique<game::systems::GameJuiceSystem>();
  m_gameJuice->Initialize(ctx);
  ctx.world.SetGlobal(m_gameJuice.get());

  // スカイボックスシステムの初期化
  m_skyboxGenerator = std::make_unique<graphics::SkyboxTextureGenerator>();

  m_skyboxEntity = CreateEntity(ctx.world);
  auto &skyboxComp = ctx.world.Add<components::Skybox>(m_skyboxEntity);
  skyboxComp.isVisible = true;
  skyboxComp.brightness = 0.7f; // 床の文字を見やすくするため控えめ
  skyboxComp.saturation = 0.8f; // 彩度も抑えめ

  std::wstring skyboxBasePath = L"Assets/textures/runtime_skybox/skybox_Default";
  if (m_skyboxGenerator->LoadCubemapFromFiles(
          ctx.graphics.GetDevice(), skyboxBasePath, skyboxComp.cubemapSRV)) {
    LOG_INFO("WikiGolf", "Skybox loaded from static files: skybox_Default");
  } else {
    LOG_WARN("WikiGolf", "Failed to load skybox from static files");
    skyboxComp.isVisible = false;
  }


  // 環境効果システムの初期化
  m_timeOfDay.Initialize(8.0f); // 朝8時スタート
  m_postProcess.Initialize(ctx.graphics.GetDevice());
  m_postProcess.ResetToDefaults();

  m_particleRenderSystem.Initialize(ctx.graphics.GetDevice());

  // 地形判定UIの初期化
  if (m_terrainImageEntity == UINT32_MAX) {
      m_terrainImageEntity = CreateEntity(ctx.world);
      auto& ui = ctx.world.Add<game::components::UIImage>(m_terrainImageEntity);
      ui.texturePath = "";
      ui.x = 1280.0f * 0.5f;
      ui.y = 720.0f * 0.5f;
      ui.width = 0.0f;
      ui.height = 0.0f;
      ui.visible = false;
      ui.layer = 130; // 判定テキストと同層
      ui.alpha = 0.0f;
  }

  std::string targetPage;
  int targetId = -1;
  bool isUserOverride = false;

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

    if (preloadedData->hasCachedData) {
      LOG_INFO("WikiGolf",
               "Found cached page data. Skipping initial network request.");
      m_pageLoader->SetPreloadedData(preloadedData->cachedLinks, preloadedData->cachedExtract);
    }
  } else {
    LOG_INFO("WikiGolf", "No preloaded data found or pathSystem invalid. "
                         "Falling back to sync load.");

    game::systems::WikiClient wikiClient;
    startPage = wikiClient.FetchRandomPageTitle();

    if (!m_shortestPath) {
      m_shortestPath = std::make_unique<game::systems::WikiShortestPath>();
      if (!m_shortestPath->Initialize("Assets/data/jawiki_sdow.sqlite")) {
        LOG_WARN("WikiGolf", "SDOW DB not found for target selection");
        m_shortestPath.reset();
      }
    }

    if (m_shortestPath && m_shortestPath->IsAvailable()) {
      auto result = m_shortestPath->FetchPopularPageTitle(100);
      targetPage = result.first;
      targetId = result.second;

      if (targetPage.empty()) {
        result = m_shortestPath->FetchPopularPageTitle(50);
        targetPage = result.first;
        targetId = result.second;
      }
    }

    if (targetPage.empty()) {
      targetPage = wikiClient.FetchTargetPageTitle();
    }

    if (startPage == targetPage) {
      targetPage = wikiClient.FetchTargetPageTitle();
      targetId = -1; // 再取得のためID不明
    }
  }

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

  LOG_DEBUG("WikiGolf", "After CreateField: Cam Alive={}",
            ctx.world.IsAlive(m_cameraEntity) ? "true" : "false");

  SpawnBall(ctx);
  LOG_DEBUG("WikiGolf", "After SpawnBall: Cam Alive={}",
            ctx.world.IsAlive(m_cameraEntity) ? "true" : "false");

  
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
  
  
  LOG_INFO("WikiGolf", "Saving global state...");
  ctx.world.SetGlobal(state);

  ShotState shotState;
  ctx.world.SetGlobal(shotState);

  LOG_DEBUG("WikiGolf", "Before LoadPage: Cam Alive={}",
            ctx.world.IsAlive(m_cameraEntity) ? "true" : "false");
  
  m_pageLoader->SetSystems(m_textureGenerator.get(), m_terrainSystem.get(), m_skyboxGenerator.get(), m_shortestPath.get());

  // コントローラの初期化
  m_cameraController = std::make_unique<game::controllers::CameraController>();
  game::controllers::CameraController::Config camCfg;
  camCfg.cameraEntity = m_cameraEntity;
  camCfg.ballEntity = m_ballEntity;
  camCfg.floorEntity = m_floorEntity;
  camCfg.fieldScale = kFieldScale;
  camCfg.terrain = m_terrainSystem.get();
  camCfg.gameJuice = m_gameJuice.get();
  m_cameraController->Initialize(camCfg);

  m_minimapController = std::make_unique<game::controllers::MinimapController>();
  game::controllers::MinimapController::Config miniCfg;
  miniCfg.cameraEntity = m_cameraEntity;
  miniCfg.ballEntity = m_ballEntity;
  miniCfg.fieldScale = kFieldScale;
  m_minimapController->Initialize(miniCfg, ctx);
  m_minimapController->InitializeUI(ctx);

  m_hud = std::make_unique<game::controllers::WikiGolfHUD>();
  m_hud->Initialize(ctx);

  m_clubController = std::make_unique<game::controllers::ClubController>();
  m_clubController->Initialize(ctx);
  if (auto *state = ctx.world.GetGlobal<GolfGameState>()) {
      state->rollingFrictionScale =
          m_clubController->GetCurrentClub().rollingFrictionScale;
  }

  m_trajectoryPredictor = std::make_unique<game::controllers::TrajectoryPredictor>();
  m_trajectoryPredictor->Initialize(ctx, 30);

  m_shotController = std::make_unique<game::controllers::ShotController>();

  m_transitionController = std::make_unique<game::controllers::ArticleTransitionController>();
  m_transitionController->Initialize(ctx);
  
  if (m_transitionController) {
      m_phase = ScenePhase::Transitioning;
      // ロード中は地球儀+カートのみ表示するためHUD/ミニマップを非表示
      if (m_hud) m_hud->SetVisible(ctx, false);
      if (m_minimapController) m_minimapController->SetVisible(ctx, false);
      // ガイド矢印を非表示
      if (ctx.world.IsAlive(m_guideArrowEntity)) {
          if (auto* mr = ctx.world.Get<MeshRenderer>(m_guideArrowEntity)) mr->isVisible = false;
      }
      m_transitionController->StartTransition(ctx, startPage, m_pageLoader.get(), m_ballEntity, m_cameraEntity, m_skyboxEntity, m_minimapController.get());
  }
  LOG_DEBUG("WikiGolf", "After LoadPage: Cam Alive={}",
            ctx.world.IsAlive(m_cameraEntity) ? "true" : "false");
}

/**
 * @brief フィールド（床・壁）を作成します。
 */
void WikiGolfScene::CreateField(core::GameContext &ctx) {
  m_floorEntity = CreateEntity(ctx.world);
  auto &ft = ctx.world.Add<Transform>(m_floorEntity);
  ft.position = {0.0f, 0.0f, 0.0f};
  ft.scale = {20.0f * kFieldScale, 0.5f * kFieldScale, 30.0f * kFieldScale};

  // 地形下の白いプレーン表示を防ぐため床のコンポーネント付与を廃止
}

/**
 * @brief ボールをスポーンします。
 */
void WikiGolfScene::SpawnBall(core::GameContext &ctx) {
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
  mr.shader = ctx.resource.LoadShader("Basic", L"Assets/shaders/BasicVS.hlsl",
                                      L"Assets/shaders/BasicPS.hlsl");
  mr.color = {1.0f, 0.6f, 0.2f, 1.0f}; // 少しオレンジで視認性アップ

  auto &rb = ctx.world.Add<RigidBody>(m_ballEntity);
  rb.isStatic = false;
  rb.mass = 0.0459f;      // 規定質量 45.9g
  rb.restitution = 0.35f; // 反発係数 (現実の芝との衝突)
  rb.drag = 0.30f;        // 空気抵抗係数 (Cd値)
  rb.rollingFriction = 0.25f; // 転がり抵抗を設定
  rb.velocity = {0, 0, 0};

  auto &c = ctx.world.Add<Collider>(m_ballEntity);
  c.type = ColliderType::Sphere;
  c.radius = 0.02135f; // 規定半径 21.35mm

  auto *state = ctx.world.GetGlobal<GolfGameState>();
  if (state)
    state->ballEntity = m_ballEntity;
}

/**
 * @brief 指定したページへ遷移します。
 */
void WikiGolfScene::TransitionToPage(core::GameContext &ctx,
                                     const std::string &pageName) {
  LOG_INFO("WikiGolf", "Transitioning to page: {}", pageName);

  auto *state = ctx.world.GetGlobal<GolfGameState>();
  if (!state)
    return;

  auto *shot = ctx.world.GetGlobal<ShotState>();

  state->moveCount++;
  state->shotCount = 0;
  state->canShoot = true;

  if (shot) {
    shot->Reset();
  }
  if (m_hud) {
    m_hud->ResetShotUI(ctx);
  }

  // ページ移動時間進行 (2時間)
  m_timeOfDay.OnPageTransition(2.0f);

  // カメラリセット
  if (m_cameraController) {
    m_cameraController->ResetForTransition(kFieldScale);
  }

  // トレイルリセット（遷移時）
  if (m_gameJuice) {
    m_gameJuice->ResetTrail();
  }

  if (m_transitionController && m_pageLoader) {
    m_phase = ScenePhase::Transitioning;
    // ロード中は地球儀+カートのみ表示するためHUD/ミニマップを非表示
    if (m_hud) m_hud->SetVisible(ctx, false);
    if (m_minimapController) m_minimapController->SetVisible(ctx, false);
    // ガイド矢印を非表示
    if (ctx.world.IsAlive(m_guideArrowEntity)) {
        if (auto* mr = ctx.world.Get<MeshRenderer>(m_guideArrowEntity)) mr->isVisible = false;
    }
    m_transitionController->StartTransition(ctx, pageName, m_pageLoader.get(),
                                            m_ballEntity, m_cameraEntity,
                                            m_skyboxEntity,
                                            m_minimapController.get());
  } else if (m_pageLoader) {
    m_pageLoader->LoadPage(ctx, pageName, m_ballEntity, m_cameraEntity,
                           m_skyboxEntity, m_minimapController.get());
  }

  if (ctx.audio) {
    ctx.audio->PlaySE(ctx, "se_warp.mp3");
  }
}

/**
 * @brief シーンを抜ける際の後処理を行います。
 */
void WikiGolfScene::OnExit(core::GameContext &ctx) {
  if (m_terrainSystem) {
    m_terrainSystem->Clear(ctx);
  }

  // HUDとミニマップのエンティティを明示的に破棄（リーク防止）
  if (m_hud) {
    m_hud->SetVisible(ctx, false); // 一旦非表示
    // WikiGolfHUD に Destroy メソッドがない場合は、Scene::DestroyAllEntities に依存するが、
    // それらは m_entities に入っている必要がある。
    // WikiGolfHUD::Initialize で CreateEntity(ctx.world) しているが m_entities に入っていない可能性が高い。
  }

  if (m_minimapController) {
    m_minimapController->SetVisible(ctx, false);
    m_minimapController->ClearHoleIcons(ctx);
  }

  m_screenFade.Shutdown(ctx);

  // 全エンティティの強制クリーンアップ（このシーンで作成されたもの以外も含む）
  std::vector<ecs::Entity> allEntities;
  ctx.world.Query<components::Transform>().Each([&](ecs::Entity e, components::Transform &) {
    allEntities.push_back(e);
  });
  // UIText や UIImage だけ持っているものも対象にする必要がある
  ctx.world.Query<components::UIText>().Each([&](ecs::Entity e, components::UIText &) {
    allEntities.push_back(e);
  });
  ctx.world.Query<components::UIImage>().Each([&](ecs::Entity e, components::UIImage &) {
    allEntities.push_back(e);
  });

  // 重複排除
  std::sort(allEntities.begin(), allEntities.end());
  allEntities.erase(std::unique(allEntities.begin(), allEntities.end()), allEntities.end());

  for (auto e : allEntities) {
    if (ctx.world.IsAlive(e)) {
      ctx.world.DestroyEntity(e);
    }
  }

  DestroyAllEntities(ctx);

  // グローバルデータから演出システムを削除（ダングリングポインタ防止）
  ctx.world.SetGlobal<game::systems::GameJuiceSystem *>(nullptr);

  LOG_INFO("WikiGolf", "Exiting WikiGolfScene (Cleaned up all entities)");
  Scene::OnExit(ctx);
}


/**
 * @brief シーンの毎フレーム更新処理を行います。
 */
void WikiGolfScene::OnUpdate(core::GameContext &ctx) {
  const float dt = ctx.dt;
  m_screenFade.Update(dt);

  auto *state = ctx.world.GetGlobal<game::components::GolfGameState>();
  auto *shot = ctx.world.GetGlobal<game::components::ShotState>();
  if (!state || !shot) return;

  if (m_phase == ScenePhase::Transitioning && m_transitionController) {
      bool finished = m_transitionController->Update(ctx);
      if (finished) {
          m_phase = ScenePhase::Playing;
          // ロード完了: HUD/ミニマップを再表示する
          if (m_hud) m_hud->SetVisible(ctx, true);
          if (m_minimapController) m_minimapController->SetVisible(ctx, true);
          if (m_cameraController) m_cameraController->Update(ctx);
      }
      return;
  }

  auto mousePos = ctx.input.GetMousePosition();
  int mouseX = mousePos.x;
  int mouseY = mousePos.y;

  // マップビュー更新
  bool isMapView = false;
  if (m_minimapController) {
      float fieldW = m_pageLoader ? m_pageLoader->GetFieldWidth() : 80.0f;
      float fieldD = m_pageLoader ? m_pageLoader->GetFieldDepth() : 120.0f;
      m_minimapController->ProcessInput(ctx, mouseX, mouseY, fieldW, fieldD, m_skyboxEntity);
      DirectX::XMFLOAT3 shotDir = m_cameraController ? m_cameraController->GetShotDirection() : DirectX::XMFLOAT3(0,0,1);
      m_minimapController->UpdateMinimap(ctx, fieldW, fieldD, shotDir);
      if (m_minimapController->IsMapView()) {
          m_minimapController->UpdateMapCamera(ctx, fieldW, fieldD);
      }
      isMapView = m_minimapController->IsMapView();
  }

  // クラブ更新
  if (m_clubController && !isMapView && shot->phase == game::components::ShotState::Phase::Idle) {
      game::controllers::ClubController::InputParams cParams;
      cParams.allowInput = state->canShoot;
      auto cResult = m_clubController->UpdateInput(ctx, cParams);
      if (cResult.clubChanged && m_cameraController) {
          m_cameraController->SetTargetDistanceAndHeight(
              m_clubController->GetRecommendedCameraDistance(4.0f),
              m_clubController->GetRecommendedCameraHeight(4.0f));
      }
  }

  // カメラ更新
  if (m_cameraController && !isMapView) {
      m_cameraController->ProcessInput(ctx, mouseX, mouseY);
      m_cameraController->Update(ctx);
  }

  // ショット処理
  if (m_shotController && !isMapView) {
      auto event = m_shotController->ProcessShot(ctx, state->canShoot, m_hud.get(), m_clubController.get());
      if (event.shotFired) {
          state->canShoot = false;
          if (m_cameraController) m_cameraController->OnShotStart(ctx, shot->confirmedPower);
          float clubPower = m_clubController ? m_clubController->GetCurrentClub().maxPower : 30.0f;
          float clubAngle = m_clubController ? m_clubController->GetCurrentClub().launchAngle : 30.0f;
          DirectX::XMFLOAT3 shotDir = m_cameraController ? m_cameraController->GetShotDirection() : DirectX::XMFLOAT3(0,0,1);
          m_shotController->ExecuteShot(ctx, m_ballEntity, shotDir, clubPower, clubAngle, &m_timeOfDay, m_hud.get());

          // 打球判定の演出表示
          auto feedback = game::utils::BuildJudgeFeedback(shot->judgement);
          if (feedback.HasVisual() && m_terrainImageEntity != UINT32_MAX) {
              auto* ui = ctx.world.Get<game::components::UIImage>(m_terrainImageEntity);
              if (ui) {
                  ui->texturePath = feedback.texturePath;
                  ui->visible = true;
                  ui->alpha = 0.0f;
                  ui->width = 0.0f;
                  ui->height = 0.0f;
                  ui->x = 1280.0f * 0.5f;
                  ui->y = 720.0f * 0.5f;
                  m_terrainDisplayTimer = feedback.displaySeconds;
              }
          }
      }
  }
  
  // クラブアニメーション更新
  if (m_clubController) {
      DirectX::XMFLOAT3 shotDir = m_cameraController ? m_cameraController->GetShotDirection() : DirectX::XMFLOAT3(0,0,1);
      m_clubController->UpdateAnimation(ctx, dt, m_ballEntity, shotDir);
  }
  
  // 物理更新
  game::systems::PhysicsSystem(ctx, dt);
  
  // カップイン判定を地形判定の前に行う（遷移時は以降の処理をスキップ）
  if (CheckCupIn(ctx)) return;

  // ボール静止・OB・地形判定ロジック
  if (shot->phase == game::components::ShotState::Phase::Executing) {
      auto *rb = ctx.world.Get<game::components::RigidBody>(m_ballEntity);
      if (rb) {
          float speed = std::sqrt(rb->velocity.x * rb->velocity.x +
                                  rb->velocity.y * rb->velocity.y +
                                  rb->velocity.z * rb->velocity.z);
          if (speed < 0.1f) {
              rb->velocity = {0, 0, 0};
              std::string terrainTex = "";
              bool treatAsOB = false;

              auto GetTerrainTex = [](const std::string& preferred, const std::string& fallback) {
                  if (std::filesystem::exists("Assets/textures/" + preferred)) {
                      return preferred;
                  }
                  return fallback;
              };

              if (state->isOB) {
                  terrainTex = GetTerrainTex("ui_terrain_ob.png", "ui_judge_miss.png");
                  LOG_INFO("WikiGolf", "OB! Returning to last shot position");
                  state->shotCount++;
                  auto *ballT = ctx.world.Get<game::components::Transform>(m_ballEntity);
                  if (ballT) {
                      ballT->position = state->lastShotPosition;
                      ballT->position.y += 0.5f;
                  }
                  state->isOB = false;
                  
                  if (ctx.audio) {
                      if (std::filesystem::exists("Assets/sounds/se_OB.wav")) {
                          ctx.audio->PlaySE(ctx, "se_OB.wav", 0.8f);
                      } else {
                          ctx.audio->PlaySE(ctx, "se_judge_ob.mp3", 0.8f);
                      }
                  }
              } else {
                  switch (state->currentMaterial) {
                  case game::components::TerrainMaterial::Fairway:
                      terrainTex = GetTerrainTex("ui_terrain_fairway.png", "ui_judge_nice.png");
                      break;
                  case game::components::TerrainMaterial::Rough:
                      terrainTex = GetTerrainTex("ui_terrain_rough.png", "ui_judge_miss.png");
                      break;
                  case game::components::TerrainMaterial::Bunker:
                      terrainTex = GetTerrainTex("ui_terrain_bunker.png", "ui_judge_miss.png");
                      break;
                  case game::components::TerrainMaterial::Green:
                      terrainTex = GetTerrainTex("ui_terrain_green.png", "ui_judge_perfect.png");
                      break;
                  default: 
                      // Ice, Water, Stone, Lava などはすべてOB扱い
                      terrainTex = GetTerrainTex("ui_terrain_ob.png", "ui_judge_miss.png");
                      treatAsOB = true;
                      break;
                  }

                  if (treatAsOB) {
                      LOG_INFO("WikiGolf", "Hazard! Treating as OB. Material: {}", (int)state->currentMaterial);
                      state->shotCount++;
                      auto *ballT = ctx.world.Get<game::components::Transform>(m_ballEntity);
                      if (ballT) {
                          ballT->position = state->lastShotPosition;
                          ballT->position.y += 0.5f;
                      }
                      if (ctx.audio) {
                          if (std::filesystem::exists("Assets/sounds/se_OB.wav")) {
                              ctx.audio->PlaySE(ctx, "se_OB.wav", 0.8f);
                          } else {
                              ctx.audio->PlaySE(ctx, "se_judge_ob.mp3", 0.8f);
                          }
                      }
                  }
              }

              if (!terrainTex.empty() && m_terrainImageEntity != UINT32_MAX) {
                  auto *ui = ctx.world.Get<game::components::UIImage>(m_terrainImageEntity);
                  if (ui) {
                      ui->texturePath = terrainTex;
                      ui->visible = true;
                      ui->alpha = 0.0f; // Updateでフェードイン
                      ui->width = 0.0f;
                      ui->height = 0.0f;
                      ui->x = 1280.0f * 0.5f;
                      ui->y = 720.0f * 0.5f;
                      m_terrainDisplayTimer = 2.0f;

                      if (ctx.audio && !state->isOB && !treatAsOB) {
                          std::string seName = "";
                          switch (state->currentMaterial) {
                          case game::components::TerrainMaterial::Fairway: 
                              seName = std::filesystem::exists("Assets/sounds/se_Fairway.wav") ? "se_Fairway.wav" : "se_Fairway.mp3";
                              break;
                          case game::components::TerrainMaterial::Rough:
                              seName = std::filesystem::exists("Assets/sounds/se_Rough.wav") ? "se_Rough.wav" : "se_Rough.mp3";
                              break;
                          case game::components::TerrainMaterial::Bunker:
                              seName = std::filesystem::exists("Assets/sounds/se_Bunker_new.mp3") ? "se_Bunker_new.mp3" : "se_Bunker.mp3";
                              break;
                          case game::components::TerrainMaterial::Green:
                              seName = std::filesystem::exists("Assets/sounds/se_Green.mp3") ? "se_Green.mp3" : 
                                      (std::filesystem::exists("Assets/sounds/se_Fairway.wav") ? "se_Fairway.wav" : "se_Fairway.mp3");
                              break;
                          default: break;
                          }

                          if (!seName.empty() && std::filesystem::exists("Assets/sounds/" + seName)) {
                              ctx.audio->PlaySE(ctx, seName, 0.8f);
                          } else {
                              // フォールバック: 新SEが見つからない場合のみ従来音を検討
                              bool isGood = (state->currentMaterial == game::components::TerrainMaterial::Fairway ||
                                             state->currentMaterial == game::components::TerrainMaterial::Green);
                              if (!isGood) ctx.audio->PlaySE(ctx, "judge_Bad.wav", 0.8f);
                          }
                      }
                  }
              }

              shot->phase = game::components::ShotState::Phase::ShowResult;
              shot->resultDisplayTime = 1.0f;
              state->canShoot = true;
          }
      }
  }

  // 判定結果表示終了とカメラ復帰
  if (shot->phase == game::components::ShotState::Phase::ShowResult) {
      shot->resultDisplayTime -= dt;
      if (shot->resultDisplayTime <= 0.0f) {
          shot->phase = game::components::ShotState::Phase::RestoringCamera;
          m_screenFade.SetCenter(0.5f, 0.5f);
          m_screenFade.FadeOut(0.4f, game::utils::FadeType::CircleWipe, {0, 0, 0});
      }
  }

  // カメラフェード復帰
  if (shot->phase == game::components::ShotState::Phase::RestoringCamera && !m_screenFade.IsFading()) {
      if (m_cameraController) m_cameraController->RestoreAfterFade(ctx);
      shot->Reset();
      if (m_hud) m_hud->ResetShotUI(ctx);
      m_screenFade.FadeIn(0.4f, game::utils::FadeType::CircleWipe, {0, 0, 0});
  }

  // 予測軌道アップデート (Idle 時も表示: クラブ切り替えやパワーレビューのため)
  bool canShowTrajectory = m_trajectoryPredictor && state->canShoot && !isMapView;
  if (canShowTrajectory &&
      (shot->phase == game::components::ShotState::Phase::Idle ||
       shot->phase == game::components::ShotState::Phase::PowerCharging ||
       shot->phase == game::components::ShotState::Phase::ImpactTiming)) {
      game::controllers::TrajectoryPredictor::Params tParams;
      tParams.ballEntity    = m_ballEntity;
      tParams.arrowEntity   = m_arrowEntity;
      tParams.shotDirection = m_cameraController ? m_cameraController->GetShotDirection() : DirectX::XMFLOAT3(0,0,1);
      tParams.maxPower      = m_clubController ? m_clubController->GetCurrentClub().maxPower : 30.0f;
      tParams.launchAngle   = m_clubController ? m_clubController->GetCurrentClub().launchAngle : 30.0f;
      tParams.isMapView     = false;
      tParams.terrainSystem = m_terrainSystem.get();

      float powerRatio = 0.0f; // Idle 時は 0 (TrajectoryPredictor 内でデフォルト比率を使用)
      if (shot->phase == game::components::ShotState::Phase::PowerCharging) {
          powerRatio = shot->powerGaugePos;
      } else if (shot->phase == game::components::ShotState::Phase::ImpactTiming) {
          powerRatio = (shot->confirmedPower > 0.0f) ? shot->confirmedPower : shot->powerGaugePos;
      } else if (shot->confirmedPower > 0.0f) {
          powerRatio = shot->confirmedPower;
      }

      tParams.powerRatio = std::clamp(powerRatio, 0.0f, 1.0f);
      m_trajectoryPredictor->Update(ctx, tParams);
  } else if (m_trajectoryPredictor) {
      m_trajectoryPredictor->Hide(ctx);
  }

  // ガイド矢印
  if (m_guideArrowEntity != UINT32_MAX) {
      auto* guideMR = ctx.world.Get<game::components::MeshRenderer>(m_guideArrowEntity);
      auto* guideT = ctx.world.Get<game::components::Transform>(m_guideArrowEntity);
      auto* ballT = ctx.world.Get<game::components::Transform>(m_ballEntity);
      if (guideMR && guideT && ballT) {
          if (shot->phase == game::components::ShotState::Phase::Idle && state->canShoot && !isMapView) {
              guideMR->isVisible = true;
              guideT->position = ballT->position;
              DirectX::XMFLOAT3 shotDir = m_cameraController ? m_cameraController->GetShotDirection() : DirectX::XMFLOAT3(0,0,1);
              float yaw = std::atan2(shotDir.x, shotDir.z);
              DirectX::XMStoreFloat4(&guideT->rotation, DirectX::XMQuaternionRotationRollPitchYaw(0, yaw, 0));
          } else {
              guideMR->isVisible = false;
          }
      }
  }

  // HUD 更新
  if (m_hud) {
      float currentPower = shot->powerGaugePos;
      if (shot->phase == game::components::ShotState::Phase::ImpactTiming || shot->confirmedPower > 0.0f) {
          currentPower = shot->confirmedPower;
      }
      float currentImpact = (shot->phase == game::components::ShotState::Phase::ImpactTiming) ? shot->impactGaugePos : 0.5f;

      // クラブ情報リストを構築して渡す
      std::vector<game::controllers::ClubUIData> clubDataList;
      int clubIdx = 0;
      if (m_clubController) {
          const auto& clubs = m_clubController->GetAllClubs();
          for (const auto& c : clubs) {
              clubDataList.push_back({c.name, c.iconTexture, c.shortName, c.categoryEN});
          }
          clubIdx = m_clubController->GetCurrentClubIndex();
      }

      // ターゲット距離と高低差を計算
      float distanceToTarget = 0.0f;
      float heightDiff = 0.0f;
      if (!state->holes.empty()) {
          auto targetHoleEntity = state->holes[0];
          auto* holeT = ctx.world.Get<game::components::Transform>(targetHoleEntity);
          auto* ballT = ctx.world.Get<game::components::Transform>(m_ballEntity);
          if (holeT && ballT) {
              float dx = holeT->position.x - ballT->position.x;
              float dz = holeT->position.z - ballT->position.z;
              distanceToTarget = std::sqrt(dx * dx + dz * dz);
              heightDiff = holeT->position.y - ballT->position.y;
          }
      }

      m_hud->Update(ctx, dt, *state,
                    currentPower, shot->confirmedPower,
                    state->windSpeed, state->windDirection,
                    m_cameraController ? m_cameraController->GetYaw() : 0.0f,
                    clubDataList, clubIdx,
                    distanceToTarget, heightDiff);
      
      // HUDへのパワーゲージ更新
      if (shot->phase == game::components::ShotState::Phase::PowerCharging ||
          shot->phase == game::components::ShotState::Phase::ImpactTiming) {
          m_hud->UpdatePowerGauge(ctx, currentPower, currentImpact, 0.0f, 1.0f);
      }

      // 通常時 <-> ショット時 UI 切り替え
      const bool isShotPhase = (shot->phase != game::components::ShotState::Phase::Idle &&
                                shot->phase != game::components::ShotState::Phase::ShowResult &&
                                shot->phase != game::components::ShotState::Phase::RestoringCamera);
      m_hud->SetShotPhaseUIVisible(ctx, isShotPhase);
  }

  if (m_gameJuice) {
      m_gameJuice->Update(ctx, m_cameraEntity, m_ballEntity);
  }

  // 地形判定UIの更新
  if (m_terrainDisplayTimer > 0.0f) {
      m_terrainDisplayTimer -= dt;
      if (m_terrainImageEntity != UINT32_MAX) {
          auto* ui = ctx.world.Get<game::components::UIImage>(m_terrainImageEntity);
          if (ui) {
              if (m_terrainDisplayTimer <= 0.0f) {
                  ui->visible = false;
              } else {
                  // 最初はズームイン、最後はフェードアウトするアニメーション
                  float lifeTime = 2.0f - m_terrainDisplayTimer;
                  if (lifeTime < 0.2f) {
                      // ズームイン
                      float t = lifeTime / 0.2f;
                      ui->width = 512.0f * t;
                      ui->height = 256.0f * t;
                      ui->alpha = t;
                  } else if (m_terrainDisplayTimer < 0.5f) {
                      // フェードアウト
                      ui->alpha = m_terrainDisplayTimer / 0.5f;
                      ui->width = 512.0f;
                      ui->height = 256.0f;
                  } else {
                      ui->alpha = 1.0f;
                      ui->width = 512.0f;
                      ui->height = 256.0f;
                  }
                  ui->x = (1280.0f - ui->width) * 0.5f;
                  ui->y = (720.0f - ui->height) * 0.5f;
              }
          }
      }
  }
}

/**
 * @brief シーンの描画処理を行います。
 */
void WikiGolfScene::Render(core::GameContext &ctx) {
  m_screenFade.Render(ctx);
}

/**
 * @brief カップイン判定を行います。
 * @return 遷移が発生した場合はtrue
 */
bool WikiGolfScene::CheckCupIn(core::GameContext &ctx) {
  auto *rb = ctx.world.Get<RigidBody>(m_ballEntity);
  auto *t = ctx.world.Get<Transform>(m_ballEntity);
  if (!rb || !t)
    return false;

  float speedSq = rb->velocity.x * rb->velocity.x +
                  rb->velocity.y * rb->velocity.y +
                  rb->velocity.z * rb->velocity.z;

  // カップ判定
  auto *state = ctx.world.GetGlobal<GolfGameState>();
  if (!state)
    return false;

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

      // 遷移前に地形判定UIを非表示にする（次のコースに持ち越さない）
      if (m_terrainImageEntity != UINT32_MAX) {
          auto* ui = ctx.world.Get<game::components::UIImage>(m_terrainImageEntity);
          if (ui) ui->visible = false;
      }
      m_terrainDisplayTimer = 0.0f;

      // カップイン時間進行 (1時間)
      m_timeOfDay.OnCupIn(1.0f);

      // ホールインワン演出の実行
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
        
        // ホールインワン判定（1打目でターゲット到達）
        if (hole->isTarget && state->shotCount == 1) {
            ctx.audio->PlaySE(ctx, "se_holeInOne.mp3", 1.0f);
        } else if (hole->isTarget) {
            ctx.audio->PlaySE(ctx, "se_shot_hard.mp3", 0.8f); // ターゲット到達音
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
          ctx.audio->PlaySE(ctx, "se_goal.mp3", 1.0f);
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
        return true;
      }

      TransitionToPage(ctx, hole->linkTarget);
      return true; // 1フレームに1回だけ遷移
    }
  }
  return false;
}

} // namespace game::scenes
