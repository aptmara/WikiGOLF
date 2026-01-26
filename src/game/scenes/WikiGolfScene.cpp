#include "WikiGolfScene.h"
#include "../../audio/AudioSystem.h"
#include "../../core/GameContext.h"
#include "../../core/Input.h"
#include "../../core/Logger.h"
#include "../../core/SceneManager.h"
#include "../../core/StringUtils.h"
#include "../../ecs/World.h"
#include "../../graphics/GraphicsDevice.h"
#include "../../graphics/WikiTextureGenerator.h"
#include "../components/Camera.h"
#include "../components/MeshRenderer.h"
#include "../components/PhysicsComponents.h"
#include "../components/Transform.h"
#include "../components/UIButton.h"
#include "../components/UIImage.h"
#include "../components/UIText.h"
#include "../components/WikiComponents.h"
#include "../systems/PhysicsSystem.h"
#include "../systems/WikiClient.h"
#include "TitleScene.h"
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>

// Windowsマクロ対策
#undef min
#undef max

namespace game::scenes {

using namespace DirectX;
using namespace game::components;

WikiGolfScene::~WikiGolfScene() = default;

void WikiGolfScene::OnEnter(core::GameContext &ctx) {
  LOG_INFO("WikiGolf", "OnEnter");

  // BGM再生
  if (ctx.audio) {
    ctx.audio->PlayBGM(ctx, "bgm_game.mp3", 0.3f);
  }

  // シーン遷移時にマウスカーソルを表示・ロック解除
  ctx.input.SetMouseCursorVisible(true);
  ctx.input.SetMouseCursorLocked(false);

  m_textureGenerator = std::make_unique<graphics::WikiTextureGenerator>();
  m_textureGenerator->Initialize(ctx.graphics.GetDevice());

  // カメラ（ボール追従）
  m_cameraEntity = ctx.world.CreateEntity();
  auto &t = ctx.world.Add<Transform>(m_cameraEntity);
  t.position = {0.0f, 15.0f, -15.0f}; // 後ろ上から
  LOG_DEBUG("WikiGolf", "Camera initial pos: ({}, {}, {})", t.position.x,
            t.position.y, t.position.z);

  // 前方下を向く（約40度）
  XMVECTOR q =
      XMQuaternionRotationRollPitchYaw(XMConvertToRadians(40.0f), 0.0f, 0.0f);
  XMStoreFloat4(&t.rotation, q);

  LOG_DEBUG("WikiGolf", "Camera created. ID={}, Alive={}", m_cameraEntity,
            ctx.world.IsAlive(m_cameraEntity) ? "true" : "false");

  auto &camComp = ctx.world.Add<Camera>(m_cameraEntity);
  camComp.fov = XMConvertToRadians(60.0f);
  camComp.aspectRatio = 1280.0f / 720.0f;
  camComp.nearZ = 0.1f;
  camComp.farZ = 150.0f;

  // カメラ初期距離
  m_cameraDistance = 15.0f;
  m_targetCameraDistance = 15.0f;
  m_shotDirection = {0.0f, 0.0f, 1.0f};

  // ミニマップ初期化
  if (!m_minimapRenderer) {
    m_minimapRenderer = std::make_unique<game::systems::MapSys>();
    if (!m_minimapRenderer->Initialize(ctx.graphics.GetDevice(), 720, 720)) {
      LOG_WARN("WikiGolf", "Minimap initialization failed");
      m_minimapRenderer.reset();
    } else {
      LOG_INFO("WikiGolf", "Minimap initialized");
    }
  }

  // 矢印（ショット予測線）
  m_arrowEntity = ctx.world.CreateEntity();
  auto &at = ctx.world.Add<Transform>(m_arrowEntity);
  at.scale = {0.0f, 0.0f, 0.0f}; // 最初は非表示

  // 軌道予測用（ドットのプール作成）
  m_trajectoryDots.clear();
  for (int i = 0; i < 30; ++i) {
    auto e = ctx.world.CreateEntity();
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

  // === Game Juice システム初期化 ===
  m_gameJuice = std::make_unique<game::systems::GameJuiceSystem>();
  m_gameJuice->Initialize(ctx);

  // === Wiki Terrain システム初期化 ===
  m_terrainSystem = std::make_unique<game::systems::WikiTerrainSystem>();

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
  m_floorEntity = ctx.world.CreateEntity();
  auto &ft = ctx.world.Add<Transform>(m_floorEntity);
  ft.position = {0.0f, 0.0f, 0.0f};
  ft.scale = {20.0f, 0.5f, 30.0f};

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

  m_ballEntity = ctx.world.CreateEntity();
  auto &t = ctx.world.Add<Transform>(m_ballEntity);
  t.position = {0.0f, 1.0f, -8.0f}; // 初期位置を少し高くして床抜け防止
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
  rb.mass = 1.0f;
  rb.restitution = 0.5f;     // 反発係数
  rb.drag = 0.1f;            // 空気抵抗
  rb.rollingFriction = 2.0f; // 転がり抵抗
  rb.velocity = {0, 0, 0};

  auto &c = ctx.world.Add<Collider>(m_ballEntity);
  c.type = ColliderType::Sphere;
  c.radius = 0.04f;

  // グローバル状態のボール参照も更新
  auto *state = ctx.world.GetGlobal<GolfGameState>();
  if (state)
    state->ballEntity = m_ballEntity;
}

void WikiGolfScene::UpdateCamera(core::GameContext &ctx) {
  if (!ctx.world.IsAlive(m_ballEntity) || !ctx.world.IsAlive(m_cameraEntity)) {
    static bool loggedOnce1 = false;
    if (!loggedOnce1) {
      LOG_ERROR("WikiGolf",
                "UpdateCamera early return: ball alive={}, cam alive={}",
                ctx.world.IsAlive(m_ballEntity) ? "true" : "false",
                ctx.world.IsAlive(m_cameraEntity) ? "true" : "false");
      loggedOnce1 = true;
    }
    return;
  }

  auto *ballT = ctx.world.Get<Transform>(m_ballEntity);
  auto *camT = ctx.world.Get<Transform>(m_cameraEntity);
  auto *state = ctx.world.GetGlobal<GolfGameState>();
  if (!ballT || !camT || !state) {
    static bool loggedOnce2 = false;
    if (!loggedOnce2) {
      LOG_ERROR("WikiGolf",
                "UpdateCamera early return: ballT={}, camT={}, state={}",
                (void *)ballT, (void *)camT, (void *)state);
      loggedOnce2 = true;
    }
    return;
  }

  XMVECTOR ballPos = XMLoadFloat3(&ballT->position);

  // 目標カメラ距離（ショット可能時は近づく）
  if (state->canShoot) {
    m_targetCameraDistance = 8.0f; // 近い
  } else {
    m_targetCameraDistance = 15.0f; // 遠い
  }

  // カメラ距離を滑らかに補間
  m_cameraDistance +=
      (m_targetCameraDistance - m_cameraDistance) * (3.0f * ctx.dt);

  // ショット方向に基づいてカメラ位置を計算
  XMVECTOR shotDir = XMLoadFloat3(&m_shotDirection);
  shotDir = XMVector3Normalize(shotDir);

  // カメラはショット方向の逆側（後ろ）に配置
  XMVECTOR camOffset = XMVectorScale(shotDir, -m_cameraDistance * 0.7f);
  camOffset = XMVectorAdd(
      camOffset, XMVectorSet(0, m_cameraDistance * 0.6f, 0, 0)); // 上方向

  XMVECTOR targetCamPos = XMVectorAdd(ballPos, camOffset);

  // 現在のカメラ位置を滑らかに補間
  XMVECTOR currentCamPos = XMLoadFloat3(&camT->position);
  XMVECTOR newCamPos = XMVectorLerp(currentCamPos, targetCamPos, 5.0f * ctx.dt);
  XMStoreFloat3(&camT->position, newCamPos);

  // カメラをボールの方向に向ける
  XMVECTOR lookDir = XMVectorSubtract(ballPos, newCamPos);
  lookDir = XMVector3Normalize(lookDir);

  // 方向からピッチとヨーを計算
  XMFLOAT3 lookDirF;
  XMStoreFloat3(&lookDirF, lookDir);

  float yaw = atan2f(lookDirF.x, lookDirF.z);
  float pitch = -asinf(lookDirF.y);

  XMVECTOR q = XMQuaternionRotationRollPitchYaw(pitch, yaw, 0.0f);
  XMStoreFloat4(&camT->rotation, q);

  // 初回のみログ
  static bool loggedOnce = false;
  if (!loggedOnce) {
    LOG_DEBUG("WikiGolf", "UpdateCamera: cam=({}, {}, {}), ball=({}, {}, {})",
              camT->position.x, camT->position.y, camT->position.z,
              ballT->position.x, ballT->position.y, ballT->position.z);
    loggedOnce = true;
  }
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

  auto *camT = ctx.world.Get<Transform>(m_cameraEntity);
  auto *ballT = ctx.world.Get<Transform>(m_ballEntity);

  // === フェーズ別処理 ===

  switch (shot->phase) {
  case ShotState::Phase::Idle: {
    // 判定結果クリア
    auto *judgeUI = ctx.world.Get<UIImage>(state->judgeEntity);
    if (judgeUI)
      judgeUI->visible = false;

    // 左クリックでパワーゲージ開始 (UIクリックでなければ)
    bool uiClicked = false;
    if (ctx.input.GetMouseButtonDown(0)) {
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

      auto *infoUI = ctx.world.Get<UIText>(state->infoEntity);
      if (infoUI) {
        infoUI->text = L"[パワー] 左クリックで決定 / 右クリックでキャンセル";
      }

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

      // 軌道予測更新
      UpdateTrajectory(ctx, shot->powerGaugePos);
    }

    // クリックでパワー確定→インパクトへ
    if (ctx.input.GetMouseButtonDown(0)) {
      shot->confirmedPower = shot->powerGaugePos;
      shot->phase = ShotState::Phase::ImpactTiming;
      shot->impactGaugePos = 0.0f;
      shot->impactGaugeDir = 1.0f;
      LOG_INFO("WikiGolf", "Power confirmed: {:.1f}%",
               shot->confirmedPower * 100.0f);

      if (infoUI) {
        infoUI->text = L"[インパクト] 赤いゾーンで止めろ！";
      }
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
      if (std::abs(offset) < 0.05f)
        indicator = L"★ GREAT ★";
      else if (std::abs(offset) < 0.15f)
        indicator = L"◎ NICE ◎";
      else
        indicator = L"○";
      infoUI->text = L"[インパクト] " + indicator;
    }

    // マーカー移動
    auto *markerUI = ctx.world.Get<UIImage>(state->gaugeMarkerEntity);
    if (markerUI) {
      markerUI->x = 440.0f + shot->impactGaugePos * 400.0f;

      UpdateTrajectory(ctx, shot->confirmedPower);
    }

    // クリックでインパクト確定→ショット実行
    if (ctx.input.GetMouseButtonDown(0)) {
      shot->confirmedImpact = shot->impactGaugePos;

      // 判定計算
      float impactError = std::abs(shot->confirmedImpact - 0.5f);
      if (impactError < 0.05f) {
        shot->judgement = ShotJudgement::Great;
      } else if (impactError < 0.15f) {
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
  case ShotJudgement::Great:
    powerMultiplier = 1.1f; // ボーナス
    curveAmount = 0.0f;
    break;
  case ShotJudgement::Nice:
    powerMultiplier = 1.0f;
    curveAmount = impactError * 0.3f; // 少し曲がる
    break;
  case ShotJudgement::Miss:
    powerMultiplier = 0.8f;           // 減少
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

    LOG_INFO("WikiGolf", "Shot: power={:.1f}, club={}, angle={:.1f}", power,
             m_currentClub.name, m_currentClub.launchAngle);

    // === Game Juice: インパクト演出 ===
    if (m_gameJuice) {
      // ボール位置でインパクトエフェクト発火
      auto *ballT = ctx.world.Get<Transform>(m_ballEntity);
      if (ballT) {
        float normalizedPower = shot->confirmedPower; // 0.0〜1.0
        m_gameJuice->TriggerImpactEffect(ctx, ballT->position, normalizedPower);
      }

      // カメラシェイク（パワーに応じて強度調整）
      float shakeIntensity = 0.1f + shot->confirmedPower * 0.4f;
      float shakeDuration = 0.15f + shot->confirmedPower * 0.15f;
      m_gameJuice->TriggerCameraShake(shakeIntensity, shakeDuration);

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

  // マーカー非表示
  auto *markerUI = ctx.world.Get<UIImage>(state->gaugeMarkerEntity);
  if (markerUI)
    markerUI->visible = false;

  // 判定表示用エンティティ取得
  auto *judgeUI = ctx.world.Get<UIImage>(state->judgeEntity);

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
  if (infoUI) {
    std::string judgeTexPath;
    switch (shot->judgement) {
    case ShotJudgement::Great:
      judgeTexPath = "ui_judge_great.png";
      break;
    case ShotJudgement::Nice:
      judgeTexPath = "ui_judge_nice.png";
      break;
    case ShotJudgement::Miss:
      judgeTexPath = "ui_judge_miss.png";
      break;
    default:
      break;
    }

    if (judgeUI && !judgeTexPath.empty()) {
      judgeUI->texturePath = judgeTexPath;
      judgeUI->visible = true;
    }
  }
}

void WikiGolfScene::CreateHole(core::GameContext &ctx, float x, float z,
                               const std::string &targetPage, bool isTarget) {
  auto e = ctx.world.CreateEntity();
  auto &t = ctx.world.Add<Transform>(e);
  t.position = {x, -0.4f, z};
  t.scale = {0.15f, 0.01f, 0.15f};

  auto &mr = ctx.world.Add<MeshRenderer>(e);
  mr.mesh = ctx.resource.LoadMesh("builtin/cylinder");
  mr.shader = ctx.resource.LoadShader("Basic", L"shaders/BasicVS.hlsl",
                                      L"shaders/BasicPS.hlsl");
  if (isTarget) {
    mr.color = {1.0f, 0.0f, 0.0f, 0.8f};
  } else {
    mr.color = {0.0f, 0.0f, 1.0f, 0.5f};
  }

  auto &hole = ctx.world.Add<GolfHole>(e);
  hole.radius = 0.3f;
  hole.linkTarget = targetPage; // 修正点：targetPage -> linkTarget
  hole.isTarget = isTarget;
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

  auto *camT = ctx.world.Get<Transform>(m_cameraEntity);
  if (camT) {
    m_cameraDistance = 15.0f;
    m_targetCameraDistance = 15.0f;
    m_shotDirection = {0, 0, 1};
  }

  auto *ballT = ctx.world.Get<Transform>(m_ballEntity);
  if (ballT) {
    ballT->position = {0.0f, 1.0f, -8.0f};
    auto *rb = ctx.world.Get<RigidBody>(m_ballEntity);
    if (rb)
      rb->velocity = {0, 0, 0};
  }
  // トレイルリセット（遷移時）
  if (m_gameJuice) {
    m_gameJuice->ResetTrail();
  }

  LoadPage(ctx, pageName);
  if (ctx.audio)
    ctx.audio->PlaySE(ctx, "se_warp.mp3");
}

void WikiGolfScene::OnUpdate(core::GameContext &ctx) {
  auto *state = ctx.world.GetGlobal<GolfGameState>();

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

  // 右クリックドラッグで視点回転 (通常時) / パン (マップ時)
  int mouseX = ctx.input.GetMousePosition().x;
  int mouseY = ctx.input.GetMousePosition().y;

  if (m_isMapView) {
    if (ctx.input.GetMouseButton(1)) {
      int deltaX = mouseX - m_prevMouseX;
      int deltaY = mouseY - m_prevMouseY;

      float sensitivity = 0.05f * m_mapZoom;
      m_mapCenterOffset.x -= deltaX * sensitivity;
      m_mapCenterOffset.z += deltaY * sensitivity;
    }
  } else {
    // 通常ビュー: 視点回転
    auto *shotStateCheck = ctx.world.GetGlobal<ShotState>();
    if (shotStateCheck && shotStateCheck->phase == ShotState::Phase::Idle) {
      if (ctx.input.GetMouseButton(1)) {
        // 右クリック中
        if (ctx.input.GetMouseButtonDown(1)) {
          // 押し始め
        } else {
          // ドラッグ中：差分計算
          int deltaX = mouseX - m_prevMouseX;
          // deltaY は通常ビューでは使わない

          if (deltaX != 0) {
            float sensitivity = 0.005f;
            float angle = deltaX * sensitivity;
            // ...
            XMVECTOR dir = XMLoadFloat3(&m_shotDirection);
            XMVECTOR q = XMQuaternionRotationRollPitchYaw(0, angle, 0);
            dir = XMVector3Rotate(dir, q);
            dir = XMVector3Normalize(dir);
            XMStoreFloat3(&m_shotDirection, dir);
          }
        }
      }
    }
  }

  m_prevMouseX = mouseX;
  m_prevMouseY = mouseY;

  // マップビュー時のズーム（+/-キー）
  if (m_isMapView) {
    if (ctx.input.GetKeyDown(VK_OEM_PLUS) || ctx.input.GetKeyDown(VK_ADD)) {
      m_mapZoom = std::clamp(m_mapZoom - 0.1f, 0.3f, 2.0f);
    }
    if (ctx.input.GetKeyDown(VK_OEM_MINUS) ||
        ctx.input.GetKeyDown(VK_SUBTRACT)) {
      m_mapZoom = std::clamp(m_mapZoom + 0.1f, 0.3f, 2.0f);
    }
  }

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
      ballT->position = {0.0f, 1.0f, -8.0f}; // スタート位置に戻す
      if (rb) {
        rb->velocity = {0, 0, 0};
      }
      state->canShoot = true;
      state->shotCount++; // ペナルティ

      auto *infoUI = ctx.world.Get<UIText>(state->infoEntity);
      if (infoUI) {
        infoUI->text = L"⚠️ OB! リスポーン";
      }
      LOG_INFO("WikiGolf", "Ball respawned (fell off)");
    }

    if (rb) {
      float speed = std::sqrt(rb->velocity.x * rb->velocity.x +
                              rb->velocity.y * rb->velocity.y +
                              rb->velocity.z * rb->velocity.z);
      if (speed < 0.1f && !state->canShoot) {
        state->canShoot = true;
        rb->velocity = {0, 0, 0};

        auto *infoUI = ctx.world.Get<UIText>(state->infoEntity);
        if (infoUI) {
          infoUI->text = L"ドラッグでショット";
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

  // ホールイン判定
  if (ballT) {
    ctx.world.Query<Transform, GolfHole>().Each([&](ecs::Entity, Transform &ht,
                                                    GolfHole &hole) {
      float dx = ballT->position.x - ht.position.x;
      float dz = ballT->position.z - ht.position.z;
      float dist = std::sqrt(dx * dx + dz * dz);
      if (dist < hole.radius) {
        // ホールイン！
        LOG_INFO("WikiGolf", "Hole in! -> {}", hole.linkTarget);

        if (ctx.audio) {
          ctx.audio->PlaySE(ctx, "se_cupin.mp3");
        }

        if (hole.isTarget) {
          // BGM
          if (ctx.audio)
            ctx.audio->PlayBGM(ctx, "bgm_result.mp3");

          // クリア！
          state->gameCleared = true;
          auto *infoUI = ctx.world.Get<UIText>(state->infoEntity);
          if (infoUI) {
            infoUI->text = L"🎉 クリア！ 打数: " +
                           std::to_wstring(state->shotCount) + L" / 遷移: " +
                           std::to_wstring(state->moveCount);
          }

          // 結果画面UI表示
          auto *resultText = ctx.world.Get<UIText>(state->resultTextEntity);
          if (resultText) {
            std::wstring result = L"🎉 クリア！\n\n";
            result += L"🏌️ 打数: " + std::to_wstring(state->shotCount);
            result += L" (Par " + std::to_wstring(state->par) + L")\n";
            result +=
                L"📍 遷移: " + std::to_wstring(state->moveCount) + L"回\n";
            int diff = state->shotCount - state->par;
            if (diff < 0) {
              result += L"🌟 " + std::to_wstring(-diff) + L"アンダー！";
            } else if (diff == 0) {
              result += L"✅ パー！";
            } else {
              result += L"+" + std::to_wstring(diff);
            }
            resultText->text = result;
            resultText->visible = true;
          }

          auto *retryBtn = ctx.world.Get<UIButton>(state->retryButtonEntity);
          if (retryBtn) {
            retryBtn->visible = true;
          }

          // ハイスコア保存
          SaveHighScore(state->targetPage, state->shotCount);
        } else {
          // 遷移
          TransitionToPage(ctx, hole.linkTarget);
        }
      }
    });
  }
}

void WikiGolfScene::OnExit(core::GameContext &ctx) {}

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

  // 初期位置（ボール位置）
  XMVECTOR prevPos = pos;

  for (size_t i = 0; i < m_trajectoryDots.size(); ++i) {
    auto e = m_trajectoryDots[i];
    auto *t = ctx.world.Get<Transform>(e);
    auto *mr = ctx.world.Get<MeshRenderer>(e);

    if (!mr || !t)
      continue;

    // --- 物理ステップ (簡易オイラー積分) ---
    // 空気抵抗 (v *= 1 - drag * dt)
    vel = XMVectorScale(vel, (1.0f - drag * dt));
    // 重力 (v += g * dt)
    vel = XMVectorAdd(vel, XMVectorScale(gravity, dt));
    // 位置 (p += v * dt)
    XMVECTOR currentPos = XMVectorAdd(prevPos, XMVectorScale(vel, dt));

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
    float baseThickness = 0.15f; // ベースの太さを3倍に (0.05 -> 0.15)
    if (m_isMapView) {
      baseThickness *= 3.0f; // マップビュー時はさらに3倍して強調
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
  m_availableClubs.push_back(
      {"Driver", 30.0f, 30.0f, "Assets/icon_driver.png"});
  m_availableClubs.push_back({"Iron", 20.0f, 45.0f, "Assets/icon_iron.png"});
  m_availableClubs.push_back({"Putter", 10.0f, 5.0f, "Assets/icon_putter.png"});

  // UI作成
  for (size_t i = 0; i < m_availableClubs.size(); ++i) {
    auto e = ctx.world.CreateEntity();

    // アイコン画像
    auto &img = ctx.world.Add<UIImage>(e);
    // texturePathを設定
    img = UIImage::Create(m_availableClubs[i].iconTexture, 0, 0);

    // 画面下部、中央揃え
    float startX = 1280.0f / 2.0f - (m_availableClubs.size() * 100.0f) / 2.0f;
    img.x = startX + i * 100.0f;
    img.y = 600.0f;
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
    m_minimapEntity = ctx.world.CreateEntity();
    auto &ui = ctx.world.Add<UIImage>(m_minimapEntity);
    ui.textureSRV = m_minimapRenderer->GetSRV();
    ui.width = 200.0f;
    ui.height = 200.0f;
    ui.x = 1280.0f - 220.0f; // 画面右端から20px余裕
    ui.y = 20.0f;
    ui.visible = true;
    ui.layer = 100; // 手前に表示
  }

  // Header
  auto headerE = ctx.world.CreateEntity();
  auto &ht = ctx.world.Add<UIText>(headerE);
  ht.text = L"Loading..."; // 初期値
  ht.x = 10;
  ht.y = 10;
  ht.style = graphics::TextStyle::ModernBlack();
  ht.visible = true;
  ht.layer = 10;
  state.headerEntity = headerE;

  // Shot HUD
  auto shotE = ctx.world.CreateEntity();
  auto &st = ctx.world.Add<UIText>(shotE);
  st.text = L""; // 初期化はUpdateHUDで行う
  st.x = 10;
  st.y = 40;
  st.style = graphics::TextStyle::ModernBlack();
  st.visible = true;
  st.layer = 10;
  state.shotCountEntity = shotE;

  // Info
  auto infoE = ctx.world.CreateEntity();
  auto &it = ctx.world.Add<UIText>(infoE);
  it.text = L"クリックでショット";
  it.x = 10;
  it.y = 680;
  it.style = graphics::TextStyle::ModernBlack();
  it.visible = true;
  it.layer = 10;
  state.infoEntity = infoE;

  // Wind UI
  auto windE = ctx.world.CreateEntity();
  auto &wt = ctx.world.Add<UIText>(windE);
  wt.x = 1100;
  wt.y = 10;
  wt.visible = true;
  wt.layer = 10;
  state.windEntity = windE;

  // Wind Arrow
  LOG_INFO("WikiGolf", "Creating wind arrow UI...");
  auto windArrowE = ctx.world.CreateEntity();
  auto &wa = ctx.world.Add<UIImage>(windArrowE);
  wa = UIImage::Create("ui_wind_arrow.png", 1120.0f, 40.0f);
  wa.width = 64.0f;
  wa.height = 64.0f;
  wa.visible = true;
  wa.layer = 10;
  state.windArrowEntity = windArrowE;

  // Gauge (Bg, Fill, Marker)
  LOG_INFO("WikiGolf", "Creating gauge UI...");
  auto gaugeBarE = ctx.world.CreateEntity();
  auto &gb = ctx.world.Add<UIImage>(gaugeBarE);
  gb = UIImage::Create("ui_gauge_bg.png", 440.0f, 650.0f);
  gb.width = 400.0f;
  gb.height = 30.0f;
  gb.visible = true;
  gb.layer = 10;
  state.gaugeBarEntity = gaugeBarE;

  auto gaugeFillE = ctx.world.CreateEntity();
  auto &gf = ctx.world.Add<UIImage>(gaugeFillE);
  gf = UIImage::Create("ui_gauge_fill.png", 450.0f, 655.0f);
  gf.width = 0.0f; // 最初は0
  gf.height = 20.0f;
  gf.visible = true;
  gf.layer = 11;
  state.gaugeFillEntity = gaugeFillE;

  auto markerE = ctx.world.CreateEntity();
  auto &gm = ctx.world.Add<UIImage>(markerE);
  gm = UIImage::Create("ui_gauge_marker.png", 450.0f, 645.0f);
  gm.width = 16.0f;
  gm.height = 32.0f;
  gm.visible = false;
  gm.layer = 12;
  state.gaugeMarkerEntity = markerE;

  // Path History
  auto pathE = ctx.world.CreateEntity();
  auto &pathT = ctx.world.Add<UIText>(pathE);
  pathT.text = L"History: ";
  pathT.x = 10;
  pathT.y = 100;
  pathT.visible = true;
  pathT.layer = 10;
  pathT.style = graphics::TextStyle::ModernBlack();
  pathT.style.fontSize = 16.0f;
  state.pathEntity = pathE;

  // Judge Result
  LOG_INFO("WikiGolf", "Creating judge UI...");
  auto judgeE = ctx.world.CreateEntity();
  auto &ji = ctx.world.Add<UIImage>(judgeE);
  ji = UIImage::Create("ui_judge_great.png", 490.0f, 300.0f);
  ji.width = 300.0f;
  ji.height = 100.0f;
  ji.visible = false;
  ji.layer = 20;
  state.judgeEntity = judgeE;

  // Result Screen UI
  LOG_INFO("WikiGolf", "Creating result UI...");
  auto resultBgE = ctx.world.CreateEntity();
  auto &rbg = ctx.world.Add<UIImage>(resultBgE);
  rbg = UIImage::Create("ui_gauge_bg.png", 0, 0);
  rbg.width = 1280.0f;
  rbg.height = 720.0f;
  rbg.alpha = 0.8f;
  rbg.visible = false;
  rbg.layer = 50;
  state.resultBgEntity = resultBgE;

  auto resultTextE = ctx.world.CreateEntity();
  auto &rt = ctx.world.Add<UIText>(resultTextE);
  rt.x = 400.0f;
  rt.y = 250.0f;
  rt.text = L"🎉 クリア！";
  rt.visible = false;
  rt.layer = 51;
  rt.style.fontSize = 48.0f;
  rt.style.color = {1.0f, 0.84f, 0.0f, 1.0f}; // 金色
  state.resultTextEntity = resultTextE;

  auto retryBtnE = ctx.world.CreateEntity();
  auto &btn = ctx.world.Add<UIButton>(retryBtnE);
  btn = UIButton::Create(L"もう一度", "retry", 540.0f, 400.0f, 200.0f, 50.0f);
  btn.visible = false;
  state.retryButtonEntity = retryBtnE;
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
    std::vector<game::systems::WikiLink> allLinks;
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
    const float minFieldWidth = 20.0f;
    const float minFieldDepth = 30.0f;
    float articleLengthFactor =
        std::max(1.0f, (float)articleText.length() / 1000.0f);
    float fieldWidth = minFieldWidth * std::sqrt(articleLengthFactor);
    float fieldDepth = minFieldDepth * std::sqrt(articleLengthFactor);

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
    const float texWidthF = (float)m_wikiTexture->width;
    const float texHeightF = (float)m_wikiTexture->height;

    for (const auto &linkRegion : m_wikiTexture->links) {
      float texCenterX = linkRegion.x + linkRegion.width * 0.5f;
      float texCenterY = linkRegion.y + linkRegion.height * 0.5f;
      float worldX = (texCenterX / texWidthF - 0.5f) * fieldWidth;
      float worldZ = (0.5f - texCenterY / texHeightF) * fieldDepth;

      CreateHole(ctx, worldX, worldZ, linkRegion.targetPage,
                 linkRegion.isTarget);
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
      shotUI->text = L"打数: " + std::to_wstring(state->shotCount) +
                     L" / Par " + std::to_wstring(state->par) + suffix;
      LOG_INFO("WikiGolf", "Updated HUD text: {}",
               core::ToString(shotUI->text));
    }
  }

} // namespace game::scenes
