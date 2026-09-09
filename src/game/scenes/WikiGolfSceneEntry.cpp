/**
 * @file WikiGolfSceneEntry.cpp
 * @brief WikiGolfシーンの初期化処理を実装します。
*/

#include "WikiGolfScene.h"
#include "WikiGolfSceneSupport.h"
#include "../../audio/AudioSystem.h"
#include "../../core/GameContext.h"
#include "../../core/Input.h"
#include "../../core/Logger.h"
#include "../../core/SceneManager.h"
#include "../../ecs/World.h"
#include "../../graphics/GraphicsDevice.h"
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
#include "../controllers/MinimapController.h"
#include "../systems/PhysicsSystem.h"
#include "../systems/SkyboxRenderSystem.h"
#include "../systems/WikiClient.h"
#include "../utils/GameplayPhysicsConstants.h"
#include "../utils/ProceduralFlag.h"
#include "../utils/PageHistoryUtils.h"
#include "LoadingScene.h"
#include "TitleScene.h"
#include <DirectXMath.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>

#undef min
#undef max

namespace game::scenes {

using namespace DirectX;
using namespace game::components;

namespace {

const char *EntityAliveLabel(const ecs::World &world, ecs::Entity entity) {
  if (world.IsAlive(entity)) {
    return "true";
  }
  return "false";
}

} // namespace

/**
 * @brief シーンに侵入した際の初期化処理を行います。
*/
void WikiGolfScene::OnEnter(core::GameContext &ctx) {
  LOG_INFO("WikiGolf", "OnEnter");

  m_tutorialCupInFired = false;
  m_tutorialFlagSampleEntities.clear();

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
  scene_detail::PreloadGameplayResources(ctx);

  // 地形システムの初期化
  m_terrainSystem = std::make_unique<game::systems::WikiTerrainSystem>();

  // ページローダーの初期化
  m_pageLoader = std::make_unique<WikiPageLoader>();

  if (m_isTutorial) {
    // チュートリアルオーバーレイはロード演出（Transitioning）が終わってから
    // 初期化する。ここでは固定データのみセットする。
    auto *globalData = ctx.world.GetGlobal<game::components::WikiGlobalData>();
    if (!globalData) {
      ctx.world.SetGlobal(game::components::WikiGlobalData{});
      globalData = ctx.world.GetGlobal<game::components::WikiGlobalData>();
    }

    globalData->startPage = "チュートリアル";
    globalData->targetPage = "ゴール";
    globalData->targetPageId = -1;

    std::vector<game::WikiLink> links;
    links.push_back({"フェアウェイ", "フェアウェイ"});
    links.push_back({"ラフ", "ラフ"});
    links.push_back({"バンカー", "バンカー"});
    links.push_back({"グリーン", "グリーン"});
    links.push_back({"ウォーターハザード", "ウォーターハザード"});
    links.push_back({"ゴール", "ゴール"});
    globalData->cachedLinks = links;
    globalData->cachedExtract =
        "チュートリアルへようこそ。フェアウェイ、ラフ、バンカー、"
        "グリーン、ウォーターハザードの違いを確認しながら、"
        "最後はゴールへカップインしましょう。";
    globalData->hasCachedData = true;
  }

  // フィールドの初期化
  CreateField(ctx);

  // 各種エンティティの初期化
  m_cameraEntity = CreateEntity(ctx.world);
  auto &t = ctx.world.Add<Transform>(m_cameraEntity);
  t.position = {0.0f, 15.0f * scene_detail::kFieldScale,
                -15.0f * scene_detail::kFieldScale};

  auto &camComp = ctx.world.Add<Camera>(m_cameraEntity);
  camComp.fov = XMConvertToRadians(60.0f);
  camComp.aspectRatio = ctx.graphics.GetAspectRatio();
  camComp.nearZ = 0.1f;
  camComp.farZ = 750.0f;

  m_arrowEntity = CreateEntity(ctx.world);
  auto &at = ctx.world.Add<Transform>(m_arrowEntity);
  at.scale = {0.0f, 0.0f, 0.0f};

  // 方向ガイドセグメント（流れる矢印）の作成
  // kGuideSegCount 個の cube を前方に並べ、アニメーションで流れるように見せる
  static constexpr int kGuideSegCount = 7;
  m_guideSegments.clear();
  m_guideSegments.reserve(kGuideSegCount);
  for (int gi = 0; gi < kGuideSegCount; ++gi) {
    auto ge = CreateEntity(ctx.world);
    auto &gt_ = ctx.world.Add<Transform>(ge);
    gt_.scale = {0.12f, 0.12f, 0.45f};

    auto &gmr = ctx.world.Add<game::components::MeshRenderer>(ge);
    gmr.mesh   = ctx.resource.LoadMesh("builtin/cube");
    gmr.shader = ctx.resource.LoadShader("Basic",
                   L"Assets/shaders/BasicVS.hlsl",
                   L"Assets/shaders/BasicPS.hlsl");
    gmr.color = {0.55f, 0.90f, 1.00f, 0.60f}; // 初期色はアニメーションで上書きされる
    gmr.isTransparent = true;
    gmr.isVisible = false;
    m_guideSegments.push_back(ge);
  }

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
  // ポストプロセス(霧/色調補正/ビネット/ブルーム)はctx.postProcessとしてグローバルに
  // 保持されており、記事ロード時にWikiPageLoaderがUpdateFromEnvironment()で更新する。
  m_timeOfDay.Initialize(8.0f); // 朝8時スタート

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

  // スイング判定UIの初期化（着地地形とは別エンティティ）
  if (m_judgeImageEntity == UINT32_MAX) {
      m_judgeImageEntity = CreateEntity(ctx.world);
      auto& ui = ctx.world.Add<game::components::UIImage>(m_judgeImageEntity);
      ui.texturePath = "";
      ui.x = 1280.0f * 0.5f;
      ui.y = 720.0f * 0.5f;
      ui.width = 0.0f;
      ui.height = 0.0f;
      ui.visible = false;
      ui.layer = 131; // 地形判定UIより手前
      ui.alpha = 0.0f;
  }

  // 着地後の待機時間短縮（倍速）インジケーターUIの初期化
  if (m_fastForwardIndicatorEntity == UINT32_MAX) {
      m_fastForwardIndicatorEntity = CreateEntity(ctx.world);
      ctx.world.Add<game::components::UIImage>(m_fastForwardIndicatorEntity);
  }
  m_fastForwardIndicator.Initialize(ctx, m_fastForwardIndicatorEntity);
  m_fastForwardTimer.Reset();

  std::string targetPage;
  int targetId = -1;
  bool isUserOverride = false;
  constexpr int kTargetMinIncomingLinks = 10000;
  constexpr int kFallbackTargetMinIncomingLinks = 5000;

  game::components::WikiGlobalData *preloadedData =
      ctx.world.GetGlobal<game::components::WikiGlobalData>();
  std::string startPage;

  const bool hasPreloadedStartupData =
      preloadedData &&
      (preloadedData->pathSystem || (m_isTutorial && preloadedData->hasCachedData));

  if (hasPreloadedStartupData) {
    LOG_INFO("WikiGolf", "Using preloaded data. Start: {}, Target: {}",
             preloadedData->startPage, preloadedData->targetPage);
    if (preloadedData->pathSystem) {
      m_shortestPath = std::move(preloadedData->pathSystem);
    }
    startPage = preloadedData->startPage;
    targetPage = preloadedData->targetPage;
    targetId = preloadedData->targetPageId;
    isUserOverride = preloadedData->isUserOverride;

    if (preloadedData->hasCachedData) {
      LOG_INFO("WikiGolf",
               "Found cached page data. Skipping initial network request.");
      m_pageLoader->SetPreloadedData(preloadedData->cachedLinks, preloadedData->cachedExtract);
    }

    if (!preloadedData->targetThumbnailPixelsBGRA.empty()) {
      m_pageLoader->SetTargetThumbnail(ctx, preloadedData->targetThumbnailPixelsBGRA,
                                       preloadedData->targetThumbnailWidth,
                                       preloadedData->targetThumbnailHeight);
    }

    game::components::WikiGlobalData consumedData;
    ctx.world.SetGlobal(std::move(consumedData));
    LOG_INFO("WikiGolf", "Consumed preloaded WikiGlobalData and reset startup state");
  } else if (!m_isTutorial) {
    // チュートリアル時は固定データを使用するため、DB/API同期ロードをスキップする。
    // 通常ゲームのみここで同期フォールバックを実行する。
    LOG_INFO("WikiGolf", "No preloaded data found or pathSystem invalid. "
                         "Falling back to sync load.");

    game::systems::WikiClient wikiClient;
    startPage = wikiClient.FetchRandomPageTitle();

    if (!m_shortestPath) {
      m_shortestPath = std::make_unique<game::systems::WikiShortestPath>();
      if (!m_shortestPath->Initialize("Assets/data/jawiki_sdow-001.sqlite")) {
        LOG_WARN("WikiGolf", "SDOW DB not found for target selection");
        m_shortestPath.reset();
      }
    }

    if (m_shortestPath && m_shortestPath->IsAvailable()) {
      auto result =
          m_shortestPath->FetchPopularPageTitle(kTargetMinIncomingLinks);
      targetPage = result.first;
      targetId = result.second;

      if (targetPage.empty()) {
        result = m_shortestPath->FetchPopularPageTitle(
            kFallbackTargetMinIncomingLinks);
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

    // 目的記事の代表サムネイルを取得（ゴール看板表示用、追加1リクエストのみ）
    if (!targetPage.empty()) {
      std::string thumbUrl = wikiClient.FetchPageThumbnail(targetPage, 256);
      if (!thumbUrl.empty()) {
        std::string thumbBytes = wikiClient.DownloadBinary(thumbUrl);
        std::vector<uint8_t> pixels;
        uint32_t thumbW = 0, thumbH = 0;
        if (!thumbBytes.empty() &&
            graphics::DecodeWikiImageFromMemory(thumbBytes, pixels, thumbW, thumbH)) {
          m_pageLoader->SetTargetThumbnail(ctx, pixels, thumbW, thumbH);
        }
      }
    }
  }

  if (targetId == -1 && m_shortestPath && m_shortestPath->IsAvailable() &&
      !targetPage.empty()) {
    targetId = m_shortestPath->ResolvePageId(targetPage);
    if (targetId != -1) {
      LOG_INFO("WikiGolf", "Resolved target page ID: {} -> {}", targetPage,
               targetId);
    } else {
      LOG_WARN("WikiGolf", "Failed to resolve target page ID: {}",
               targetPage);
    }
  }

  if (!isUserOverride && m_shortestPath && m_shortestPath->IsAvailable() && !startPage.empty() &&
      !targetPage.empty()) {
    constexpr int kStartPagePathCheckMaxDepth = 4;
    const int maxRetry = 5;
    for (int attempt = 0; attempt < maxRetry; ++attempt) {
      game::systems::ShortestPathResult pathResult;
      if (targetId != -1) {
        pathResult = m_shortestPath->FindShortestPath(
            startPage, targetId, kStartPagePathCheckMaxDepth);
      } else {
        pathResult = m_shortestPath->FindShortestPath(
            startPage, targetPage, kStartPagePathCheckMaxDepth);
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

      auto newTarget =
          m_shortestPath->FetchPopularPageTitle(kTargetMinIncomingLinks);
      if (newTarget.first.empty()) {
        newTarget = m_shortestPath->FetchPopularPageTitle(
            kFallbackTargetMinIncomingLinks);
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
            EntityAliveLabel(ctx.world, m_cameraEntity));

  SpawnBall(ctx);
  LOG_DEBUG("WikiGolf", "After SpawnBall: Cam Alive={}",
            EntityAliveLabel(ctx.world, m_cameraEntity));


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
            EntityAliveLabel(ctx.world, m_cameraEntity));

  m_pageLoader->SetSystems(m_textureGenerator.get(), m_terrainSystem.get(), m_skyboxGenerator.get(), m_shortestPath.get());
  m_pageLoader->SetTutorialMode(m_isTutorial);
  m_terrainSystem->SetTutorialMode(m_isTutorial);

  // コントローラの初期化
  m_cameraController = std::make_unique<game::controllers::CameraController>();
  game::controllers::CameraController::Config camCfg;
  camCfg.cameraEntity = m_cameraEntity;
  camCfg.ballEntity = m_ballEntity;
  camCfg.floorEntity = m_floorEntity;
  camCfg.fieldScale = scene_detail::kFieldScale;
  camCfg.terrain = m_terrainSystem.get();
  camCfg.gameJuice = m_gameJuice.get();
  m_cameraController->Initialize(camCfg);

  m_minimapController = std::make_unique<game::controllers::MinimapController>();
  game::controllers::MinimapController::Config miniCfg;
  miniCfg.cameraEntity = m_cameraEntity;
  miniCfg.ballEntity = m_ballEntity;
  miniCfg.fieldScale = scene_detail::kFieldScale;
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
      // ロード中は地球儀のみ表示するためHUD/ミニマップを非表示
      if (m_hud) m_hud->SetVisible(ctx, false);
      if (m_minimapController) m_minimapController->SetVisible(ctx, false);
      // 方向ガイドセグメントを非表示
      for (auto segE : m_guideSegments) {
        if (auto* mr = ctx.world.Get<MeshRenderer>(segE)) mr->isVisible = false;
      }
      m_transitionController->StartTransition(ctx, startPage, m_pageLoader.get(), m_ballEntity, m_cameraEntity, m_skyboxEntity, m_minimapController.get());
  }
  LOG_DEBUG("WikiGolf", "After LoadPage: Cam Alive={}",
            EntityAliveLabel(ctx.world, m_cameraEntity));
}


} // namespace game::scenes
