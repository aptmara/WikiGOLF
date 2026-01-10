/**
 * @file LoadingScene.cpp
 * @brief ローディング画面シーン（ゴルフボール物理演出）の実装
 */

#include "LoadingScene.h"
#include "../../core/GameContext.h"
#include "../../core/Input.h"
#include "../../core/Logger.h"
#include "../../core/SceneManager.h"
#include "../components/Camera.h"
#include "../components/MeshRenderer.h"
#include "../components/Transform.h"
#include "../components/UIText.h"
#include "../systems/WikiClient.h"
#include "../systems/WikiShortestPath.h"
#include "LoadingSceneUtils.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <random>
#include <thread>

namespace game::scenes {

LoadingScene::LoadingScene(
    std::function<std::unique_ptr<core::Scene>()> nextSceneFactory)
    : m_nextSceneFactory(std::move(nextSceneFactory)) {}

void LoadingScene::OnEnter(core::GameContext &ctx) {
  LOG_INFO("LoadingScene", "OnEnter");

  m_balls.clear();
  m_spawnedCount = 0;
  m_spawnTimer = 0.0f;
  m_fadeAlpha = 0.0f;
  m_fadeStarted = false;
  m_fadeDelay = 0.6f;
  m_sceneTime = 0.0f;
  m_tipTimer = 0.0f;
  m_tipIndex = 0;
  m_cameraTime = 0.0f;
  m_logTimer = 0.0f;
  m_spawnFinishedLogged = false;
  m_allSettledLogged = false;
  m_fadeLogged = false;
  m_movingCount = 0;
  m_maxSpeed = 0.0f;
  m_avgSpeed = 0.0f;
  m_settledCount = 0;
  m_lastSettledCount = 0;
  m_stuckTimer = 0.0f;
  m_lastMovingPos = {0.0f, 0.0f, 0.0f};
  m_hasMovingSample = false;
  m_forceFinishTimer = 0.0f;
  m_isLoading = false;
  m_loadCompleted = false;
  m_uiProgress = 0.0f;
  m_explosionTimer = 0.0f;
  m_exploded = false;
  m_loadProgress = std::make_shared<std::atomic<float>>(0.0f);

  // 非同期ロード開始
  m_isLoading = true;
  auto progressPtr = m_loadProgress;
  m_loadTask = std::async(std::launch::async, [progressPtr]() {
    const auto setProgress = [progressPtr](float value) {
      if (progressPtr) {
        progressPtr->store(std::clamp(value, 0.0f, 1.0f),
                           std::memory_order_relaxed);
      }
    };

    setProgress(0.02f);
    auto data = std::make_unique<game::components::WikiGlobalData>();

    // WikiShortestPathの初期化（重い処理）
    data->pathSystem = std::make_unique<game::systems::WikiShortestPath>();
    bool dbLoaded =
        data->pathSystem->Initialize("Assets/data/jawiki_sdow.sqlite");
    setProgress(0.3f);
    if (!dbLoaded) {
      // ログは別スレッドからは注意が必要だが、ここでは簡易的に
    }

    // スタート・ゴール選定
    game::systems::WikiClient wikiClient;
    data->startPage = wikiClient.FetchRandomPageTitle();
    setProgress(0.45f);

    // 人気記事からターゲット選定
    if (dbLoaded && data->pathSystem->IsAvailable()) {
      auto result = data->pathSystem->FetchPopularPageTitle(100);
      data->targetPage = result.first;
      data->targetPageId = result.second;
      setProgress(0.7f);
      if (data->targetPage.empty()) {
        result = data->pathSystem->FetchPopularPageTitle(50);
        data->targetPage = result.first;
        data->targetPageId = result.second;
      }
    }

    // フォールバック
    if (data->targetPage.empty()) {
      data->targetPage = wikiClient.FetchTargetPageTitle();
      data->targetPageId = -1;
      setProgress(0.8f);
    }

    if (data->startPage == data->targetPage) {
      data->targetPage = wikiClient.FetchTargetPageTitle();
      data->targetPageId = -1;
      setProgress(0.82f);
    }

    // 最短1記事（または同一）のターゲットは再抽選
    if (dbLoaded && data->pathSystem && data->pathSystem->IsAvailable() &&
        !data->startPage.empty() && !data->targetPage.empty()) {
      const int maxRetry = 5;
      for (int attempt = 0; attempt < maxRetry; ++attempt) {
        game::systems::ShortestPathResult pathResult;
        if (data->targetPageId != -1) {
          pathResult = data->pathSystem->FindShortestPath(
              data->startPage, data->targetPageId, 6);
        } else {
          pathResult = data->pathSystem->FindShortestPath(data->startPage,
                                                          data->targetPage, 6);
        }

        if (!pathResult.success || pathResult.degrees > 1) {
          break; // 計算失敗時もここで抜ける
        }

        auto newTarget = data->pathSystem->FetchPopularPageTitle(100);
        if (newTarget.first.empty()) {
          newTarget = data->pathSystem->FetchPopularPageTitle(50);
        }

        if (newTarget.first.empty()) {
          break; // 取得できなければ諦める
        }

        data->targetPage = newTarget.first;
        data->targetPageId = newTarget.second;
      }
    }

    // パー計算（ついでにやっておく）
    if (dbLoaded && data->targetPageId != -1) {
      // スタート位置のIDがわからないので正確には計算できないが、
      // ここではロードの重さを吸収するのが目標なのでOK
      // IdFromTitleなどはDBアクセスが必要
    }

    // 初回ページのデータを先行ロード（通信ラグ解消）
    if (!data->startPage.empty()) {
      data->cachedLinks = wikiClient.FetchPageLinks(data->startPage, 100);
      data->cachedExtract = wikiClient.FetchPageExtract(data->startPage, 5000);
      data->hasCachedData = true;
      setProgress(0.97f);
    }

    setProgress(1.0f);
    return data;
  });

  // マウスカーソルを非表示
  ctx.input.SetMouseCursorVisible(false);

  // ゴルフボールメッシュをロード
  m_ballMeshHandle = ctx.resource.LoadMesh("Assets/models/golfball.fbx");

  // カメラを作成
  m_cameraEntity = CreateEntity(ctx.world);
  auto &camTr = ctx.world.Add<components::Transform>(m_cameraEntity);
  camTr.position = {0.0f, 6.0f, -68.0f};
  camTr.rotation = {0.0f, 0.0f, 0.0f, 1.0f}; // 軽く俯瞰させるための基準

  auto &cam = ctx.world.Add<components::Camera>(m_cameraEntity);
  cam.fov = DirectX::XM_PIDIV4; // 45度
  cam.nearZ = 0.1f;
  cam.farZ = 100.0f;
  cam.isMainCamera = true;

  // 床と壁を生成
  CreateBoundaries(ctx);

  // UIスタイル構築
  m_primaryStyle = graphics::TextStyle::Title();
  m_primaryStyle.fontSize = 46.0f;
  m_primaryStyle.align = graphics::TextAlign::Center;
  m_primaryStyle.color = {0.96f, 0.98f, 1.0f, 1.0f};
  m_primaryStyle.outlineColor = {0.07f, 0.18f, 0.35f, 0.85f};
  m_primaryStyle.outlineWidth = 2.2f;
  m_primaryStyle.hasShadow = true;
  m_primaryStyle.shadowColor = {0.0f, 0.0f, 0.0f, 0.7f};
  m_primaryStyle.shadowOffsetX = 2.5f;
  m_primaryStyle.shadowOffsetY = 2.5f;

  m_progressStyle = graphics::TextStyle::ModernBlack();
  m_progressStyle.fontSize = 28.0f;
  m_progressStyle.align = graphics::TextAlign::Center;
  m_progressStyle.color = {0.1f, 0.45f, 0.6f, 1.0f};
  m_progressStyle.hasShadow = true;
  m_progressStyle.shadowColor = {0.0f, 0.0f, 0.0f, 0.6f};

  m_captionStyle = graphics::TextStyle::ModernBlack();
  m_captionStyle.fontSize = 20.0f;
  m_captionStyle.align = graphics::TextAlign::Center;
  m_captionStyle.color = {0.2f, 0.2f, 0.25f, 0.9f};
  m_captionStyle.hasShadow = true;
  m_captionStyle.shadowColor = {0.0f, 0.0f, 0.0f, 0.4f};

  // メインタイトル
  m_textEntity = CreateEntity(ctx.world);
  auto &titleText = ctx.world.Add<components::UIText>(m_textEntity);
  titleText.text = L"WIKI GOLF LOADING";
  titleText.x = 0.0f;
  titleText.y = 110.0f;
  titleText.width = 1280.0f;
  titleText.style = m_primaryStyle;
  titleText.visible = true;
  titleText.layer = 12;

  // 進行状況
  m_progressTextEntity = CreateEntity(ctx.world);
  auto &progressText = ctx.world.Add<components::UIText>(m_progressTextEntity);
  progressText.text = L"0%";
  progressText.x = 0.0f;
  progressText.y = 170.0f;
  progressText.width = 1280.0f;
  progressText.style = m_progressStyle;
  progressText.visible = true;
  progressText.layer = 12;

  // キャプション（ tips 巡回用 ）
  m_captionTextEntity = CreateEntity(ctx.world);
  auto &caption = ctx.world.Add<components::UIText>(m_captionTextEntity);
  caption.text = L"芝目をスキャン中...";
  caption.x = 0.0f;
  caption.y = 210.0f;
  caption.width = 1280.0f;
  caption.style = m_captionStyle;
  caption.visible = true;
  caption.layer = 11;

  // 最初のボールは即スポーンさせて動きを見せる
  SpawnBall(ctx);

  LOG_INFO("LoadingScene", "OnEnter complete");
}

void LoadingScene::CreateBoundaries(core::GameContext &ctx) {
  // シェーダーをロード
  auto shaderHandle = ctx.resource.LoadShader(
      "Basic", L"Assets/shaders/BasicVS.hlsl", L"Assets/shaders/BasicPS.hlsl");

  // 床（水槽の底）
  m_floorEntity = CreateEntity(ctx.world);
  auto &floorTr = ctx.world.Add<components::Transform>(m_floorEntity);
  floorTr.position = {0.0f, FLOOR_Y, 0.0f};
  floorTr.scale = {ARENA_HALF_WIDTH * 2.0f, 0.6f, ARENA_HALF_DEPTH * 2.4f};

  auto &floorMr = ctx.world.Add<components::MeshRenderer>(m_floorEntity);
  floorMr.mesh = ctx.resource.LoadMesh("builtin/cube");
  floorMr.shader = shaderHandle;
  floorMr.color = {0.08f, 0.14f, 0.2f, 1.0f}; // 深めのブルーグレー
  floorMr.isVisible = true;

  // 壁（水槽の左右と奥） - 2D的に見せるため薄い
  auto createWall = [&](DirectX::XMFLOAT3 pos, DirectX::XMFLOAT3 scale,
                        DirectX::XMFLOAT4 color, bool visible) {
    auto wallEntity = CreateEntity(ctx.world);
    auto &tr = ctx.world.Add<components::Transform>(wallEntity);
    tr.position = pos;
    tr.scale = scale;

    auto &mr = ctx.world.Add<components::MeshRenderer>(wallEntity);
    mr.mesh = ctx.resource.LoadMesh("builtin/cube");
    mr.shader = shaderHandle;
    mr.color = color;
    mr.isVisible = visible;

    m_wallEntities.push_back(wallEntity);
  };

  const float wallHeight = 60.0f;
  const float wallDepth = ARENA_HALF_DEPTH * 2.4f;

  // 左壁
  createWall({-ARENA_HALF_WIDTH, FLOOR_Y + wallHeight * 0.5f, 0.0f},
             {1.0f, wallHeight, wallDepth}, {0.2f, 0.45f, 0.65f, 0.32f}, true);
  // 右壁
  createWall({ARENA_HALF_WIDTH, FLOOR_Y + wallHeight * 0.5f, 0.0f},
             {1.0f, wallHeight, wallDepth}, {0.2f, 0.45f, 0.65f, 0.32f}, true);
  // 奥壁（透明ガラスの雰囲気）
  createWall({0.0f, FLOOR_Y + wallHeight * 0.5f, ARENA_HALF_DEPTH},
             {ARENA_HALF_WIDTH * 2.0f, wallHeight, 1.0f},
             {0.15f, 0.24f, 0.35f, 0.22f}, true);

  // 背景パネル
  m_backdropEntity = CreateEntity(ctx.world);
  auto &panelTr = ctx.world.Add<components::Transform>(m_backdropEntity);
  panelTr.position = {0.0f, 12.0f, ARENA_HALF_DEPTH + 10.0f};
  panelTr.scale = {ARENA_HALF_WIDTH * 2.6f, wallHeight * 0.8f, 2.0f};
  auto &panelMr = ctx.world.Add<components::MeshRenderer>(m_backdropEntity);
  panelMr.mesh = ctx.resource.LoadMesh("builtin/cube");
  panelMr.shader = shaderHandle;
  panelMr.color = {0.03f, 0.06f, 0.1f, 1.0f};
  panelMr.isVisible = true;
}

void LoadingScene::SpawnBall(core::GameContext &ctx) {
  // ランダム生成器
  static std::mt19937 rng(std::random_device{}());
  const float renderRadius = BALL_RADIUS * BALL_MODEL_SCALE;
  const float spawnHalfX = ARENA_HALF_WIDTH - renderRadius - 0.5f;
  const float spawnHalfZ = ARENA_HALF_DEPTH - renderRadius - 0.2f;
  std::uniform_real_distribution<float> distX(-spawnHalfX, spawnHalfX);
  std::uniform_real_distribution<float> distZ(-spawnHalfZ, spawnHalfZ);
  std::uniform_real_distribution<float> distHeight(24.0f, 40.0f);
  std::uniform_real_distribution<float> distVelX(-22.0f, 22.0f);
  std::uniform_real_distribution<float> distVelZ(-14.0f, 14.0f);
  std::uniform_real_distribution<float> spinDist(-4.0f, 4.0f);
  std::uniform_real_distribution<float> angleDist(-DirectX::XM_PI,
                                                  DirectX::XM_PI);

  auto entity = CreateEntity(ctx.world);

  // Transform
  auto &tr = ctx.world.Add<components::Transform>(entity);
  tr.position = {distX(rng), distHeight(rng), distZ(rng)};
  const float renderScale =
      BALL_RADIUS * 2.0f * BALL_MODEL_SCALE * MODEL_ASSET_SCALE_FACTOR;
  tr.scale = {renderScale, renderScale, renderScale};
  // 軽いランダム回転を入れておく
  auto randomRotation = DirectX::XMQuaternionRotationRollPitchYaw(
      angleDist(rng) * 0.25f, angleDist(rng) * 0.25f, angleDist(rng) * 0.25f);
  DirectX::XMStoreFloat4(&tr.rotation, randomRotation);

  // MeshRenderer
  auto &mr = ctx.world.Add<components::MeshRenderer>(entity);
  mr.mesh = m_ballMeshHandle;
  mr.shader = ctx.resource.LoadShader("Basic", L"Assets/shaders/BasicVS.hlsl",
                                      L"Assets/shaders/BasicPS.hlsl");

    // 色の決定（5%の確率で上品なネオンカラー）
    std::uniform_real_distribution<float> colorDist(0.0f, 1.0f);
    if (colorDist(rng) < 0.05f) {
        // ネオンパレット (RGB) - 控えめな発光
        const float intensity = 1.8f; 
        const std::array<DirectX::XMFLOAT3, 5> neonColors = {{
            {0.1f * intensity, 0.6f * intensity, 1.0f * intensity}, // Sky Blue
            {0.9f * intensity, 0.2f * intensity, 0.6f * intensity}, // Rose
            {1.0f * intensity, 0.8f * intensity, 0.1f * intensity}, // Amber
            {0.2f * intensity, 0.9f * intensity, 0.4f * intensity}, // Soft Emerald
            {0.6f * intensity, 0.3f * intensity, 1.0f * intensity}  // Violet
        }};
        std::uniform_int_distribution<size_t> paletteDist(0, neonColors.size() - 1);
        DirectX::XMFLOAT3 c = neonColors[paletteDist(rng)];
        mr.color = {c.x, c.y, c.z, 1.0f};
    } else {
        // ゴルフボールらしい白色
        mr.color = {1.0f, 1.0f, 1.0f, 1.0f};
    }
  mr.isVisible = true;
  mr.normalMapSRV = ctx.resource.LoadTextureSRV("Assets/models/golfball_n.png");
  mr.hasNormalMap = static_cast<bool>(mr.normalMapSRV);

  // ボール状態を記録
  BallState ball{};
  ball.entity = entity;
  ball.velocity = {distVelX(rng), -16.0f, distVelZ(rng)};
  ball.angularVelocity = {spinDist(rng) * 0.8f, spinDist(rng),
                          spinDist(rng) * 0.6f};
  ball.settled = false;
  m_balls.push_back(ball);

  m_spawnedCount++;
}

void LoadingScene::UpdatePhysics(core::GameContext &ctx, float dt) {
  const float renderRadius = BALL_RADIUS * BALL_MODEL_SCALE;
  const float floorY = FLOOR_Y + 0.3f + renderRadius;
  const float wallX = ARENA_HALF_WIDTH - renderRadius;
  const float wallZ = ARENA_HALF_DEPTH - renderRadius;

  // サブステップ数（貫通防止のため分割実行）
  const int subSteps = 8;
  const float subDt = dt / subSteps;

  for (int step = 0; step < subSteps; ++step) {
    for (auto &ball : m_balls) {
      if (!ctx.world.IsAlive(ball.entity) || ball.settled) {
        continue;
      }

      auto *tr = ctx.world.Get<components::Transform>(ball.entity);
      if (!tr)
        continue;

      // 重力を適用
      ball.velocity.y += GRAVITY * subDt;

      // 空気抵抗
      float subDrag = std::pow(AIR_DRAG, subDt * 60.0f);
      ball.velocity.x *= subDrag;
      ball.velocity.z *= subDrag;

      // 位置を更新
      tr->position.x += ball.velocity.x * subDt;
      tr->position.y += ball.velocity.y * subDt;
      tr->position.z += ball.velocity.z * subDt;

      // 回転を更新
      DirectX::XMVECTOR currentRot = DirectX::XMLoadFloat4(&tr->rotation);
      DirectX::XMVECTOR deltaRot = DirectX::XMQuaternionRotationRollPitchYaw(
          ball.angularVelocity.x * subDt, ball.angularVelocity.y * subDt,
          ball.angularVelocity.z * subDt);
      auto nextRot = DirectX::XMQuaternionNormalize(
          DirectX::XMQuaternionMultiply(deltaRot, currentRot));
      DirectX::XMStoreFloat4(&tr->rotation, nextRot);
      float subAngDrag = std::pow(ANGULAR_DAMPING, subDt * 60.0f);
      ball.angularVelocity.x *= subAngDrag;
      ball.angularVelocity.y *= subAngDrag;
      ball.angularVelocity.z *= subAngDrag;

      // 床との衝突
      if (tr->position.y < floorY) {
        tr->position.y = floorY;
        if (ball.velocity.y < -0.5f) {
          ball.velocity.y = -ball.velocity.y * RESTITUTION;
        } else {
          ball.velocity.y = 0.0f;
        }
        const float subFriction = std::pow(FRICTION, subDt * 60.0f);
        ball.velocity.x *= subFriction;
        ball.velocity.z *= subFriction;
      }

      // 壁との衝突
      const float wallMinX = -wallX, wallMaxX = wallX;
      const float wallMinZ = -wallZ, wallMaxZ = wallZ;
      if (tr->position.x < wallMinX) {
        tr->position.x = wallMinX;
        ball.velocity.x = -ball.velocity.x * RESTITUTION;
      } else if (tr->position.x > wallMaxX) {
        tr->position.x = wallMaxX;
        ball.velocity.x = -ball.velocity.x * RESTITUTION;
      }
      if (tr->position.z < wallMinZ) {
        tr->position.z = wallMinZ;
        ball.velocity.z = -ball.velocity.z * RESTITUTION;
      } else if (tr->position.z > wallMaxZ) {
        tr->position.z = wallMaxZ;
        ball.velocity.z = -ball.velocity.z * RESTITUTION;
      }
    }
  }

  // ボール同士の衝突 (N^2だがサブステップ化で安定)
  for (size_t i = 0; i < m_balls.size(); i++) {
    auto &ball = m_balls[i];
    if (!ctx.world.IsAlive(ball.entity))
      continue;
    auto *tr = ctx.world.Get<components::Transform>(ball.entity);
    if (!tr)
      continue;

    for (size_t j = i + 1; j < m_balls.size(); j++) {
      auto &other = m_balls[j];
      if (!ctx.world.IsAlive(other.entity) || (ball.settled && other.settled))
        continue;

      auto *otherTr = ctx.world.Get<components::Transform>(other.entity);
      if (!otherTr)
        continue;

      float dx = otherTr->position.x - tr->position.x;
      float dy = otherTr->position.y - tr->position.y;
      float dz = otherTr->position.z - tr->position.z;
      float distSq = dx * dx + dy * dy + dz * dz;
      float minDist = renderRadius * 2.0f;

      if (distSq < minDist * minDist && distSq > 0.0001f) {
        float dist = std::sqrt(distSq);
        float overlap = (minDist - dist);
        float nx = dx / dist, ny = dy / dist, nz = dz / dist;

        float p1 = ball.settled ? 0.0f : (other.settled ? 1.0f : 0.5f);
        float p2 = 1.0f - p1;

        tr->position.x -= nx * overlap * p1;
        tr->position.y -= ny * overlap * p1;
        tr->position.z -= nz * overlap * p1;
        otherTr->position.x += nx * overlap * p2;
        otherTr->position.y += ny * overlap * p2;
        otherTr->position.z += nz * overlap * p2;

        float rvx = other.velocity.x - ball.velocity.x;
        float rvy = other.velocity.y - ball.velocity.y;
        float rvz = other.velocity.z - ball.velocity.z;
        float velAlongNormal = rvx * nx + rvy * ny + rvz * nz;

        if (velAlongNormal < 0) {
          float jVal = -(1 + RESTITUTION) * velAlongNormal * 0.5f;
          float ix = nx * jVal, iy = ny * jVal, iz = nz * jVal;
          if (!ball.settled) {
            ball.velocity.x -= ix; ball.velocity.y -= iy; ball.velocity.z -= iz;
          }
          if (!other.settled) {
            other.velocity.x += ix; other.velocity.y += iy; other.velocity.z += iz;
          }
          if (jVal > 0.5f) { ball.settled = other.settled = false; }
        }
      }
    }

    float speedSq = ball.velocity.x * ball.velocity.x + ball.velocity.y * ball.velocity.y + ball.velocity.z * ball.velocity.z;
    if (speedSq < SETTLE_THRESHOLD * SETTLE_THRESHOLD * 0.5f && tr->position.y <= floorY + 0.1f) {
      ball.settled = true;
      ball.velocity = {0,0,0}; ball.angularVelocity = {0,0,0};
    } else {
      ball.settled = false;
    }
  }

  // ログ用メトリクス
  int settledCount = 0;
  int moving = 0;
  float maxSpeed = 0.0f, speedAccum = 0.0f;
  int speedCount = 0;
  m_hasMovingSample = false;
  for (const auto &ball : m_balls) {
    if (!ctx.world.IsAlive(ball.entity)) continue;
    if (ball.settled) settledCount++;
    else {
      moving++;
      float speed = std::sqrt(ball.velocity.x * ball.velocity.x + ball.velocity.y * ball.velocity.y + ball.velocity.z * ball.velocity.z);
      maxSpeed = std::max(maxSpeed, speed);
      speedAccum += speed;
      speedCount++;
      if (!m_hasMovingSample) {
        if (auto *tr = ctx.world.Get<components::Transform>(ball.entity)) {
            m_lastMovingPos = tr->position;
            m_hasMovingSample = true;
        }
      }
    }
  }
  m_movingCount = moving; m_maxSpeed = maxSpeed; m_settledCount = settledCount;
  m_avgSpeed = (speedCount > 0) ? speedAccum / speedCount : 0.0f;
}

bool LoadingScene::AreAllBallsSettled() {
  if (m_spawnedCount < TOTAL_BALLS) return false;
  for (const auto &ball : m_balls) { if (!ball.settled) return false; }
  if (!m_allSettledLogged) { LOG_INFO("LoadingScene", "All balls settled"); m_allSettledLogged = true; }
  return true;
}

void LoadingScene::UpdateFade(core::GameContext &ctx, float dt) {
  if (!m_fadeStarted) {
    m_fadeStarted = true;
    LOG_INFO("LoadingScene", "Fade started");
    return;
  }

  m_fadeAlpha += FADE_SPEED * dt;
  if (m_fadeAlpha >= 1.0f) {
    m_fadeAlpha = 1.0f;

    // ロード結果を取得してグローバルにセット
    if (m_loadTask.valid()) {
      try {
        auto data = m_loadTask.get();
        LOG_INFO("LoadingScene", "Async load completed. Start: {}, Target: {}",
                 data->startPage, data->targetPage);
        ctx.world.SetGlobal(std::move(*data));
      } catch (const std::exception &e) {
        LOG_ERROR("LoadingScene", "Load task failed: {}", e.what());
      }
    }

    // 次のシーンへ遷移
    if (ctx.sceneManager && m_nextSceneFactory) {
      LOG_INFO("LoadingScene", "Explosion finished, switching scene");
      ctx.sceneManager->ChangeScene(m_nextSceneFactory());
    }
  }
}

void LoadingScene::UpdateCamera(core::GameContext &ctx, float dt) {
  auto *tr = ctx.world.Get<components::Transform>(m_cameraEntity);
  if (!tr)
    return;

  m_cameraTime += dt;

  const float sway = std::sin(m_cameraTime * 0.55f) * 6.0f;
  const float bob = std::sin(m_cameraTime * 1.1f) * 1.6f;
  const float dolly = std::cos(m_cameraTime * 0.35f) * 2.5f;

  tr->position.x = sway * 0.6f;
  tr->position.y = 6.0f + bob;
  tr->position.z = -68.0f + dolly;

  auto camRot = DirectX::XMQuaternionRotationRollPitchYaw(-0.12f + bob * 0.01f,
                                                          sway * 0.003f, 0.0f);
  DirectX::XMStoreFloat4(&tr->rotation, camRot);
}

void LoadingScene::UpdateUI(core::GameContext &ctx) {
  const std::array<std::wstring, 4> tips = {
      L"芝目をスキャン中...", L"Wikipediaの芝刈り準備中",
      L"ショットの風向きをプレビュー中", L"リンクをフェアウェイに整地中"};

  const float spawnRatio =
      static_cast<float>(m_spawnedCount) / static_cast<float>(TOTAL_BALLS);
  const float settledRatio =
      (m_spawnedCount > 0)
          ? static_cast<float>(m_settledCount) / static_cast<float>(TOTAL_BALLS)
          : 0.0f;

  const float blended = loading_detail::BlendProgress(spawnRatio, settledRatio);
  const float visualProgress = loading_detail::EaseOutCubic(blended);
  const float asyncProgress =
      m_loadProgress ? m_loadProgress->load(std::memory_order_relaxed) : 0.0f;
  const float combined =
      loading_detail::CombineLoadingProgress(asyncProgress, visualProgress);

  // UI表示は単調増加させ、ロード完了なら必ず100%
  m_uiProgress = std::clamp(std::max(m_uiProgress, combined), 0.0f, 1.0f);
  const int percent = static_cast<int>(std::round(m_uiProgress * 100.0f));

  const int dotCount =
      static_cast<int>(std::fmod(m_sceneTime * 1.6f, 3.0f)) + 1;
  const std::wstring dots(static_cast<size_t>(dotCount), L'.');
  const float fade =
      m_fadeStarted ? std::clamp(1.0f - m_fadeAlpha, 0.0f, 1.0f) : 1.0f;
  const char *phase =
      m_fadeStarted
          ? "FADE"
          : (m_spawnedCount < TOTAL_BALLS
                 ? "SPAWNING"
                 : (m_settledCount < TOTAL_BALLS ? "WAIT_SETTLE" : "READY"));

  if (auto *title = ctx.world.Get<components::UIText>(m_textEntity)) {
    auto style = m_primaryStyle;
    style.color.w *= fade;
    style.outlineColor.w *= fade;
    title->style = style;
  }

  if (auto *progress =
          ctx.world.Get<components::UIText>(m_progressTextEntity)) {
    auto style = m_progressStyle;
    style.color.w *= fade;
    style.shadowColor.w *= fade;
    progress->style = style;
    progress->text = L"LOADING " + std::to_wstring(percent) + L"%" + dots;
  }

  m_tipTimer += ctx.dt;
  if (m_tipTimer > 2.8f) {
    m_tipTimer = 0.0f;
    m_tipIndex = (m_tipIndex + 1) % tips.size();
  }

  if (auto *caption = ctx.world.Get<components::UIText>(m_captionTextEntity)) {
    auto style = m_captionStyle;
    style.color.w *= fade;
    style.shadowColor.w *= fade;
    caption->style = style;
    caption->text = tips[m_tipIndex];
  }

  m_logTimer += ctx.dt;
  if (m_logTimer > 1.5f) {
    LOG_INFO("LoadingScene",
             "phase={} progress={}%, spawned={}/{} settled={} moving={} "
             "maxSpeed={:.2f} "
             "avgSpeed={:.2f} fade={}",
             phase, percent, m_spawnedCount, TOTAL_BALLS, m_settledCount,
             m_movingCount, m_maxSpeed, m_avgSpeed, m_fadeAlpha);
    m_logTimer = 0.0f;
  }
}

void LoadingScene::ApplyFadeToScene(core::GameContext &ctx) {
  if (!m_fadeStarted)
    return;

  // フェード中のアルファ変更は行わず、純粋に時間経過で遷移する
}

void LoadingScene::OnUpdate(core::GameContext &ctx) {
  float dt = ctx.dt;
  m_sceneTime += dt;

  // ボールをスポーン
  if (m_spawnedCount < TOTAL_BALLS) {
    m_spawnTimer += dt;
    if (m_spawnTimer >= SPAWN_INTERVAL) {
      SpawnBall(ctx);
      m_spawnTimer = 0.0f;
    }
  } else if (!m_spawnFinishedLogged) {
    LOG_INFO("LoadingScene", "All balls spawned: {}", TOTAL_BALLS);
    m_spawnFinishedLogged = true;
  }

  // 物理更新
  UpdatePhysics(ctx, dt);

  // 停滞監視（静止数が増えない状態が続いたら詳細ログ）
  if (m_settledCount != m_lastSettledCount) {
    m_stuckTimer = 0.0f;
    m_lastSettledCount = m_settledCount;
  } else if (m_spawnFinishedLogged && m_settledCount < TOTAL_BALLS) {
    m_stuckTimer += dt;
    if (m_stuckTimer > 3.0f) {
      if (m_hasMovingSample) {
        LOG_INFO("LoadingScene",
                 "phase=WAIT_SETTLE stuck settled={}/{} moving={} "
                 "maxSpeed={:.2f} avgSpeed={:.2f} "
                 "samplePos=({:.1f},{:.1f},{:.1f}) floorY={:.1f}",
                 m_settledCount, TOTAL_BALLS, m_movingCount, m_maxSpeed,
                 m_avgSpeed, m_lastMovingPos.x, m_lastMovingPos.y,
                 m_lastMovingPos.z, FLOOR_Y + BALL_RADIUS * BALL_MODEL_SCALE);
      } else {
        LOG_INFO("LoadingScene",
                 "phase=WAIT_SETTLE stuck settled={}/{} moving={} "
                 "maxSpeed={:.2f} avgSpeed={:.2f}",
                 m_settledCount, TOTAL_BALLS, m_movingCount, m_maxSpeed,
                 m_avgSpeed);
      }
      m_stuckTimer = 0.0f;
    }
  }

  // カメラ演出とUI更新
  UpdateCamera(ctx, dt);
  UpdateUI(ctx);

  // 全ボール静止後のフェード処理
  // 安全装置: 全てスポーンしてから一定時間経過したら強制的に次へ
  // 修正: ロードが完了したら、ボールの状態に関わらず即座に次へ進む
  bool triggerFade = false;

  // ロード完了チェック
  if (m_isLoading && m_loadTask.valid()) {
    auto status = m_loadTask.wait_for(std::chrono::milliseconds(0));
    if (status == std::future_status::ready) {
      m_loadCompleted = true;
      m_isLoading = false;
      if (m_loadProgress) {
        m_loadProgress->store(1.0f, std::memory_order_relaxed);
      }
      LOG_INFO("LoadingScene", "Asset Load Completed!");
    }
  }

  if (m_loadCompleted) {
    triggerFade = true; // ロード完了したら即終了
  }

  // タイムアウト安全装置
  if (m_spawnedCount >= TOTAL_BALLS) {
    m_forceFinishTimer += dt;
    if (m_forceFinishTimer > 4.0f) { // タイムアウトを少し延長
      triggerFade = true;
      if (!m_fadeStarted) {
        LOG_INFO("LoadingScene", "Force finish triggered due to timeout");
      }
    }
  }

  if (triggerFade) {
    UpdateFade(ctx, dt);
  }

  // ESCで強制スキップ
  if (ctx.input.GetKeyDown(VK_ESCAPE)) {
    LOG_INFO("LoadingScene", "Skip requested via ESC");
    if (ctx.sceneManager && m_nextSceneFactory) {
      ctx.sceneManager->ChangeScene(m_nextSceneFactory());
    }
  }
}

void LoadingScene::OnExit(core::GameContext &ctx) {
  LOG_INFO("LoadingScene", "OnExit");

  m_balls.clear();
  m_wallEntities.clear();

  // 基底クラスのクリーンアップ（全エンティティ破棄）
  Scene::OnExit(ctx);
}

} // namespace game::scenes
