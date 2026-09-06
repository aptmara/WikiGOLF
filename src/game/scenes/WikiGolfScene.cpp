#include "WikiGolfScene.h"
#include "../../core/Profiler.h"
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
#include "../utils/GameplayPhysicsConstants.h"
#include "../utils/TrajectorySimulation.h"
#include "../utils/PageHistoryUtils.h"
#include "../utils/UIConstants.h"
#include "../utils/ProceduralFlag.h"
#include "PauseScene.h"
#include "CupInUtils.h"
#include "ResultScene.h"
#include "TitleScene.h"
#include "LoadingScene.h"
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

/**
 * @brief インゲーム中に頻出する描画リソースを事前にキャッシュへ載せます。
 * @author 山内陽
 */
void PreloadGameplayResources(core::GameContext& ctx) {
  ctx.resource.LoadMesh("builtin/cube");
  ctx.resource.LoadMesh("builtin/sphere");
  ctx.resource.LoadMesh("builtin/cylinder");
  ctx.resource.LoadMesh("builtin/quad");
  ctx.resource.LoadShader("Basic", L"Assets/shaders/BasicVS.hlsl",
                          L"Assets/shaders/BasicPS.hlsl");
  ctx.resource.LoadShader("Particle", L"shaders/ParticleVS.hlsl",
                          L"shaders/ParticlePS.hlsl");
  ctx.resource.LoadShader("Terrain", L"Assets/shaders/TerrainVS.hlsl",
                          L"Assets/shaders/TerrainPS.hlsl");
}
} // namespace

WikiGolfScene::WikiGolfScene(bool isTutorial) : m_isTutorial(isTutorial) {}
WikiGolfScene::~WikiGolfScene() = default;

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
  PreloadGameplayResources(ctx);

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
  t.position = {0.0f, 15.0f * kFieldScale, -15.0f * kFieldScale};

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
  m_pageLoader->SetTutorialMode(m_isTutorial);
  m_terrainSystem->SetTutorialMode(m_isTutorial);

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
            ctx.world.IsAlive(m_cameraEntity) ? "true" : "false");
}

/**
 * @brief プロシージャル旗のなびきと旗粒子を更新します。
 * @author 山内陽
 */
void WikiGolfScene::UpdateProceduralFlagEffects(core::GameContext &ctx,
                                                float dt) {
  m_flagEffectTimer += dt;
}

/**
 * @brief トップビューの着弾点プレビューを、現在選択中クラブのフルスイングで
 * 計算し直し、MinimapControllerへ反映します。
 * @details 実際の地形・風を考慮した弾道シミュレーション(TrajectoryPredictorと
 * 同じ物理ロジック)を使うため、着弾点は実際のショット結果と整合する。
 * クラブ切り替えはマップビュー中は行えないため、マップビューに入った
 * 瞬間に1回計算すれば十分。
 */
void WikiGolfScene::RefreshLandingPreview(core::GameContext &ctx) {
  if (!m_minimapController) return;

  if (!m_clubController || !ctx.world.IsAlive(m_ballEntity)) {
    m_minimapController->SetLandingPreview(ctx, {0, 0, 0}, 0.0f, false);
    return;
  }

  const auto &club = m_clubController->GetCurrentClub();
  auto *ballT = ctx.world.Get<game::components::Transform>(m_ballEntity);
  if (!ballT || club.baseCarryDistance <= 0.0f) {
    m_minimapController->SetLandingPreview(ctx, {0, 0, 0}, 0.0f, false);
    return;
  }

  const DirectX::XMFLOAT3 shotDir =
      m_cameraController ? m_cameraController->GetShotDirection()
                         : DirectX::XMFLOAT3(0, 0, 1);

  game::physics::BallPhysicsParams ballParams;
  ballParams.rollingFrictionScale = club.rollingFrictionScale;

  game::physics::WindParams wind;
  if (auto *golfState =
          ctx.world.GetGlobal<game::components::GolfGameState>()) {
    wind.windSpeed = golfState->windSpeed;
    wind.windDirection = golfState->windDirection;
  }

  game::physics::FlatGroundParams flatGroundUnused; // enabled=false: 実地形を使う

  const auto result = game::physics::SimulateCarryDistance(
      club.maxPower, club.launchAngle, shotDir, ballT->position,
      m_terrainSystem.get(), flatGroundUnused, ballParams, wind);

  // ばらつき範囲は基準飛距離の一定割合(ミート精度による左右・距離ブレの
  // ざっくりした目安)とする。
  const float dispersionRadius = club.baseCarryDistance * 0.06f;
  m_minimapController->SetLandingPreview(ctx, result.landingPosition,
                                         dispersionRadius, true);
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
 * @brief チュートリアルの旗色解説用に、一時的な実旗モデルを配置します。
 * @details ホールやコライダーは付けず、描画専用にしてショット判定へ干渉させません。
 * @author 山内陽
 */
void WikiGolfScene::CreateTutorialFlagSamples(core::GameContext &ctx) {
  ClearTutorialFlagSamples(ctx);

  struct FlagSample {
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT4 color;
  };

  const FlagSample samples[] = {
      {{-37.5f, 0.0f, 30.0f}, {1.0f, 0.2f, 0.2f, 1.0f}},   // 目的地
      {{-22.5f, 0.0f, 30.0f}, {1.0f, 0.85f, 0.0f, 1.0f}},  // 1リンク
      {{-7.5f, 0.0f, 30.0f}, {1.0f, 0.6f, 0.2f, 1.0f}},    // 2リンク
      {{7.5f, 0.0f, 30.0f}, {0.95f, 0.95f, 0.95f, 1.0f}},  // 3〜5リンク
      {{22.5f, 0.0f, 30.0f}, {0.6f, 0.6f, 0.6f, 1.0f}},    // 6リンク以上
      {{37.5f, 0.0f, 30.0f}, {0.25f, 0.65f, 1.0f, 1.0f}},  // 未解析
  };

  constexpr size_t sampleCount = sizeof(samples) / sizeof(samples[0]);
  m_tutorialFlagSampleEntities.reserve(sampleCount * 10);
  for (size_t i = 0; i < sampleCount; ++i) {
    const auto &sample = samples[i];
    float terrainH = 0.0f;
    if (m_terrainSystem) {
      terrainH = m_terrainSystem->GetHeight(sample.position.x, sample.position.z);
    }

    game::utils::ProceduralFlagOptions options;
    options.holeEntity = UINT32_MAX;
    options.large = (i == 0);
    options.createParticles = (i <= 1);
    options.animationWeight = (i == 0) ? 1.0f : 0.72f;
    auto result = game::utils::CreateProceduralFlag(
        ctx, {sample.position.x, terrainH + 0.05f, sample.position.z},
        sample.color, options);
    for (auto entity : result.allEntities) {
      m_tutorialFlagSampleEntities.push_back(entity);
    }
  }
}

/**
 * @brief チュートリアルの旗色解説用に配置した一時旗モデルを破棄します。
 * @author 山内陽
 */
void WikiGolfScene::ClearTutorialFlagSamples(core::GameContext &ctx) {
  for (auto entity : m_tutorialFlagSampleEntities) {
    if (ctx.world.IsAlive(entity)) {
      ctx.world.DestroyEntity(entity);
    }
  }
  m_tutorialFlagSampleEntities.clear();
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
  t.position = {0.0f, game::physics::kBallRadius,
                -8.0f * kFieldScale};

  auto ballMeshHandle = ctx.resource.LoadMesh("Assets/models/golfball.glb");

  // golfball.glb と builtin/sphere ではメッシュの内部スケールが異なるため、
  // 決め打ちの係数ではなく、ロード済みメッシュの実際のバウンディング半径から
  // 見た目の半径(kBallVisualScale基準)に必要な Transform.scale を逆算する。
  const float ballVisualRadius = game::physics::kBallVisualScale * 0.5f;
  float ballGlbScale = ballVisualRadius; // メッシュ取得に失敗した場合の保険
  float debugNativeRadius = -1.0f;
  DirectX::XMFLOAT3 debugBoundsCenter = {0.0f, 0.0f, 0.0f};
  if (auto *ballMesh = ctx.resource.GetMesh(ballMeshHandle)) {
    debugNativeRadius = ballMesh->GetBounds().Radius;
    debugBoundsCenter = ballMesh->GetBounds().Center;
    if (debugNativeRadius > 0.0001f) {
      ballGlbScale = ballVisualRadius / debugNativeRadius;
    }
  }
  t.scale = {ballGlbScale, ballGlbScale, ballGlbScale};
  LOG_INFO("WikiGolf",
           "Ball spawned at: ({}, {}, {}) scale={} nativeRadius={} "
           "boundsCenter=({}, {}, {})",
           t.position.x, t.position.y, t.position.z, ballGlbScale,
           debugNativeRadius, debugBoundsCenter.x, debugBoundsCenter.y,
           debugBoundsCenter.z);

  auto &mr = ctx.world.Add<MeshRenderer>(m_ballEntity);
  mr.mesh = ballMeshHandle;
  // ディンプルを強調した専用ピクセルシェーダーを使う（BasicPS.hlsl 共有だと
  // ロード画面の演出用ボールや建物にも強調が及んでしまうため分離している）。
  // 注意: RenderSystem.cpp 側のインスタンス化対応シェーダー一覧にも
  // "GolfBall" を登録しておく必要がある（そうしないと非インスタンス経路に
  // 落ちて実質描画されない）。
  mr.shader = ctx.resource.LoadShader("GolfBall", L"Assets/shaders/BasicVS.hlsl",
                                      L"Assets/shaders/GolfBallPS.hlsl");
  mr.color = {1.0f, 1.0f, 1.0f, 1.0f};
  mr.normalMapSRV = ctx.resource.LoadTextureSRV("Assets/models/golfball_n.png");
  mr.hasNormalMap = static_cast<bool>(mr.normalMapSRV);

  auto &rb = ctx.world.Add<RigidBody>(m_ballEntity);
  rb.isStatic = false;
  rb.mass = 0.0459f;      // 規定質量 45.9g
  rb.restitution = 0.35f; // 反発係数 (現実の芝との衝突)
  rb.drag = 0.30f;        // 空気抵抗係数 (Cd値)
  rb.rollingFriction = 0.25f; // 転がり抵抗を設定
  rb.velocity = {0, 0, 0};

  auto &c = ctx.world.Add<Collider>(m_ballEntity);
  c.type = ColliderType::Sphere;
  c.radius = game::physics::kBallRadius;

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
  m_tutorialCupInFired = false;

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
    // ロード中は地球儀のみ表示するためHUD/ミニマップを非表示
    if (m_hud) m_hud->SetVisible(ctx, false);
    if (m_minimapController) m_minimapController->SetVisible(ctx, false);
    // 方向ガイドセグメントを非表示
    for (auto segE : m_guideSegments) {
      if (auto* mr = ctx.world.Get<MeshRenderer>(segE)) mr->isVisible = false;
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

bool WikiGolfScene::CanReturnToPreviousPage(core::GameContext &ctx) const {
  if (m_isTutorial || m_phase != ScenePhase::Playing) {
    return false;
  }

  const auto* state = ctx.world.GetGlobal<GolfGameState>();
  return state && state->pathHistory.size() >= 2;
}

bool WikiGolfScene::ReturnToPreviousPage(core::GameContext &ctx) {
  if (!CanReturnToPreviousPage(ctx)) {
    return false;
  }

  auto* state = ctx.world.GetGlobal<GolfGameState>();
  if (!state) {
    return false;
  }

  const auto previousPage =
      game::utils::ConsumePreviousPage(state->pathHistory);
  if (!previousPage.has_value()) {
    return false;
  }

  LOG_INFO("WikiGolf", "Returning to previous page: {}",
           previousPage.value());
  TransitionToPage(ctx, previousPage.value());
  return true;
}

void WikiGolfScene::OpenPauseScene(core::GameContext &ctx) {
  if (!ctx.sceneManager || m_phase != ScenePhase::Playing) {
    return;
  }

  ctx.sceneManager->PushScene(std::make_unique<PauseScene>(
      CanReturnToPreviousPage(ctx),
      [this](core::GameContext& innerCtx) { this->ReturnToPreviousPage(innerCtx); }));
}

/**
 * @brief シーンを抜ける際の後処理を行います。
 */
void WikiGolfScene::OnExit(core::GameContext &ctx) {
  if (m_pageLoader) {
    m_pageLoader->CancelAsyncPathEvaluations();
  }

  // ゲーム終了（タイトルへ戻る等）時に、ターゲット記事単位の最短経路
  // プロセス内キャッシュ（逆方向BFS到達済みノード・確定済み距離）を破棄する。
  // 次のゲームで別のターゲットに切り替わっても自動的に破棄されるが、
  // 明示的に後始末しておく。
  game::systems::WikiShortestPath::ClearProcessCaches();

  // Fix3: シーン離脱時にゲーム中BGMを停止する（タイトルへ戻った際の鳴り続け防止）
  if (ctx.audio) {
    ctx.audio->StopBGM();
  }

  if (m_tutorialOverlay) {
    m_tutorialOverlay->Shutdown(ctx);
    m_tutorialOverlay.reset();
  }

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

  ClearTutorialFlagSamples(ctx);

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
  PROFILE_SCOPE("WikiGolf.Update");
  const float dt = ctx.dt;
  m_screenFade.Update(dt);

  auto *state = ctx.world.GetGlobal<game::components::GolfGameState>();
  auto *shot = ctx.world.GetGlobal<game::components::ShotState>();
  if (!state || !shot) return;

  if (m_pageLoader) {
      PROFILE_SCOPE("WikiGolf.PageLoaderAsync");
      m_pageLoader->UpdateAsyncPathEvaluation(ctx);

      if (m_phase == ScenePhase::Playing) {
          if (auto* ballT = ctx.world.Get<game::components::Transform>(m_ballEntity)) {
              m_pageLoader->UpdateNearbyHoleSignboards(ctx, ballT->position, m_cameraEntity);
          }
      }
  }

  if (m_phase == ScenePhase::Transitioning && m_transitionController) {
      PROFILE_SCOPE("WikiGolf.Transition");
      bool finished = m_transitionController->Update(ctx);
      if (finished) {
          m_phase = ScenePhase::Playing;
          m_prevTutorialInputLocked = false;
          // ロード完了: HUD/ミニマップを再表示する
          if (m_hud) m_hud->SetVisible(ctx, true);
          if (m_minimapController) m_minimapController->SetVisible(ctx, true);
          if (m_cameraController) m_cameraController->Update(ctx);
          // チュートリアルオーバーレイはロード演出完了後に初期化する
          // （ロード中にUIが重なって表示されるのを防ぐ）
          if (m_isTutorial && !m_tutorialOverlay) {
              m_tutorialOverlay = std::make_unique<game::controllers::TutorialOverlayController>();
              m_tutorialOverlay->Initialize(ctx);
              CreateTutorialFlagSamples(ctx);

              std::vector<game::controllers::TutorialOverlayController::EventCameraTarget> targets = {
                  { {0.0f, 15.0f, -25.0f}, {0.0f, 0.0f, -10.0f}, L"Fairway (フェアウェイ)", L"ボールが転がりやすい標準的な地形です。" },
                  { {-15.0f, 15.0f, -15.0f}, {-15.0f, 0.0f, 0.0f}, L"Rough (ラフ)", L"草が深く、ボールの転がりが少し悪くなります。" },
                  { {24.0f, 15.0f, -18.0f}, {24.0f, 0.0f, -3.0f}, L"Bunker (バンカー)", L"砂地です。転がりにくく、パワーも落ちやすくなります。" },
                  { {0.0f, 15.0f, 37.0f}, {0.0f, 0.0f, 52.0f}, L"Green (グリーン)", L"カップ周りの滑らかな地形です。よく転がります。" },
                  { {-24.0f, 15.0f, 5.0f}, {-24.0f, 0.0f, 20.0f}, L"OB / Water / Lava", L"水や溶岩などの危険エリア。入るとペナルティで1打戻されます。" }
              };
              std::vector<game::controllers::TutorialOverlayController::EventCameraTarget> flagTargets = {
                  { {-37.5f, 10.0f, 18.0f}, {-37.5f, 1.4f, 30.0f}, L"赤い旗 / 目的地", L"赤はターゲット記事へのホールです。ここに入れるとチュートリアルクリアです。" },
                  { {-15.0f, 11.0f, 16.0f}, {-15.0f, 1.4f, 30.0f}, L"黄・橙の旗", L"黄は目的地まで1リンク、橙は2リンク先のホールです。近道候補になります。" },
                  { {15.0f, 11.0f, 16.0f}, {15.0f, 1.4f, 30.0f}, L"白・灰・青の旗", L"白は3〜5リンク、灰は6リンク以上、青は距離が未解析です。赤に近い色ほど有利です。" }
              };
              m_tutorialOverlay->SetEventCameraTargets(m_cameraEntity,
                                                       std::move(targets),
                                                       std::move(flagTargets));
          }
      }
      return;
  }

  auto mousePos = ctx.input.GetMousePosition();
  int mouseX = mousePos.x;
  int mouseY = mousePos.y;

  bool tutorialInputLocked = false;
  if (m_isTutorial && m_tutorialOverlay) {
    PROFILE_SCOPE("WikiGolf.TutorialOverlay");
    m_tutorialOverlay->Update(ctx, m_cameraController.get(), m_clubController.get(), m_shotController.get(), m_minimapController.get());
    if (m_tutorialOverlay->IsDone()) {
      // チュートリアル終了後、タイトルへ戻る処理など
      // （フラグの保存はTitleScene側か、ここで行う）
      std::string path = "save_tutorial_done.flag";
      std::ofstream ofs(path);
      ofs << "done";
      ofs.close();

      auto loadingScene = std::make_unique<LoadingScene>([]() { return std::make_unique<TitleScene>(); });
      ctx.sceneManager->ChangeScene(std::move(loadingScene));
      return;
    }
    tutorialInputLocked = m_tutorialOverlay->IsInputLocked();
  }

  // マップビュー更新
  bool isMapView = false;
  bool wasMapView = m_minimapController && m_minimapController->IsMapView();
  if (m_minimapController && !tutorialInputLocked) {
      PROFILE_SCOPE("WikiGolf.Minimap");
      float fieldW = m_pageLoader ? m_pageLoader->GetFieldWidth() : 80.0f;
      float fieldD = m_pageLoader ? m_pageLoader->GetFieldDepth() : 120.0f;
      m_minimapController->ProcessInput(ctx, mouseX, mouseY, fieldW, fieldD, m_skyboxEntity);
      DirectX::XMFLOAT3 shotDir = m_cameraController ? m_cameraController->GetShotDirection() : DirectX::XMFLOAT3(0,0,1);
      const bool mapViewNow = m_minimapController->IsMapView();
      const float minimapInterval = mapViewNow ? (1.0f / 60.0f) : (1.0f / 30.0f);
      m_minimapUpdateTimer += dt;
      if (m_minimapUpdateTimer >= minimapInterval) {
          m_minimapController->UpdateMinimap(ctx, fieldW, fieldD, shotDir);
          m_minimapUpdateTimer = 0.0f;
      }
      if (m_minimapController->IsMapView()) {
          m_minimapController->UpdateMapCamera(ctx, fieldW, fieldD);
      }
      isMapView = m_minimapController->IsMapView();
  }

  // クラブ選択パネル横: 着弾点プレビュー(トップビュー)トグルボタン
  if (m_hud && m_minimapController && !tutorialInputLocked) {
      PROFILE_SCOPE("WikiGolf.LandingPreviewButton");
      const bool btnEnabled = !isMapView && state->canShoot &&
          shot->phase == game::components::ShotState::Phase::Idle;
      const bool hovered =
          mouseX >= game::ui::kLandingPreviewBtnX &&
          mouseX <= game::ui::kLandingPreviewBtnX + game::ui::kLandingPreviewBtnW &&
          mouseY >= game::ui::kLandingPreviewBtnY &&
          mouseY <= game::ui::kLandingPreviewBtnY + game::ui::kLandingPreviewBtnH;

      if ((btnEnabled || isMapView) && hovered && ctx.input.GetMouseButtonDown(0)) {
          m_minimapController->ToggleMapView(ctx, m_skyboxEntity);
          isMapView = m_minimapController->IsMapView();
          if (isMapView) {
              RefreshLandingPreview(ctx);
          } else {
              m_minimapController->SetLandingPreview(ctx, {0, 0, 0}, 0.0f, false);
          }
      }

      m_hud->UpdateLandingPreviewButton(ctx, hovered, isMapView, btnEnabled || isMapView);
  }

  const bool escapeHandledByMapView =
      wasMapView && ctx.input.GetKeyDown(VK_ESCAPE);
  if (!tutorialInputLocked && !escapeHandledByMapView &&
      ctx.input.GetKeyDown(VK_ESCAPE)) {
    OpenPauseScene(ctx);
    return;
  }

  if (!tutorialInputLocked && !isMapView && ctx.input.GetKeyDown(VK_BACK) &&
      ReturnToPreviousPage(ctx)) {
    return;
  }

  if (m_prevTutorialInputLocked && !tutorialInputLocked && m_cameraController) {
      m_cameraController->ResetForTransition(kFieldScale);
  }
  m_prevTutorialInputLocked = tutorialInputLocked;

  // クラブ更新
  if (m_clubController && !tutorialInputLocked && !isMapView &&
      shot->phase == game::components::ShotState::Phase::Idle) {
      PROFILE_SCOPE("WikiGolf.ClubInput");
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
  if (m_cameraController && !tutorialInputLocked && !isMapView) {
      PROFILE_SCOPE("WikiGolf.Camera");
      m_cameraController->ProcessInput(ctx, mouseX, mouseY);
  }

  // ショット処理
  if (m_shotController && !tutorialInputLocked && !isMapView) {
      PROFILE_SCOPE("WikiGolf.Shot");
      auto event = m_shotController->ProcessShot(ctx, state->canShoot, m_hud.get(), m_clubController.get());
      if (event.shotFired) {
          state->canShoot = false;
          if (m_cameraController) m_cameraController->OnShotStart(ctx, shot->confirmedPower);
          if (m_clubController) {
              DirectX::XMFLOAT3 shotDir = m_cameraController ? m_cameraController->GetShotDirection() : DirectX::XMFLOAT3(0,0,1);
              m_shotController->ExecuteShot(ctx, m_ballEntity, shotDir, m_clubController->GetCurrentClub(), &m_timeOfDay, m_hud.get());
          }

          // 打球判定の演出表示（着地地形の演出とは別エンティティ・別カーブ）
          auto feedback = game::utils::BuildJudgeFeedback(shot->judgement);
          if (feedback.HasVisual() && m_judgeImageEntity != UINT32_MAX) {
              auto* ui = ctx.world.Get<game::components::UIImage>(m_judgeImageEntity);
              if (ui) {
                  ui->texturePath = feedback.texturePath;
                  ui->visible = true;
                  ui->alpha = 0.0f;
                  ui->width = 0.0f;
                  ui->height = 0.0f;
                  ui->rotation = 0.0f;
                  ui->x = 1280.0f * 0.5f;
                  ui->y = game::ui::kJudgeImageCenterY;
                  m_judgeDisplayTimer = feedback.displaySeconds;
                  m_judgeDisplayTotal  = feedback.displaySeconds;
                  m_judgeDisplayTargetW = feedback.width;
                  m_judgeDisplayTargetH = feedback.height;
                  m_judgeDisplayJudgement = shot->judgement;
              }
          }
      }
  }
  
  // クラブアニメーション更新
  if (m_clubController && !tutorialInputLocked) {
      PROFILE_SCOPE("WikiGolf.ClubAnimation");
      DirectX::XMFLOAT3 shotDir = m_cameraController ? m_cameraController->GetShotDirection() : DirectX::XMFLOAT3(0,0,1);
      m_clubController->UpdateAnimation(ctx, dt, m_ballEntity, shotDir);
  }
  
  // 物理更新
  game::systems::PhysicsSystem(ctx, dt);

  // 物理後のボール位置を使い、追従カメラの1フレーム遅延を防ぐ。
  if (m_cameraController && !tutorialInputLocked && !isMapView) {
      PROFILE_SCOPE("WikiGolf.CameraFollow");
      m_cameraController->Update(ctx);
  }
  
  // カップイン判定を地形判定の前に行う（遷移時は以降の処理をスキップ）
  if (CheckCupIn(ctx)) return;

  // ボール静止・OB・地形判定ロジック
  if (shot->phase == game::components::ShotState::Phase::Executing) {
      auto *rb = ctx.world.Get<game::components::RigidBody>(m_ballEntity);
      if (rb) {
          float speed = std::sqrt(rb->velocity.x * rb->velocity.x +
                                  rb->velocity.y * rb->velocity.y +
                                  rb->velocity.z * rb->velocity.z);
          if (speed < 0.1f && state->isBallGrounded) {
              rb->velocity = {0, 0, 0};
              std::string terrainTex = "";
              bool treatAsOB = false;
              const bool wasOB = state->isOB; // state->isOB はこの後リセットされるため先に控える

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
                  case game::components::TerrainMaterial::Ice:
                      terrainTex = GetTerrainTex("ui_terrain_fairway.png", "ui_judge_nice.png");
                      break;
                  case game::components::TerrainMaterial::Stone:
                      terrainTex = GetTerrainTex("ui_terrain_rough.png", "ui_judge_miss.png");
                      break;
                  case game::components::TerrainMaterial::Water:
                  case game::components::TerrainMaterial::Lava:
                      terrainTex = GetTerrainTex("ui_terrain_ob.png", "ui_judge_miss.png");
                      treatAsOB = true;
                      break;
                  default:
                      terrainTex = GetTerrainTex("ui_terrain_rough.png", "ui_judge_miss.png");
                      break;
                  }

                  if (treatAsOB) {
                      LOG_INFO("WikiGolf", "Out-of-bounds terrain. Material: {}", (int)state->currentMaterial);
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
                      ui->rotation = 0.0f;
                      ui->x = 1280.0f * 0.5f;
                      ui->y = 720.0f * 0.5f;
                      m_terrainDisplayTimer = 2.0f;
                      m_terrainDisplayTotal  = 2.0f;
                      m_terrainDisplayTargetW = 512.0f;
                      m_terrainDisplayTargetH = 256.0f;
                      if (wasOB || treatAsOB) {
                          m_terrainDisplayTier = TerrainResultTier::Rough;
                      } else {
                          using Material = game::components::TerrainMaterial;
                          switch (state->currentMaterial) {
                          case Material::Green:                    m_terrainDisplayTier = TerrainResultTier::Perfect; break;
                          case Material::Fairway:
                          case Material::Ice:                      m_terrainDisplayTier = TerrainResultTier::Good;    break;
                          default:                                 m_terrainDisplayTier = TerrainResultTier::Rough;   break;
                          }
                      }

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
  bool canShowTrajectory = m_trajectoryPredictor && state->canShoot &&
                           !tutorialInputLocked && !isMapView;
  if (canShowTrajectory &&
      (shot->phase == game::components::ShotState::Phase::Idle ||
       shot->phase == game::components::ShotState::Phase::PowerCharging ||
       shot->phase == game::components::ShotState::Phase::ImpactTiming)) {
      PROFILE_SCOPE("WikiGolf.Trajectory");
      game::controllers::TrajectoryPredictor::Params tParams;
      tParams.ballEntity    = m_ballEntity;
      tParams.arrowEntity   = m_arrowEntity;
      tParams.shotDirection = m_cameraController ? m_cameraController->GetShotDirection() : DirectX::XMFLOAT3(0,0,1);
      if (m_clubController) {
          const auto& currentClub = m_clubController->GetCurrentClub();
          tParams.baseCarryDistance = currentClub.baseCarryDistance;
          tParams.carryTable        = &currentClub.carryTable;
          tParams.launchAngle       = currentClub.launchAngle;
      } else {
          tParams.launchAngle = 30.0f;
      }
      tParams.isMapView     = false;
      tParams.terrainSystem = m_terrainSystem.get();

      float powerRatio = 0.0f; // Idle 時は 0 (TrajectoryPredictor 内でデフォルト比率を使用)
      if (shot->phase == game::components::ShotState::Phase::PowerCharging) {
          powerRatio = shot->powerGaugePos;
      } else if (shot->phase == game::components::ShotState::Phase::ImpactTiming) {
          // パワー決定後はゲージ値に関わらずクラブの基準飛距離(フルスイング相当)を
          // 固定表示する。実際の飛距離への反映はExecuteShot時の判定倍率で行う。
          powerRatio = 1.0f;
      } else if (shot->confirmedPower > 0.0f) {
          powerRatio = shot->confirmedPower;
      }

      tParams.powerRatio = std::clamp(powerRatio, 0.0f, 1.0f);
      m_trajectoryPredictor->Update(ctx, tParams);
  } else if (m_trajectoryPredictor) {
      m_trajectoryPredictor->Hide(ctx);
  }

  // 方向ガイド（流れる矢印アニメーション）
  {
    const bool showGuide = !m_guideSegments.empty() &&
                           (shot->phase == game::components::ShotState::Phase::Idle) &&
                           state->canShoot && !tutorialInputLocked && !isMapView;

    auto* ballT2 = ctx.world.Get<game::components::Transform>(m_ballEntity);
    if (showGuide && ballT2) {
      m_guideAnimTimer += dt;

      DirectX::XMFLOAT3 shotDir = m_cameraController
          ? m_cameraController->GetShotDirection()
          : DirectX::XMFLOAT3{0, 0, 1};
      float yaw = std::atan2(shotDir.x, shotDir.z);
      DirectX::XMVECTOR qRot = DirectX::XMQuaternionRotationRollPitchYaw(0.0f, yaw, 0.0f);

      // 各セグメントをボール前方に等間隔で並べ、色・サイズをグラデーション
      const int nSeg = static_cast<int>(m_guideSegments.size());
      const float kSpacing   = 1.05f; // セグメント間隔
      const float kScrollSpd = 2.0f;  // スクロール速度
      // アニメーションフェーズ: 0→1 で先頭に向かってスクロール
      float phase = std::fmod(m_guideAnimTimer * kScrollSpd, 1.0f);

      for (int si = 0; si < nSeg; ++si) {
        auto* segMR = ctx.world.Get<game::components::MeshRenderer>(m_guideSegments[si]);
        auto* segT  = ctx.world.Get<game::components::Transform>(m_guideSegments[si]);
        if (!segMR || !segT) continue;

        // 正規化比率：先端(si=0)＝0, 末尾＝1
        float nt = (nSeg > 1) ? (float)si / (float)(nSeg - 1) : 0.0f;

        // スクロールオフセット: 各セグメントが時間経過で前方に流れる
        float scrolledNt = std::fmod(nt + phase, 1.0f);
        float dist = 0.6f + scrolledNt * (kSpacing * (float)(nSeg - 1));

        // 送り先位置: ボールせから前方 dist m
        DirectX::XMVECTOR fwd = DirectX::XMVectorSet(shotDir.x, 0, shotDir.z, 0);
        DirectX::XMVECTOR segPosV = DirectX::XMVectorAdd(
            DirectX::XMLoadFloat3(&ballT2->position),
            DirectX::XMVectorScale(fwd, dist));
        DirectX::XMFLOAT3 segPos;
        DirectX::XMStoreFloat3(&segPos, segPosV);
        // 地面に少し浮かせる
        if (m_terrainSystem) {
          segPos.y = m_terrainSystem->GetHeight(segPos.x, segPos.z) + 0.12f;
        } else {
          segPos.y = ballT2->position.y + 0.12f;
        }
        segT->position = segPos;
        DirectX::XMStoreFloat4(&segT->rotation, qRot);

        // 先端大く(1.6x)→末尾小さく(0.5x)
        float thick = 0.18f + (0.06f - 0.18f) * nt;
        // 先端のセグメントをヒシ形に見せる（scaleXを幅広に）
        float xScale = thick * (1.0f + (1.0f - nt) * 0.8f);
        segT->scale = {xScale, thick, 0.50f};

        // 色: 先端白/水色 → 末尾シアン/透明
        // scrolledNtが0に近いほど「新生」部分なので明るく、大きく表示
        float alpha = 0.85f - scrolledNt * 0.75f;
        float r = 0.50f + (1.00f - 0.50f) * (1.0f - scrolledNt);
        float g = 0.85f + (1.00f - 0.85f) * (1.0f - scrolledNt);
        float b = 1.00f;
        segMR->color = {r, g, b, alpha};
        segMR->isVisible = (alpha > 0.02f);
      }
    } else {
      // 非表示時は全セグメントを隠す
      for (auto segE : m_guideSegments) {
        if (auto* segMR = ctx.world.Get<game::components::MeshRenderer>(segE)) {
          segMR->isVisible = false;
        }
      }
    }
  }

  // HUD 更新
  if (m_hud) {
      PROFILE_SCOPE("WikiGolf.HUD");
      float currentPower = shot->powerGaugePos;
      if (shot->phase == game::components::ShotState::Phase::ImpactTiming || shot->confirmedPower > 0.0f) {
          currentPower = shot->confirmedPower;
      }
      float currentImpact = (shot->phase == game::components::ShotState::Phase::ImpactTiming) ? shot->impactGaugePos : 0.5f;
      const bool isShotPhase = (shot->phase != game::components::ShotState::Phase::Idle &&
                                shot->phase != game::components::ShotState::Phase::ShowResult &&
                                shot->phase != game::components::ShotState::Phase::RestoringCamera);
      m_hudUpdateTimer += dt;
      const bool shouldRefreshHud = isShotPhase || m_hudUpdateTimer >= 0.1f;

      if (shouldRefreshHud) {
          // 間引き中に経過した実時間を渡す。ここで単フレームの dt を渡すと、
          // 間引き期間(約0.1秒)ぶん進んだはずのアイドルアニメーション用の
          // 時計(m_elapsedTime)が単フレーム分しか進まず、クラブの矢印や
          // 選択行の揺れ等がほぼ静止して見える不具合になっていた。
          const float hudDt = isShotPhase ? dt : m_hudUpdateTimer;
          m_hudUpdateTimer = 0.0f;

          // クラブ情報リストを構築して渡す
          std::vector<game::controllers::ClubUIData> clubDataList;
          int clubIdx = 0;
          if (m_clubController) {
              const auto& clubs = m_clubController->GetAllClubs();
              for (const auto& c : clubs) {
                  clubDataList.push_back({c.name, c.iconTexture, c.shortName, c.categoryEN, c.maxPower, c.baseCarryDistance});
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

          m_hud->Update(ctx, hudDt, *state,
                        shot->phase, currentImpact,
                        currentPower, shot->confirmedPower,
                        shot->confirmedImpact,
                        state->windSpeed, state->windDirection,
                        m_cameraController ? m_cameraController->GetYaw() : 0.0f,
                        clubDataList, clubIdx,
                        distanceToTarget, heightDiff);
      }
      
      // HUDへのパワーゲージ更新
      if (shot->phase == game::components::ShotState::Phase::PowerCharging ||
          shot->phase == game::components::ShotState::Phase::ImpactTiming) {
          m_hud->UpdatePowerGauge(ctx, currentPower, currentImpact, 0.0f, 1.0f);
      }

      // 通常時 <-> ショット時 UI 切り替え
      m_hud->SetShotPhaseUIVisible(ctx, isShotPhase);
  }

  if (m_gameJuice) {
      PROFILE_SCOPE("WikiGolf.GameJuice");
      m_gameJuice->Update(ctx, m_cameraEntity, m_ballEntity);
  }
  if (m_terrainSystem) {
      PROFILE_SCOPE("WikiGolf.SurfaceResponse");
      m_terrainSystem->UpdateSurfaceResponse(ctx, m_ballEntity, dt);
  }
  {
    PROFILE_SCOPE("WikiGolf.ProceduralFlags");
    UpdateProceduralFlagEffects(ctx, dt);
  }

  // 着地地形の結果画像(Fairway/Rough/Bunker/Green/OB)の登場・中間・退場
  // アニメーション。スイング判定の結果画像とは完全に別のエンティティ・
  // 別の演出カーブ(このすぐ下のブロック)で、意図的に共通化しない。
  if (m_terrainDisplayTimer > 0.0f) {
      m_terrainDisplayTimer -= dt;
      if (m_terrainImageEntity != UINT32_MAX) {
          auto* ui = ctx.world.Get<game::components::UIImage>(m_terrainImageEntity);
          if (ui) {
              if (m_terrainDisplayTimer <= 0.0f) {
                  ui->visible = false;
              } else {
                  const float total   = std::max(m_terrainDisplayTotal, 0.05f);
                  const float elapsed = total - m_terrainDisplayTimer;
                  const float targetW = m_terrainDisplayTargetW;
                  const float targetH = m_terrainDisplayTargetH;

                  float scale = 1.0f;
                  float alpha = 1.0f;
                  float rotDeg = 0.0f;
                  float offsetX = 0.0f;
                  float offsetY = 0.0f;

                  switch (m_terrainDisplayTier) {
                  case TerrainResultTier::Perfect: {
                      // グリーンにきれいに乗った、上品でゆったりした演出。
                      // 判定側の Perfect(激しく弾む)とは違い、着地の柔らかさを
                      // 表すため沈み込んでから静かに定位置へ戻る。
                      constexpr float kEnter = 0.4f;
                      constexpr float kExit  = 0.4f;
                      if (elapsed < kEnter) {
                          float t = elapsed / kEnter;
                          float settle = 1.0f - (1.0f - t) * (1.0f - t); // イーズアウト
                          scale = 1.08f - 0.08f * settle; // わずかに大きめから収束
                          alpha = std::min(1.0f, t * 1.6f);
                      } else if (m_terrainDisplayTimer < kExit) {
                          float t = m_terrainDisplayTimer / kExit;
                          alpha = t;
                          offsetY = -(1.0f - t) * 10.0f;
                      } else {
                          offsetY = std::sin(elapsed * 1.2f) * 3.0f; // 静かな浮遊
                      }
                      break;
                  }
                  case TerrainResultTier::Good: {
                      // フェアウェイ/氷: 過度な演出を付けない実直なポップ。
                      constexpr float kEnter = 0.2f;
                      constexpr float kExit  = 0.3f;
                      if (elapsed < kEnter) {
                          float t = elapsed / kEnter;
                          scale = t;
                          alpha = t;
                      } else if (m_terrainDisplayTimer < kExit) {
                          alpha = m_terrainDisplayTimer / kExit;
                      }
                      break;
                  }
                  case TerrainResultTier::Rough:
                  default: {
                      // ラフ/バンカー/OB: ボテッと落ちて土煙が舞うような、
                      // 弾みの悪い着地感。表示中もじわっと沈む。
                      constexpr float kEnter = 0.18f;
                      constexpr float kExit  = 0.25f;
                      if (elapsed < kEnter) {
                          float t = elapsed / kEnter;
                          scale = 1.25f - 0.25f * t; // 大きめから縮んで収まる(潰れる感じ)
                          alpha = t;
                          offsetY = (1.0f - t) * 10.0f; // 落下してめり込む
                      } else if (m_terrainDisplayTimer < kExit) {
                          alpha = m_terrainDisplayTimer / kExit;
                      }
                      offsetX += std::sin(elapsed * 9.0f) * 2.0f; // 土煙のようなゆらぎ(低頻度)
                      break;
                  }
                  }

                  ui->alpha = std::clamp(alpha, 0.0f, 1.0f);
                  ui->width  = targetW * scale;
                  ui->height = targetH * scale;
                  ui->rotation = rotDeg; // D2D1::Matrix3x2F::Rotation は度数指定
                  ui->x = (1280.0f - ui->width) * 0.5f + offsetX;
                  ui->y = (720.0f - ui->height) * 0.5f + offsetY;
              }
          }
      }
  }

  // スイング判定の結果画像(Perfect/Great/Nice/Miss)の登場・中間・退場
  // アニメーション。着地地形の演出とは別エンティティ・別カーブ。
  if (m_judgeDisplayTimer > 0.0f) {
      m_judgeDisplayTimer -= dt;
      if (m_judgeImageEntity != UINT32_MAX) {
          auto* ui = ctx.world.Get<game::components::UIImage>(m_judgeImageEntity);
          if (ui) {
              if (m_judgeDisplayTimer <= 0.0f) {
                  ui->visible = false;
              } else {
                  const float total   = std::max(m_judgeDisplayTotal, 0.05f);
                  const float elapsed = total - m_judgeDisplayTimer;
                  const float targetW = m_judgeDisplayTargetW;
                  const float targetH = m_judgeDisplayTargetH;

                  float scale = 1.0f;
                  float alpha = 1.0f;
                  float rotDeg = 0.0f;
                  float offsetX = 0.0f;
                  float offsetY = 0.0f;

                  using Judgement = game::components::ShotJudgement;
                  switch (m_judgeDisplayJudgement) {
                  case Judgement::Special: {
                      // インパクトの気持ちよさを弾けるスタンプで表現。
                      // 大きくオーバーシュートして登場し、余韻中も脈動し続け、
                      // 最後は膨らみながら浮き上がって消える。
                      constexpr float kEnter = 0.28f;
                      constexpr float kExit  = 0.4f;
                      if (elapsed < kEnter) {
                          float t = elapsed / kEnter;
                          float tm1 = t - 1.0f;
                          constexpr float kOvershoot = 2.0f;
                          scale = std::max(0.0f, 1.0f + (kOvershoot + 1.0f) * tm1 * tm1 * tm1 +
                                                  kOvershoot * tm1 * tm1);
                          alpha = std::min(1.0f, t * 2.4f);
                      } else if (m_judgeDisplayTimer < kExit) {
                          float t = m_judgeDisplayTimer / kExit;
                          scale = 1.0f + (1.0f - t) * 0.15f;
                          alpha = t;
                          offsetY = -(1.0f - t) * 16.0f;
                      } else {
                          scale = 1.0f + std::sin(elapsed * 4.0f) * 0.04f;
                          rotDeg = std::sin(elapsed * 2.0f) * 2.2f;
                      }
                      break;
                  }
                  case Judgement::Great: {
                      // 確かな手応え。小さめのバウンドでキビキビ決まる。
                      constexpr float kEnter = 0.18f;
                      constexpr float kExit  = 0.3f;
                      if (elapsed < kEnter) {
                          float t = elapsed / kEnter;
                          scale = t * (1.0f + (1.0f - t) * 0.22f);
                          alpha = t;
                      } else if (m_judgeDisplayTimer < kExit) {
                          alpha = m_judgeDisplayTimer / kExit;
                      }
                      break;
                  }
                  case Judgement::Nice: {
                      // まずまずの手応え。オーバーシュートなしで淡々と現れる。
                      constexpr float kEnter = 0.22f;
                      constexpr float kExit  = 0.25f;
                      if (elapsed < kEnter) {
                          float t = elapsed / kEnter;
                          scale = t;
                          alpha = t;
                      } else if (m_judgeDisplayTimer < kExit) {
                          alpha = m_judgeDisplayTimer / kExit;
                      }
                      break;
                  }
                  case Judgement::Miss:
                  default: {
                      // タイミングを外した気まずさをぐらつきで表現。
                      // 斜めに傾いだまま現れ、表示中ずっと細かく震え、
                      // 余韻なくすぐ消える。
                      constexpr float kEnter = 0.12f;
                      constexpr float kExit  = 0.18f;
                      if (elapsed < kEnter) {
                          float t = elapsed / kEnter;
                          scale = 0.6f + 0.4f * t;
                          alpha = t;
                          rotDeg = (1.0f - t) * -10.0f;
                      } else if (m_judgeDisplayTimer < kExit) {
                          alpha = m_judgeDisplayTimer / kExit;
                      }
                      offsetX += (std::sin(elapsed * 34.0f) + std::sin(elapsed * 51.0f)) * 1.8f;
                      rotDeg  += std::sin(elapsed * 26.0f) * 3.0f;
                      break;
                  }
                  }

                  ui->alpha = std::clamp(alpha, 0.0f, 1.0f);
                  ui->width  = targetW * scale;
                  ui->height = targetH * scale;
                  ui->rotation = rotDeg; // D2D1::Matrix3x2F::Rotation は度数指定
                  ui->x = (1280.0f - ui->width) * 0.5f + offsetX;
                  ui->y = game::ui::kJudgeImageCenterY - ui->height * 0.5f + offsetY;
              }
          }
      }
  }
}

/**
 * @brief シーンの描画処理を行います。
 */
void WikiGolfScene::Render(core::GameContext &ctx) {
  PROFILE_SCOPE("WikiGolf.SceneOverlay");
  m_screenFade.Render(ctx);
}

void WikiGolfScene::RenderOffscreen(core::GameContext &ctx) {
  if (m_minimapController) {
    m_minimapController->RenderPendingMinimap(ctx);
  }
}

/**
 * @brief カップイン判定を行います。
 * @return 遷移が発生した場合はtrue
 */
bool WikiGolfScene::CheckCupIn(core::GameContext &ctx) {
  // Fix2: チュートリアル中にカップイン音SEが毎フレーム連打されるのを防ぐ。
  // gameCleared セット後はオーバーレイが Done になるまでこの関数をスキップする。
  if (m_isTutorial && m_tutorialCupInFired) return false;

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

      // カップイン後にボールが跳ねてホールから出てしまい、判定が
      // ちらつく（出入りで再判定される）のを防ぐため速度を即座に停止する。
      rb->velocity = {0.0f, 0.0f, 0.0f};
      rb->angularVelocity = {0.0f, 0.0f, 0.0f};

      // 遷移前に地形判定UI/スイング判定UIを非表示にする（次のコースに持ち越さない）
      if (m_terrainImageEntity != UINT32_MAX) {
          auto* ui = ctx.world.Get<game::components::UIImage>(m_terrainImageEntity);
          if (ui) ui->visible = false;
      }
      m_terrainDisplayTimer = 0.0f;
      if (m_judgeImageEntity != UINT32_MAX) {
          auto* ui = ctx.world.Get<game::components::UIImage>(m_judgeImageEntity);
          if (ui) ui->visible = false;
      }
      m_judgeDisplayTimer = 0.0f;

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

        // gameCleared をセット: TutorialStep::CupIn の完了判定が依存する
        state->gameCleared = true;

        if (ctx.audio) {
          ctx.audio->PlaySE(ctx, "se_goal.mp3", 1.0f);
        }

        // チュートリアル中はゴール検知をオーバーレイに委ねてリザルトへは進まない
        // Fix2: フラグをセットして以降のカップイン判定をスキップする
        if (m_isTutorial) {
          m_tutorialCupInFired = true;
          return true;
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

      // Fix1: チュートリアル中は非ターゲットホールへのカップインで次ページへ遷移しない。
      // ボールをティー位置にリセットして続行させる。
      if (m_isTutorial) {
        LOG_INFO("WikiGolf", "[Tutorial] Non-target hole cupin. Resetting ball.");
        if (auto *ballT = ctx.world.Get<Transform>(m_ballEntity)) {
          const float terrainHeight =
              m_terrainSystem ? m_terrainSystem->GetHeight(0.0f, -32.0f)
                              : 0.0f;
          ballT->position = {
              0.0f,
              game::physics::ToVisualSurfaceHeight(terrainHeight) +
                  game::physics::kBallRadius,
              -32.0f};
        }
        if (auto *ballRb = ctx.world.Get<RigidBody>(m_ballEntity)) {
          ballRb->velocity = {0.0f, 0.0f, 0.0f};
          ballRb->angularVelocity = {0.0f, 0.0f, 0.0f};
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
