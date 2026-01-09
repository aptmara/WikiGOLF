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

  // 非同期ロード開始
  m_isLoading = true;
  m_loadTask = std::async(std::launch::async, []() {
    auto data = std::make_unique<game::components::WikiGlobalData>();

    // WikiShortestPathの初期化（重い処理）
    data->pathSystem = std::make_unique<game::systems::WikiShortestPath>();
    bool dbLoaded =
        data->pathSystem->Initialize("Assets/data/jawiki_sdow.sqlite");
    if (!dbLoaded) {
      // ログは別スレッドからは注意が必要だが、ここでは簡易的に
    }

    // スタート・ゴール選定
    game::systems::WikiClient wikiClient;
    data->startPage = wikiClient.FetchRandomPageTitle();

    // 人気記事からターゲット選定
    if (dbLoaded && data->pathSystem->IsAvailable()) {
      auto result = data->pathSystem->FetchPopularPageTitle(100);
      data->targetPage = result.first;
      data->targetPageId = result.second;
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
    }

    if (data->startPage == data->targetPage) {
      data->targetPage = wikiClient.FetchTargetPageTitle();
      data->targetPageId = -1;
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
    }

    return data;
  });

  // マウスカーソルを非表示
  ctx.input.SetMouseCursorVisible(false);

  // ゴルフボールメッシュをロード
  m_ballMeshHandle = ctx.resource.LoadMesh("models/golfball.fbx");

  // カメラを作成
  m_cameraEntity = ctx.world.CreateEntity();
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
  m_textEntity = ctx.world.CreateEntity();
  auto &titleText = ctx.world.Add<components::UIText>(m_textEntity);
  titleText.text = L"WIKI GOLF LOADING";
  titleText.x = 0.0f;
  titleText.y = 110.0f;
  titleText.width = 1280.0f;
  titleText.style = m_primaryStyle;
  titleText.visible = true;
  titleText.layer = 12;

  // 進行状況
  m_progressTextEntity = ctx.world.CreateEntity();
  auto &progressText = ctx.world.Add<components::UIText>(m_progressTextEntity);
  progressText.text = L"0%";
  progressText.x = 0.0f;
  progressText.y = 170.0f;
  progressText.width = 1280.0f;
  progressText.style = m_progressStyle;
  progressText.visible = true;
  progressText.layer = 12;

  // キャプション（ tips 巡回用 ）
  m_captionTextEntity = ctx.world.CreateEntity();
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
  m_floorEntity = ctx.world.CreateEntity();
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
    auto wallEntity = ctx.world.CreateEntity();
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
  m_backdropEntity = ctx.world.CreateEntity();
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
  const float spawnHalfX = ARENA_HALF_WIDTH - renderRadius - 1.0f;
  const float spawnHalfZ = ARENA_HALF_DEPTH - renderRadius - 0.3f;
  std::uniform_real_distribution<float> distX(-spawnHalfX, spawnHalfX);
  std::uniform_real_distribution<float> distZ(-spawnHalfZ, spawnHalfZ);
  std::uniform_real_distribution<float> distVelX(-12.0f, 12.0f);
  std::uniform_real_distribution<float> distVelZ(-6.0f, 6.0f);
  std::uniform_real_distribution<float> spinDist(-2.5f, 2.5f);
  std::uniform_real_distribution<float> angleDist(-DirectX::XM_PI,
                                                  DirectX::XM_PI);

  auto entity = ctx.world.CreateEntity();

  // Transform
  auto &tr = ctx.world.Add<components::Transform>(entity);
  tr.position = {distX(rng),
                 26.0f +
                     static_cast<float>(m_spawnedCount) * (renderRadius * 0.9f),
                 distZ(rng)};
  const float renderScale = BALL_RADIUS * 2.0f * BALL_MODEL_SCALE;
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
  // ゴルフボールらしい白色
  mr.color = {1.0f, 1.0f, 1.0f, 1.0f};
  mr.isVisible = true;

  // ボール状態を記録
  BallState ball{};
  ball.entity = entity;
  ball.velocity = {distVelX(rng), -10.0f, distVelZ(rng)};
  ball.angularVelocity = {spinDist(rng) * 0.6f, spinDist(rng),
                          spinDist(rng) * 0.4f};
  ball.settled = false;
  m_balls.push_back(ball);

  m_spawnedCount++;
}

void LoadingScene::UpdatePhysics(core::GameContext &ctx, float dt) {
  const float renderRadius = BALL_RADIUS * BALL_MODEL_SCALE;
  const float floorY = FLOOR_Y + 0.3f + renderRadius;
  const float wallX = ARENA_HALF_WIDTH - renderRadius;
  const float wallZ = ARENA_HALF_DEPTH - renderRadius;
  const float linearDrag = std::pow(AIR_DRAG, dt * 60.0f);

  for (size_t i = 0; i < m_balls.size(); i++) {
    auto &ball = m_balls[i];
    if (!ctx.world.IsAlive(ball.entity) || ball.settled) {
      continue;
    }

    auto *tr = ctx.world.Get<components::Transform>(ball.entity);
    if (!tr)
      continue;

    // 重力を適用
    ball.velocity.y += GRAVITY * dt;

    // 空気抵抗で横移動を滑らかに減衰
    ball.velocity.x *= linearDrag;
    ball.velocity.z *= linearDrag;

    // 位置を更新
    tr->position.x += ball.velocity.x * dt;
    tr->position.y += ball.velocity.y * dt;
    tr->position.z += ball.velocity.z * dt;

    // 回転を更新
    DirectX::XMVECTOR currentRot = DirectX::XMLoadFloat4(&tr->rotation);
    DirectX::XMVECTOR deltaRot = DirectX::XMQuaternionRotationRollPitchYaw(
        ball.angularVelocity.x * dt, ball.angularVelocity.y * dt,
        ball.angularVelocity.z * dt);
    auto nextRot = DirectX::XMQuaternionNormalize(
        DirectX::XMQuaternionMultiply(deltaRot, currentRot));
    DirectX::XMStoreFloat4(&tr->rotation, nextRot);
    ball.angularVelocity.x *= ANGULAR_DAMPING;
    ball.angularVelocity.y *= ANGULAR_DAMPING;
    ball.angularVelocity.z *= ANGULAR_DAMPING;

    // 床との衝突
    if (tr->position.y < floorY) {
      tr->position.y = floorY;

      // 反発
      if (ball.velocity.y < -0.5f) {
        ball.velocity.y = -ball.velocity.y * RESTITUTION;
      } else {
        ball.velocity.y = 0.0f;
      }

      // 摩擦
      ball.velocity.x *= FRICTION;
      ball.velocity.z *= FRICTION;
    }

    // 壁との衝突（水槽の範囲）
    const float wallMinX = -wallX;
    const float wallMaxX = wallX;
    const float wallMinZ = -wallZ;
    const float wallMaxZ = wallZ;

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

    // ボール同士の衝突（簡易）
    for (size_t j = 0; j < m_balls.size(); j++) {
      if (i == j)
        continue; // 自分自身はスキップ

      auto &other = m_balls[j];
      auto *otherTr = ctx.world.Get<components::Transform>(other.entity);
      if (!otherTr)
        continue;

      float dx = otherTr->position.x - tr->position.x;
      float dy = otherTr->position.y - tr->position.y;
      float dz = otherTr->position.z - tr->position.z;
      float distSq = dx * dx + dy * dy + dz * dz;
      float minDist = renderRadius * 2.0f;

      if (distSq < minDist * minDist && distSq > 0.001f) {
        float dist = std::sqrt(distSq);
        float overlap = minDist - dist;

        // 分離ベクトル
        float nx = dx / dist;
        float ny = dy / dist;
        float nz = dz / dist;

        // 位置修正（めり込み解消）
        if (other.settled) {
          // 相手が静止しているなら自分だけが押し返される
          tr->position.x -= nx * overlap;
          tr->position.y -= ny * overlap;
          tr->position.z -= nz * overlap;
        } else if (j > i) {
          // 両方動いているなら半分ずつ（二重処理防止のため j > i の時のみ）
          tr->position.x -= nx * overlap * 0.5f;
          tr->position.y -= ny * overlap * 0.5f;
          tr->position.z -= nz * overlap * 0.5f;

          otherTr->position.x += nx * overlap * 0.5f;
          otherTr->position.y += ny * overlap * 0.5f;
          otherTr->position.z += nz * overlap * 0.5f;
        }

        // 速度の衝突応答（インパルスベース）
        // 相対速度
        float rvx = other.velocity.x - ball.velocity.x;
        float rvy = other.velocity.y - ball.velocity.y;
        float rvz = other.velocity.z - ball.velocity.z;

        // 法線方向の相対速度
        float velAlongNormal = rvx * nx + rvy * ny + rvz * nz;

        // 互いに近づいている場合のみ計算（離れようとしているなら無視）
        if (velAlongNormal < 0) {
          // 反発係数
          float jVal = -(1 + RESTITUTION) * velAlongNormal;

          // インパルス
          float ix = nx * jVal;
          float iy = ny * jVal;
          float iz = nz * jVal;

          if (other.settled) {
            // 相手は動かないので自分が全て受ける（壁扱い）
            ball.velocity.x -= ix;
            ball.velocity.y -= iy;
            ball.velocity.z -= iz;
          } else if (j > i) {
            // お互い動くなら半分ずつ（質量等しいと仮定）
            ball.velocity.x -= ix * 0.5f;
            ball.velocity.y -= iy * 0.5f;
            ball.velocity.z -= iz * 0.5f;

            other.velocity.x += ix * 0.5f;
            other.velocity.y += iy * 0.5f;
            other.velocity.z += iz * 0.5f;
          }
        }
      }
    }

    // 静止判定（速度が十分小さくなれば静止）
    float speedSq = ball.velocity.x * ball.velocity.x +
                    ball.velocity.y * ball.velocity.y +
                    ball.velocity.z * ball.velocity.z;
    if (speedSq < SETTLE_THRESHOLD * SETTLE_THRESHOLD) {
      ball.settled = true;
      ball.velocity = {0, 0, 0};
      ball.angularVelocity = {0, 0, 0};
    }
  }

  // ログ用メトリクスを更新
  int settledCount = 0;
  int moving = 0;
  float maxSpeed = 0.0f;
  float speedAccum = 0.0f;
  int speedCount = 0;
  m_hasMovingSample = false;
  for (const auto &ball : m_balls) {
    if (!ctx.world.IsAlive(ball.entity))
      continue;
    if (ball.settled) {
      settledCount++;
    }
    auto *tr = ctx.world.Get<components::Transform>(ball.entity);
    float speedSq = ball.velocity.x * ball.velocity.x +
                    ball.velocity.y * ball.velocity.y +
                    ball.velocity.z * ball.velocity.z;
    float speed = std::sqrt(speedSq);
    if (!ball.settled) {
      moving++;
      maxSpeed = std::max(maxSpeed, speed);
      speedAccum += speed;
      speedCount++;
      if (!m_hasMovingSample && tr) {
        m_lastMovingPos = tr->position;
        m_hasMovingSample = true;
      }
    }
  }
  m_movingCount = moving;
  m_maxSpeed = maxSpeed;
  m_avgSpeed =
      (speedCount > 0) ? speedAccum / static_cast<float>(speedCount) : 0.0f;
  m_settledCount = settledCount;
}

bool LoadingScene::AreAllBallsSettled() {
  if (m_spawnedCount < TOTAL_BALLS) {
    return false;
  }

  for (const auto &ball : m_balls) {
    if (!ball.settled) {
      return false;
    }
  }
  if (!m_allSettledLogged) {
    LOG_INFO("LoadingScene", "All balls settled");
    m_allSettledLogged = true;
  }
  return true;
}

void LoadingScene::UpdateFade(core::GameContext &ctx, float dt) {
  if (!m_fadeStarted) {
    m_fadeDelay -= dt;
    if (m_fadeDelay <= 0.0f) {
      m_fadeStarted = true;
      if (!m_fadeLogged) {
        LOG_INFO("LoadingScene", "Fade started");
        m_fadeLogged = true;
      }
    }
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
        // グローバルに移す。WikiGlobalDataはmove-onlyなのでmoveで渡す。
        ctx.world.SetGlobal(std::move(*data));
      } catch (const std::exception &e) {
        LOG_ERROR("LoadingScene", "Load task failed: {}", e.what());
      }
    }

    // 次のシーンへ遷移
    if (ctx.sceneManager && m_nextSceneFactory) {
      LOG_INFO("LoadingScene", "Fade completed, switching scene");
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
  const float eased = loading_detail::EaseOutCubic(blended);
  const int percent = static_cast<int>(std::round(eased * 100.0f));

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

  const float shrink = std::max(0.25f, 1.0f - m_fadeAlpha * 0.65f);
  const float baseScale = BALL_RADIUS * 2.0f * BALL_MODEL_SCALE;

  for (auto &ball : m_balls) {
    auto *tr = ctx.world.Get<components::Transform>(ball.entity);
    if (tr) {
      const float scaled = baseScale * shrink;
      tr->scale = {scaled, scaled, scaled};
    }

    auto *mr = ctx.world.Get<components::MeshRenderer>(ball.entity);
    if (mr) {
      mr->color.w = std::clamp(1.0f - m_fadeAlpha * 0.8f, 0.0f, 1.0f);
    }
  }

  if (auto *panelMr =
          ctx.world.Get<components::MeshRenderer>(m_backdropEntity)) {
    panelMr->color.w = std::clamp(1.0f - m_fadeAlpha * 0.5f, 0.0f, 1.0f);
  }
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
  bool forceFinish = false;

  // ロード完了チェック
  if (m_isLoading && m_loadTask.valid()) {
    auto status = m_loadTask.wait_for(std::chrono::milliseconds(0));
    if (status == std::future_status::ready) {
      m_loadCompleted = true;
      m_isLoading = false;
      LOG_INFO("LoadingScene", "Asset Load Completed!");
    }
  }

  if (m_loadCompleted) {
    forceFinish = true; // ロード完了したら即終了
  }

  // タイムアウト安全装置
  if (m_spawnedCount >= TOTAL_BALLS) {
    m_forceFinishTimer += dt;
    if (m_forceFinishTimer > 15.0f) { // タイムアウトを少し延長
      forceFinish = true;
      if (!m_fadeStarted) {
        LOG_INFO("LoadingScene", "Force finish triggered due to timeout");
      }
    }
  }

  if (AreAllBallsSettled() || forceFinish) {
    UpdateFade(ctx, dt);
    ApplyFadeToScene(ctx);
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

  // ボールを破棄
  for (auto &ball : m_balls) {
    if (ctx.world.IsAlive(ball.entity)) {
      ctx.world.DestroyEntity(ball.entity);
    }
  }
  m_balls.clear();

  // UI破棄
  if (ctx.world.IsAlive(m_textEntity)) {
    ctx.world.DestroyEntity(m_textEntity);
  }
  if (ctx.world.IsAlive(m_progressTextEntity)) {
    ctx.world.DestroyEntity(m_progressTextEntity);
  }
  if (ctx.world.IsAlive(m_captionTextEntity)) {
    ctx.world.DestroyEntity(m_captionTextEntity);
  }

  // カメラ破棄
  if (ctx.world.IsAlive(m_cameraEntity)) {
    ctx.world.DestroyEntity(m_cameraEntity);
  }

  // 床・壁破棄
  if (ctx.world.IsAlive(m_floorEntity)) {
    ctx.world.DestroyEntity(m_floorEntity);
  }
  for (auto &wall : m_wallEntities) {
    if (ctx.world.IsAlive(wall)) {
      ctx.world.DestroyEntity(wall);
    }
  }
  m_wallEntities.clear();

  if (ctx.world.IsAlive(m_backdropEntity)) {
    ctx.world.DestroyEntity(m_backdropEntity);
  }
}

} // namespace game::scenes
