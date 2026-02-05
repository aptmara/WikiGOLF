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
#include "../components/MeshRenderer.h"
#include "../components/PhysicsComponents.h"
#include "../components/Skybox.h"
#include "../components/Transform.h"
#include "../components/UIButton.h"
#include "../components/UIImage.h"
#include "../components/UIText.h"
#include "../components/WikiComponents.h"
#include "../systems/PhysicsSystem.h"
#include "../systems/SkyboxRenderSystem.h"
#include "../systems/WikiClient.h"
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
} // namespace

WikiGolfScene::~WikiGolfScene() = default;

void WikiGolfScene::OnEnter(core::GameContext &ctx) {
  LOG_INFO("WikiGolf", "OnEnter");

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
  t.position = {0.0f, 15.0f * kFieldScale,
                -15.0f * kFieldScale}; // 後ろ上から（UpdateCameraで即座に更新される）
  LOG_DEBUG("WikiGolf", "Camera initial pos: ({}, {}, {})", t.position.x,
            t.position.y, t.position.z);

  LOG_DEBUG("WikiGolf", "Camera created. ID={}, Alive={}", m_cameraEntity,
            ctx.world.IsAlive(m_cameraEntity) ? "true" : "false");

  auto &camComp = ctx.world.Add<Camera>(m_cameraEntity);
  camComp.fov = XMConvertToRadians(60.0f);
  camComp.aspectRatio = 1280.0f / 720.0f;
  camComp.nearZ = 0.1f;
  camComp.farZ = 150.0f;

  // カメラ初期状態（TPSオービットカメラ）
  m_cameraYaw = 0.0f;                   // 初期方向: 北（Z+方向）
  m_cameraPitch = 0.5f;                 // 初期角度: 少し見下ろし（約28.6度）
  m_cameraDistance = 15.0f * kFieldScale; // 初期距離
  m_shotDirection = {0.0f, 0.0f, 1.0f}; // 初期ショット方向

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
    t.scale = {0.1f, 0.1f, 0.1f};

    auto &mr = ctx.world.Add<MeshRenderer>(e);
    mr.mesh = ctx.resource.LoadMesh("builtin/cube");
    mr.shader = ctx.resource.LoadShader("Basic", L"Assets/shaders/BasicVS.hlsl",
                                        L"Assets/shaders/BasicPS.hlsl");
    mr.color = {1.0f, 1.0f, 0.0f, 0.5f}; // 黄色半透明
    mr.isVisible = false;

    m_trajectoryDots.push_back(e);
  }

  auto &amr = ctx.world.Add<MeshRenderer>(m_arrowEntity);
  amr.mesh = ctx.resource.LoadMesh("builtin/cube");
  amr.shader = ctx.resource.LoadShader("Basic", L"Assets/shaders/BasicVS.hlsl",
                                       L"Assets/shaders/BasicPS.hlsl");
  amr.color = {1.0f, 0.2f, 0.2f, 0.8f}; // 赤半透明
  amr.isVisible = false;

  // クラブ初期化
  InitializeClubs(ctx);

  // クラブ3Dモデル初期化
  InitializeClubModel(ctx);

  // === Game Juice システム初期化 ===
  m_gameJuice = std::make_unique<game::systems::GameJuiceSystem>();
  m_gameJuice->Initialize(ctx);

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

  // デフォルト（静的）スカイボックスを生成して割り当て
  // ページごとの動的生成は廃止
  // デバイスロスト回避のため完全に無効化
  // m_skyboxGenerator->GenerateCubemapFromTheme(ctx.graphics.GetDevice(),
  //                                             graphics::SkyboxTheme::Default,
  //                                             skyboxComp.cubemapSRV);

  LOG_INFO("WikiGolf", "Skybox system disabled (Device Lost Prevention)");

  // ゲーム状態初期化
  // ターゲット記事選択（SDOWデータベース優先）
  std::string targetPage;
  int targetId = -1;

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

  // 最短距離が1記事以内のターゲットはスキップして再抽選
  if (m_shortestPath && m_shortestPath->IsAvailable() && !startPage.empty() &&
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
  ft.scale = {20.0f * kFieldScale, 0.5f * kFieldScale,
              30.0f * kFieldScale};

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
  mr.color = {1.0f, 1.0f, 1.0f, 1.0f};

  auto &rb = ctx.world.Add<RigidBody>(m_ballEntity);
  rb.isStatic = false;
  rb.mass = 0.0459f;          // 規定質量 45.9g
  rb.restitution = 0.35f;     // 反発係数 (現実の芝との衝突)
  rb.drag = 0.30f;            // 空気抵抗係数 (Cd値)
  rb.rollingFriction = 0.35f; // 転がり抵抗 (フェアウェイ基準)
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
      shot->phase = ShotState::Phase::Idle;
      shot->judgement = ShotJudgement::None;
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
      infoUI->text = L"[パワー] " + std::to_wstring(powerPct) +
                     L"% (右クリックでキャンセル)";
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

// === 判定UI表示 ===
auto *judgeUI = ctx.world.Get<UIImage>(state->judgeEntity);
if (judgeUI) {
  if (shot->judgement == ShotJudgement::Special) {
    judgeUI->texturePath = "ui_judge_perfect.png";
    judgeUI->visible = true;
    // 幅調整 (Perfect画像は少し横長)
    judgeUI->width = 300.0f;
    judgeUI->height = 100.0f;
  } else if (shot->judgement == ShotJudgement::Great) {
    // ui_judge_great.png があれば表示
    judgeUI->texturePath = "ui_judge_great.png"; // 既存アセット想定
    judgeUI->visible = true; // アセットがない場合は表示されないだけ
    judgeUI->width = 200.0f;
    judgeUI->height = 80.0f;
  } else {
    // その他は表示しないか、既存用
    judgeUI->visible = false;
  }

  if (judgeUI->visible) {
    m_judgeDisplayTimer = 3.0f; // 3秒表示
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

// 判定表示
std::string judgeTexPath;
switch (shot->judgement) {
case ShotJudgement::Great:
  judgeTexPath = "ui_judge_great.png";
  if (infoUI)
    infoUI->text = L"★ GREAT ★";
  break;
case ShotJudgement::Nice:
  judgeTexPath = "ui_judge_nice.png";
  if (infoUI)
    infoUI->text = L"◎ NICE ◎";
  break;
case ShotJudgement::Miss:
  judgeTexPath = "ui_judge_miss.png";
  if (infoUI)
    infoUI->text = L"△ MISS △";
  break;
default:
  break;
}

if (judgeUI && !judgeTexPath.empty()) {
  judgeUI->texturePath = judgeTexPath;
  judgeUI->visible = true;
  LOG_INFO("WikiGolf", "Showing judge UI: {}", judgeTexPath);
}
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

  // === TPSオービットカメラ ===
  // ボール位置を中心に、Yaw/Pitch/Distanceで球面座標上にカメラを配置

  XMVECTOR ballPos = XMLoadFloat3(&ballT->position);

  // カメラの回転クォータニオン（Yaw, Pitch）
  XMVECTOR camRotQ =
      XMQuaternionRotationRollPitchYaw(m_cameraPitch, m_cameraYaw, 0.0f);

  // 基準オフセット（後方 = -Z方向）からカメラまでの距離ベクトル
  XMVECTOR offset = XMVectorSet(0, 0, -m_cameraDistance, 0);
  offset = XMVector3Rotate(offset, camRotQ);

  // カメラ位置 = ボール位置 + オフセット
  XMVECTOR camPos = XMVectorAdd(ballPos, offset);
  XMStoreFloat3(&camT->position, camPos);

  // カメラの回転（ボールを見る向きではなく、Yaw/Pitchそのもの）
  XMStoreFloat4(&camT->rotation, camRotQ);

  // === ショット方向の同期 ===
  // カメラの前方ベクトル（水平成分のみ）をショット方向とする
  XMVECTOR forward = XMVectorSet(0, 0, 1, 0);
  forward = XMVector3Rotate(forward, camRotQ);

  // 水平成分のみ抽出（Y成分をゼロにして正規化）
  XMFLOAT3 fwd;
  XMStoreFloat3(&fwd, forward);
  fwd.y = 0.0f; // 水平方向のみ
  XMVECTOR flatForward = XMLoadFloat3(&fwd);
  flatForward = XMVector3Normalize(flatForward);
  XMStoreFloat3(&m_shotDirection, flatForward);
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
    CreateHole(ctx, worldX, worldZ, link.targetPage, isTarget);
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
  auto *state = ctx.world.GetGlobal<GolfGameState>();

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

  // リザルト画面処理
  if (state && state->gameCleared) {
    auto *bg = ctx.world.Get<UIImage>(state->resultBgEntity);
    auto *txt = ctx.world.Get<UIText>(state->resultTextEntity);

    if (bg)
      bg->visible = true;
    if (txt) {
      txt->visible = true;
      // 簡易テキスト整形
      txt->text = L"STAGE CLEAR!\n\nScore: " +
                  std::to_wstring(state->shotCount) + L"\nTarget: " +
                  core::ToWString(state->targetPage) +
                  L"\n\nClick to Next Level";
    }

    if (ctx.input.GetMouseButtonDown(0)) {
      // タイトルへ戻る
      if (ctx.sceneManager) {
        ctx.sceneManager->ChangeScene(std::make_unique<TitleScene>());
      }
    }
    return;
  }

  if (ctx.input.GetKeyDown('R')) {
    OnEnter(ctx);
    return;
  }

  // マップビュー切り替え (Mキー)
  if (ctx.input.GetKeyDown('M')) {
    m_isMapView = !m_isMapView;
    LOG_INFO("WikiGolf", "Map view: {}", m_isMapView ? "ON" : "OFF");
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

    if (ctx.input.GetMouseButton(1)) {
      int deltaX = mouseX - m_prevMouseX;
      int deltaY = mouseY - m_prevMouseY;

      float sensitivity = 0.05f * m_mapZoom;
      m_mapCenterOffset.x -= deltaX * sensitivity;
      m_mapCenterOffset.z += deltaY * sensitivity;
    }

    // マップビューのズーム（ホイール + キー）
    float wheel = ctx.input.GetMouseScrollDelta();
    if (wheel != 0.0f) {
      m_mapZoom = std::clamp(m_mapZoom - wheel * 0.1f, 0.1f,
                             2.0f); // 最小値を0.3から0.1に緩和
    }
    if (ctx.input.GetKeyDown(VK_OEM_PLUS) || ctx.input.GetKeyDown(VK_ADD)) {
      m_mapZoom = std::clamp(m_mapZoom - 0.1f, 0.1f, 2.0f);
    }
    if (ctx.input.GetKeyDown(VK_OEM_MINUS) ||
        ctx.input.GetKeyDown(VK_SUBTRACT)) {
      m_mapZoom = std::clamp(m_mapZoom + 0.1f, 0.1f, 2.0f);
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

        // Pitch制限（上: -0.1rad, 下: 1.4rad ≒ 80度）
        m_cameraPitch = std::clamp(m_cameraPitch, -0.1f, 1.4f);
      }

      // マウスホイールでズーム
      float wheel = ctx.input.GetMouseScrollDelta();
      if (wheel != 0.0f) {
        m_cameraDistance -= wheel * 2.0f * kFieldScale; // ホイール感度
        m_cameraDistance =
            std::clamp(m_cameraDistance, 3.0f * kFieldScale,
                       30.0f * kFieldScale); // 距離制限
      }
    }
  }

  m_prevMouseX = mouseX;
  m_prevMouseY = mouseY;

  // クリア済みの場合、リトライボタンのみチェック
  if (state->gameCleared) {
    auto *retryBtn = ctx.world.Get<UIButton>(state->retryButtonEntity);
    if (retryBtn && retryBtn->state == ButtonState::Pressed) {
      OnEnter(ctx);
    }
    return;
  }

  // 物理更新
  game::systems::PhysicsSystem(ctx, ctx.dt);

  // ショット処理
  ProcessShot(ctx);

  // ボール速度チェック（停止したらショット可能に）
  if (ctx.world.IsAlive(m_ballEntity)) {
    auto *rb = ctx.world.Get<RigidBody>(m_ballEntity);
    auto *ballT = ctx.world.Get<Transform>(m_ballEntity);

    // 落下チェック（Y < -5 でリスポーン）
    if (ballT && ballT->position.y < -5.0f) {
      ballT->position = {0.0f, 1.0f,
                         -8.0f * kFieldScale}; // スタート位置に戻す
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

        rb->velocity = {0, 0, 0};
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
}

void WikiGolfScene::UpdateMapCamera(core::GameContext &ctx) {
  if (!ctx.world.IsAlive(m_cameraEntity))
    return;

  auto *camT = ctx.world.Get<Transform>(m_cameraEntity);
  if (!camT)
    return;

  // フィールド中央の真上から見下ろす
  float height = std::max(m_fieldWidth, m_fieldDepth) * m_mapZoom;

  // 目標位置: オフセット適用
  XMVECTOR targetPos =
      XMVectorSet(m_mapCenterOffset.x, height, m_mapCenterOffset.z, 0.0f);

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
  if (m_minimapRenderer) {
    m_minimapRenderer->RenderMinimap(ctx);
  }
}

void WikiGolfScene::SaveHighScore(const std::string &targetPage, int shots) {
  // 既存のスコアを読み込み
  std::map<std::string, int> scores;
  std::ifstream inFile("../../scores.txt");
  if (inFile.is_open()) {
    std::string line;
    while (std::getline(inFile, line)) {
      size_t pos = line.find('|');
      if (pos != std::string::npos) {
        std::string page = line.substr(0, pos);
        int score = std::stoi(line.substr(pos + 1));
        scores[page] = score;
      }
    }
    inFile.close();
  }

  // 新記録か確認
  if (scores.find(targetPage) == scores.end() || shots < scores[targetPage]) {
    scores[targetPage] = shots;
    LOG_INFO("WikiGolf", "New high score for '{}': {} shots", targetPage,
             shots);

    // ファイルに書き込み
    std::ofstream outFile("../../scores.txt");
    if (outFile.is_open()) {
      for (const auto &[page, score] : scores) {
        outFile << page << "|" << score << "\n";
      }
      outFile.close();
    }
  }
}

int WikiGolfScene::LoadHighScore(const std::string &targetPage) {
  std::ifstream inFile("../../scores.txt");
  if (!inFile.is_open()) {
    return -1; // 記録なし
  }

  std::string line;
  while (std::getline(inFile, line)) {
    size_t pos = line.find('|');
    if (pos != std::string::npos) {
      std::string page = line.substr(0, pos);
      if (page == targetPage) {
        return std::stoi(line.substr(pos + 1));
      }
    }
  }
  return -1; // 記録なし
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

  // シミュレーション設定
  const float dt = 0.05f; // 刻み幅
  const XMVECTOR gravity = XMVectorSet(0.0f, -9.8f, 0.0f, 0.0f);

  // RigidBody設定（ボールと同じ値を使う）
  auto *rb = ctx.world.Get<RigidBody>(m_ballEntity);
  float drag = rb ? rb->drag : 0.01f;
  float rollingFriction = rb ? rb->rollingFriction : 0.5f;
  XMVECTOR initialAngularVelocity =
      rb ? XMLoadFloat3(&rb->angularVelocity) : XMVectorZero();

  // 風情報取得
  auto *golfState = ctx.world.GetGlobal<GolfGameState>();

  // 地形データ取得
  auto terrainData =
      m_terrainSystem ? m_terrainSystem->GetTerrainData() : nullptr;

  // マテリアル取得ヘルパー
  auto GetMaterial = [&](float x, float z) -> uint8_t {
    if (!terrainData || terrainData->materialMap.empty())
      return 0; // Default Fairway
    float u = x / terrainData->config.worldWidth + 0.5f;
    float v = 0.5f - z / terrainData->config.worldDepth;
    int ix = (int)(u * (terrainData->config.resolutionX - 1));
    int iz = (int)(v * (terrainData->config.resolutionZ - 1));
    if (ix < 0 || ix >= terrainData->config.resolutionX || iz < 0 ||
        iz >= terrainData->config.resolutionZ)
      return 0;
    return terrainData->materialMap[iz * terrainData->config.resolutionX + ix];
  };

  // 初期位置（ボール位置）
  XMVECTOR prevPos = pos;
  XMVECTOR angularVelocity = initialAngularVelocity;

  for (size_t i = 0; i < m_trajectoryDots.size(); ++i) {
    auto e = m_trajectoryDots[i];
    auto *t = ctx.world.Get<Transform>(e);
    auto *mr = ctx.world.Get<MeshRenderer>(e);

    if (!mr || !t)
      continue;

    XMVECTOR currentPos = prevPos;

    // --- 物理ステップ (PhysicsSystemのロジックを模倣) ---
    float speed = XMVectorGetX(XMVector3Length(vel));

    XMVECTOR acc;

    // 1. 空気抵抗 (二乗則)
    // F = 0.5 * rho * v^2 * Cd * A => mで割って加速度a
    // 係数 K = 0.000876 (PhysicsSystemと同じ)
    if (speed > 0.001f) {
      float K = 0.000876f;
      float dragVal = rb ? rb->drag : 0.30f;
      float dragForce = K * dragVal * speed * speed;
      float dragAccMag = dragForce / (rb ? rb->mass : 0.0459f);

      XMVECTOR dragDir = XMVectorScale(vel, -1.0f / speed);
      XMVECTOR dragAcc = XMVectorScale(dragDir, dragAccMag);

      // マグヌス効果 (簡易計算 - 揚力として追加)
      // 本来は角速度ベクトルとの外積だが、簡易的にY軸方向への揚力とXZ方向への曲がりを考慮
      // ここでは重力とドラッグのみをメインとして計算し、マグヌスは以前の簡易実装を維持しつつ係数調整
      XMVECTOR magnusAcc = XMVectorZero();
      /*
       * マグヌスは複雑なため、予測線では「曲がり」はショットパラメータで計算済みの
       * 初期velocityに含まれるサイドスピン成分に任せ、
       * ここでは追加の揚力（バックスピンによる滞空時間延長）のみ考慮する
       */

      acc = XMVectorAdd(gravity, dragAcc);
    } else {
      acc = gravity;
    }

    // 風の影響 (簡易: 加速度として加算 - 本来は相対速度だが予測線では近似)
    if (golfState && golfState->windSpeed > 0.0f) {
      float windForce = golfState->windSpeed * 0.1f; // 係数調整
      XMVECTOR windVec = XMVectorSet(golfState->windDirection.x, 0,
                                     golfState->windDirection.y, 0);
      acc = XMVectorAdd(acc, XMVectorScale(windVec, windForce));
    }

    // 速度更新 (オイラー積分)
    vel = XMVectorAdd(vel, XMVectorScale(acc, dt));

    // 位置更新
    currentPos = XMVectorAdd(prevPos, XMVectorScale(vel, dt));

    // 地面との接触判定
    float groundY = 0.0f;
    bool isGrounded = false;
    XMVECTOR groundNormal = XMVectorSet(0, 1, 0, 0);

    if (m_terrainSystem) {
      XMVECTOR n;
      float h; // dummy
      float px = XMVectorGetX(currentPos);
      float pz = XMVectorGetZ(currentPos);
      // GetHeightは補間あり、GetNormalも必要だがここでは簡易的にY高さだけチェック
      groundY = m_terrainSystem->GetHeight(px, pz);

      // 法線取得（傾斜抵抗のため）
      // 簡易差分法 (PhysicsSystemと同じロジックの一部)
      float hX = m_terrainSystem->GetHeight(px + 0.1f, pz);
      float hZ = m_terrainSystem->GetHeight(px, pz + 0.1f);
      float gradX = (hX - groundY) / 0.1f;
      float gradZ = (hZ - groundY) / 0.1f;
      XMVECTOR slopeVec = XMVectorSet(-gradX, 1.0f, -gradZ, 0.0f);
      groundNormal = XMVector3Normalize(slopeVec);
    }

    if (XMVectorGetY(currentPos) <= groundY) {
      // 着地
      currentPos = XMVectorSetY(currentPos, groundY);
      isGrounded = true;

      // 速度のY成分を反転・減衰 (バウンド)
      float vy = XMVectorGetY(vel);
      if (vy < 0.0f) {
        float restitution = rb ? rb->restitution : 0.35f;

        // 浅い角度なら転がりへ移行
        if (std::abs(vy) < 2.0f) {
          vy = 0.0f;
          // 傾斜に沿ったベクトルへ修正
          // ここは厳密にやると複雑なので、Yを0にするだけにする（PhysicsSystemの着地処理近似）
        } else {
          vy = -vy * restitution;
        }
        vel = XMVectorSetY(vel, vy);
      }

      // 転がり抵抗 (PhysicsSystemと同じ減算方式)
      if (std::abs(XMVectorGetY(vel)) < 0.1f) { // ほぼ転がっている
        float baseFriction = rb ? rb->rollingFriction : 0.35f;
        float terrainFriction = 1.0f;

        if (terrainData) {
          uint8_t mat =
              GetMaterial(XMVectorGetX(currentPos), XMVectorGetZ(currentPos));
          switch (mat) {
          case 1:
            terrainFriction = 2.0f;
            break; // Rough
          case 2:
            terrainFriction = 3.0f;
            break; // Bunker
          case 3:
            terrainFriction = 0.28f;
            break; // Green
          }
        }

        float mu = baseFriction * terrainFriction;

        // 傾斜による加速/減速 (簡易: 重力の接線成分)
        XMVECTOR slopeAcc = XMVectorZero();
        // 重力 g * sin(theta) ... 簡易的に勾配から算出
        // 摩擦による減速: a = -mu * g
        float frictionDecel = mu * 9.8f;

        float currentSpeed = XMVectorGetX(XMVector3Length(vel));
        if (currentSpeed > 0.0f) {
          float dropSpeed = frictionDecel * dt;
          if (currentSpeed <= dropSpeed) {
            vel = XMVectorZero();
            currentSpeed = 0.0f;
          } else {
            vel = XMVectorScale(vel, (currentSpeed - dropSpeed) / currentSpeed);
          }
        }
      }
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
  m_availableClubs.push_back({"Driver", 65.0f, 12.0f, "icon_driver.png"});
  m_availableClubs.push_back({"Iron", 48.0f, 18.0f, "icon_iron.png"});
  m_availableClubs.push_back({"Wedge", 35.0f, 26.0f, "icon_wedge.png"});
  m_availableClubs.push_back({"Putter", 10.0f, 0.0f, "icon_putter.png"});

  // UI作成
  for (size_t i = 0; i < m_availableClubs.size(); ++i) {
    auto e = CreateEntity(ctx.world);

    // アイコン画像
    auto &img = ctx.world.Add<UIImage>(e);
    // texturePathを設定
    img = UIImage::Create(m_availableClubs[i].iconTexture, 0, 0);

    // 画面右端、垂直に配置
    float startY = 720.0f / 2.0f - (m_availableClubs.size() * 100.0f) / 2.0f;
    img.x = 1150.0f; // 右側
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
  LOG_INFO("WikiGolf", "Initializing UI elements...");

  // ミニマップUI作成 (右上)
  if (m_minimapRenderer) {
    m_minimapEntity = CreateEntity(ctx.world);
    auto &ui = ctx.world.Add<UIImage>(m_minimapEntity);
    ui.textureSRV = m_minimapRenderer->GetSRV();
    ui.width = 180.0f; // 少し小さく
    ui.height = 180.0f;
    ui.x = 1080.0f; // 左に移動
    ui.y = 20.0f;
    ui.visible = true;
    ui.layer = 100; // 手前に表示
  }

  // Header
  auto headerE = CreateEntity(ctx.world);
  auto &ht = ctx.world.Add<UIText>(headerE);
  ht.text = L"Loading..."; // 初期値
  ht.x = 20;
  ht.y = 20;
  ht.style = graphics::TextStyle::Status(); // ステータススタイル（40pt）
  ht.visible = true;
  ht.layer = 10;
  state.headerEntity = headerE;

  // Shot HUD
  auto shotE = CreateEntity(ctx.world);
  auto &st = ctx.world.Add<UIText>(shotE);
  st.text = L""; // 初期化はUpdateHUDで行う
  st.x = 20;
  st.y = 70;
  st.style = graphics::TextStyle::Status(); // ステータススタイル（40pt）
  st.visible = true;
  st.layer = 10;
  state.shotCountEntity = shotE;

  // Wind UI
  auto windE = CreateEntity(ctx.world);
  auto &wt = ctx.world.Add<UIText>(windE);
  wt.x = 950; // 左に移動
  wt.y = 10;
  wt.visible = true;
  wt.layer = 10;
  state.windEntity = windE;

  // Wind Arrow
  LOG_INFO("WikiGolf", "Creating wind arrow UI...");
  auto windArrowE = CreateEntity(ctx.world);
  auto &wa = ctx.world.Add<UIImage>(windArrowE);
  wa = UIImage::Create("ui_wind_arrow.png", 970.0f, 40.0f);
  wa.width = 64.0f;
  wa.height = 64.0f;
  wa.visible = true;
  wa.layer = 10;
  state.windArrowEntity = windArrowE;

  // Gauge (Bg, Fill, Marker)
  LOG_INFO("WikiGolf", "Creating gauge UI...");
  // ゲージ背景は削除
  // auto gaugeBarE = CreateEntity(ctx.world);
  // auto &gb = ctx.world.Add<UIImage>(gaugeBarE);
  // gb = UIImage::Create("ui_gauge_bg.png", 440.0f, 650.0f);
  // gb.width = 400.0f;
  // gb.height = 30.0f;
  // gb.visible = true;
  // gb.layer = 10;
  // state.gaugeBarEntity = gaugeBarE;

  // --- パワーゲージ (D2D UIBarGauge) ---
  state.gaugeBarEntity = CreateEntity(ctx.world);
  auto &gauge = ctx.world.Add<UIBarGauge>(state.gaugeBarEntity);
  gauge.x = 440.0f;
  gauge.y = 650.0f;
  gauge.width = 400.0f;
  gauge.height = 30.0f;

  gauge.value = 0.0f;
  gauge.maxValue = 1.0f;
  gauge.color = {0.2f, 0.8f, 1.0f, 1.0f}; // 青
  gauge.bgColor = {0.0f, 0.0f, 0.0f, 0.8f};
  gauge.borderColor = {1.0f, 1.0f, 1.0f, 1.0f};
  gauge.borderWidth = 2.0f;
  gauge.isVisible = false;

  gauge.showImpactZones = false;
  gauge.impactCenter = 0.5f;
  gauge.impactWidthGreat = 0.1f;
  gauge.impactWidthNice = 0.3f;
  gauge.showMarker = false;

  state.gaugeFillEntity = 0;
  state.gaugeMarkerEntity = 0;

  // Path History
  auto pathE = CreateEntity(ctx.world);
  auto &pathT = ctx.world.Add<UIText>(pathE);
  pathT.text = L"History: ";
  pathT.x = 20;
  pathT.y = 130;
  pathT.visible = true;
  pathT.layer = 10;
  pathT.style = graphics::TextStyle::ModernBlack();
  pathT.style.fontSize = 24.0f; // 24ptに拡大
  pathT.style.hasShadow = true;
  state.pathEntity = pathE;

  // Judge Result
  LOG_INFO("WikiGolf", "Creating judge UI...");
  auto judgeE = CreateEntity(ctx.world);
  auto &ji = ctx.world.Add<UIImage>(judgeE);
  ji = UIImage::Create("ui_judge_great.png", 540.0f, 280.0f); // 画面中央に配置
  ji.width = 200.0f;                                          // 小さく
  ji.height = 80.0f;                                          // 小さく
  ji.visible = false;
  ji.layer = 20;
  state.judgeEntity = judgeE;

  // Result Screen UI
  LOG_INFO("WikiGolf", "Creating result UI...");
  // リザルト背景は削除
  // auto resultBgE = CreateEntity(ctx.world);
  // auto &rbg = ctx.world.Add<UIImage>(resultBgE);
  // rbg = UIImage::Create("ui_gauge_bg.png", 0, 0);
  // rbg.width = 1280.0f;
  // rbg.height = 720.0f;
  // rbg.alpha = 0.8f;
  // rbg.visible = false;
  // rbg.layer = 50;
  // state.resultBgEntity = resultBgE;

  auto resultTextE = CreateEntity(ctx.world);
  auto &rt = ctx.world.Add<UIText>(resultTextE);
  rt.x = 400.0f;
  rt.y = 250.0f;
  rt.text = L"🎉 クリア！";
  rt.visible = false;
  rt.layer = 51;
  rt.style.fontSize = 48.0f;
  rt.style.color = {1.0f, 0.84f, 0.0f, 1.0f}; // 金色
  state.resultTextEntity = resultTextE;

  auto retryBtnE = CreateEntity(ctx.world);
  auto &btn = ctx.world.Add<UIButton>(retryBtnE);
  btn = UIButton::Create(L"もう一度", "retry", 540.0f, 400.0f, 200.0f, 50.0f);
  btn.visible = false;
  state.retryButtonEntity = retryBtnE;

  // Context-Sensitive Guide UI
  LOG_INFO("WikiGolf", "Creating guide UI...");

  // 背景（Message Bar）は削除
  // auto guideBgE = CreateEntity(ctx.world);
  // auto &gbg = ctx.world.Add<UIImage>(guideBgE);
  // // ui_gauge_bg.png（半透明黒）を画面幅いっぱいに引き伸ばして使用
  // gbg = UIImage::Create("ui_gauge_bg.png", 0.0f, 660.0f);
  // gbg.width = 1280.0f;
  // gbg.height = 60.0f;
  // gbg.alpha = 0.9f; // 濃いめ
  // gbg.visible = true;
  // gbg.layer = 90; // テキストより奥
  // state.guideBgEntity = guideBgE;

  // テキスト
  auto guideE = CreateEntity(ctx.world);
  auto &gt = ctx.world.Add<UIText>(guideE);
  gt.text = L"";
  gt.x = 600.0f;                           // 左寄りに配置
  gt.y = 640.0f;                           // 少し上に調整
  gt.width = 800.0f;                       // 幅を調整
  gt.style = graphics::TextStyle::Guide(); // 新しいガイドスタイル（24pt）
  gt.visible = true;
  gt.layer = 100; // 最前面
  state.guideEntity = guideE;
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
    text = L"[右ドラッグ] 移動  [ホイール] ズーム  [M] 戻る";
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
        text = L"[マウス] 狙う  [Shift] 精密  [Q/E] クラブ  [M] マップ";
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
  ctx.world.Query<game::components::GolfHole>().Each(
      [&](ecs::Entity e, game::components::GolfHole &) {
        holesToDelete.push_back(e);
      });
  for (auto e : holesToDelete) {
    ctx.world.DestroyEntity(e);
  }
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
    allLinks = wikiClient.FetchPageLinks(pageName, 100);
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

  for (const auto &link : allLinks) {
    if (isIgnored(link.title))
      continue;

    // 本文に含まれているかチェック
    if (articleText.find(link.title) != std::string::npos) {
      validLinks.push_back({link.title, core::ToWString(link.title)});
    }
    // ターゲットページは必ず含める
    else if (link.title == state->targetPage) {
      validLinks.push_back({link.title, core::ToWString(link.title)});
    }

    if (validLinks.size() >= 20)
      break;
  }

  // リンク不足時の補充
  if (validLinks.size() < 3) {
    for (const auto &link : allLinks) {
      bool exists = false;
      for (const auto &v : validLinks)
        if (v.first == link.title)
          exists = true;
      if (!exists && !isIgnored(link.title)) { // ここでもignoreチェック
        validLinks.push_back({link.title, core::ToWString(link.title)});
        if (validLinks.size() >= 5)
          break;
      }
    }
  }

  // ラムダ式内で使うisIgnoredをここでも定義する必要があったので、
  // 上記の補充ループ内のisIgnoredはコンパイルエラーになる可能性がある。
  // まじめに実装しなおす。

  // 4. フィールドサイズ計算
  const float minFieldWidth = 20.0f * kFieldScale;
  const float minFieldDepth = 30.0f * kFieldScale;
  float articleLengthFactor =
      std::max(1.0f, (float)articleText.length() / 1000.0f);
  float fieldWidth = minFieldWidth * std::sqrt(articleLengthFactor);
  float fieldDepth = minFieldDepth * std::sqrt(articleLengthFactor);

  // 安全策: 最小サイズ保証
  fieldWidth = std::max(fieldWidth, minFieldWidth);
  fieldDepth = std::max(fieldDepth, minFieldDepth);

  m_fieldWidth = fieldWidth;
  m_fieldDepth = fieldDepth;
  state->fieldWidth = fieldWidth;
  state->fieldDepth = fieldDepth;

  // 5. テクスチャ生成
  uint32_t texWidth = static_cast<uint32_t>(fieldWidth * 100.0f);
  uint32_t texHeight = static_cast<uint32_t>(fieldDepth * 100.0f);

  std::vector<std::pair<std::wstring, std::string>> linkPairs;
  for (const auto &link : validLinks) {
    linkPairs.push_back({link.second, link.first});
  }

  // === Skybox テクスチャ生成（プロシージャル） (無効化) ===
  // if (m_skyboxGenerator) {
  //   auto *skyboxComp = ctx.world.Get<components::Skybox>(m_skyboxEntity);
  //   if (skyboxComp) {
  //     // ... (Generation logic removed)
  //   }
  // }

  auto texResult = m_textureGenerator->GenerateTexture(
      core::ToWString(pageName), core::ToWString(articleText), linkPairs,
      state->targetPage, texWidth, texHeight);

  m_wikiTexture =
      std::make_unique<graphics::WikiTextureResult>(std::move(texResult));

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
  } else {
    LOG_ERROR("WikiGolf", "Ball transform not found!");
  }

  // 7. ホール配置
  // 同じ座標に複数のホールを作らないよう追跡（座標ベース）
  std::vector<std::pair<float, float>> createdHolePositions;
  const float texWidthF = (float)m_wikiTexture->width;
  const float texHeightF = (float)m_wikiTexture->height;
  const float minHoleDistance = 0.2f; // ホール間の最小距離

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

    CreateHole(ctx, worldX, worldZ, linkRegion.targetPage, linkRegion.isTarget);
  }

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
                               const std::string &linkTarget,
                               bool isTargetHole) {
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
  // カップの底：地形とほぼ面一。中心は地面よりごく僅かに下げる。
  t.position = {x, terrainHeight - 0.02f, z};
  t.scale = {0.22f, 0.07f, 0.22f}; // 見た目半径~0.22, 高さ0.07

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
  h.radius = 0.55f;                         // 吸い込み有効半径を拡大
  h.gravity = isTargetHole ? 24.0f : 14.0f; // 吸引力弱め
  h.linkTarget = linkTarget;
  h.isTarget = isTargetHole;

  // ホールを物理演算用にも登録（ただしPhysicsSystemで反発は無視される）
  auto &col = ctx.world.Add<Collider>(e);
  col.type = ColliderType::Cylinder;
  col.radius = 0.2f; // 見た目に合わせて拡大
  col.size = {0.2f, 0.1f, 0.2f};

  // ホールエンティティをリストに追加
  state->holes.push_back(e);

  LOG_DEBUG("WikiGolf",
            "Created hole at ({}, {}, {}) for target '{}', isTarget={}", x,
            t.position.y, z, linkTarget, isTargetHole);
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

    // 距離チェック (XZ平面)
    float dx = t->position.x - holeT->position.x;
    float dz = t->position.z - holeT->position.z;
    float distSq = dx * dx + dz * dz;
    float radius = hole->radius * 0.9f; // 判定はややタイト

    bool inHole = (distSq < radius * radius);

    // 高さチェック (カップの底に落ちているか)
    float dy = t->position.y - holeT->position.y;
    bool atBottom = (dy < 0.2f && dy > -0.2f); // 許容範囲

    if (inHole && atBottom && speedSq < 0.01f) {
      // カップイン！
      LOG_INFO("WikiGolf", "Cup In! Target: {}", hole->linkTarget);

      // === 超派手なホールイン演出 ===
      if (m_gameJuice) {
        // 巨大カメラシェイク
        m_gameJuice->TriggerCameraShake(0.8f, 0.5f);

        // 複数回のパーティクル爆発（タイミングをずらして）
        XMFLOAT3 effectPos = holeT->position;
        effectPos.y += 0.5f; // ホールの上から発火

        // 1回目：中央大爆発
        m_gameJuice->TriggerImpactEffect(ctx, effectPos, 1.0f);

        // 2回目以降は高さを変えて（連続爆発風）
        effectPos.y += 1.0f;
        m_gameJuice->TriggerImpactEffect(ctx, effectPos, 0.8f);

        effectPos.y += 1.0f;
        m_gameJuice->TriggerImpactEffect(ctx, effectPos, 0.6f);

        // FOV変化（ズームイン→アウト）
        m_gameJuice->SetTargetFov(40.0f); // 一瞬ズームイン
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

  // クラブの変形（位置・回転）を適用
  XMStoreFloat3(&clubTr->position, clubBasePos);

  // スイング角度をX軸回転として適用（ピッチ）
  // Y軸はショット方向に合わせる
  float pitchRad = XMConvertToRadians(m_clubSwingAngle);
  XMVECTOR q = XMQuaternionRotationRollPitchYaw(pitchRad, yaw, 0);
  XMStoreFloat4(&clubTr->rotation, q);
}

void WikiGolfScene::OnExit(core::GameContext &ctx) {
  LOG_INFO("WikiGolf", "Exiting WikiGolfScene");
  Scene::OnExit(ctx);
}

} // namespace game::scenes
