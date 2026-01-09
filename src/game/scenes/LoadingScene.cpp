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
#include <cmath>
#include <random>

namespace game::scenes {

LoadingScene::LoadingScene(
    std::function<std::unique_ptr<core::Scene>()> nextSceneFactory)
    : m_nextSceneFactory(std::move(nextSceneFactory)) {}

void LoadingScene::OnEnter(core::GameContext &ctx) {
  LOG_INFO("LoadingScene", "OnEnter");

  // マウスカーソルを非表示
  ctx.input.SetMouseCursorVisible(false);

  // ゴルフボールメッシュをロード
  m_ballMeshHandle = ctx.resource.LoadMesh("models/golfball.fbx");

  // カメラを作成
  m_cameraEntity = ctx.world.CreateEntity();
  auto &camTr = ctx.world.Add<components::Transform>(m_cameraEntity);
  camTr.position = {0.0f, 0.0f,
                    -70.0f}; // 半径10のボールを映すためさらに遠ざける
  camTr.rotation = {0.0f, 0.0f, 0.0f, 1.0f};

  auto &cam = ctx.world.Add<components::Camera>(m_cameraEntity);
  cam.fov = DirectX::XM_PIDIV4; // 45度
  cam.nearZ = 0.1f;
  cam.farZ = 100.0f;
  cam.isMainCamera = true;

  // 床と壁を生成
  CreateBoundaries(ctx);

  // テキスト表示: "ゴルファー練習中" (ドットは後で追加)
  m_textEntity = ctx.world.CreateEntity();
  auto &textComp = ctx.world.Add<components::UIText>(m_textEntity);
  textComp.text = L"ゴルファー練習中";
  textComp.x = 350.0f;
  textComp.y = 520.0f;
  textComp.style = graphics::TextStyle::ModernBlack();
  textComp.visible = true;
  textComp.layer = 10;

  LOG_INFO("LoadingScene", "OnEnter complete");
}

void LoadingScene::CreateBoundaries(core::GameContext &ctx) {
  // シェーダーをロード
  auto shaderHandle = ctx.resource.LoadShader(
      "Basic", L"Assets/shaders/BasicVS.hlsl", L"Assets/shaders/BasicPS.hlsl");

  // 床（水槽の底）
  m_floorEntity = ctx.world.CreateEntity();
  auto &floorTr = ctx.world.Add<components::Transform>(m_floorEntity);
  floorTr.position = {0.0f, -10.0f, 0.0f};
  floorTr.scale = {100.0f, 0.5f, 1.0f}; // 薄い奥行き

  auto &floorMr = ctx.world.Add<components::MeshRenderer>(m_floorEntity);
  floorMr.mesh = ctx.resource.LoadMesh("builtin/cube");
  floorMr.shader = shaderHandle;
  floorMr.color = {0.9f, 0.9f, 0.95f, 1.0f}; // 明るいグレー
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

  // 左壁（巨大ボールに合わせて広く）
  createWall({-30.0f, 0.0f, 0.0f}, {1.0f, 100.0f, 5.0f},
             {0.7f, 0.85f, 0.95f, 0.3f}, true);
  // 右壁
  createWall({30.0f, 0.0f, 0.0f}, {1.0f, 100.0f, 5.0f},
             {0.7f, 0.85f, 0.95f, 0.3f}, true);
  // 奥壁
  createWall({0.0f, 0.0f, 2.0f}, {60.0f, 100.0f, 1.0f},
             {0.0f, 0.0f, 0.0f, 0.0f}, false);
}

void LoadingScene::SpawnBall(core::GameContext &ctx) {
  // ランダム生成器
  static std::mt19937 rng(std::random_device{}());
  std::uniform_real_distribution<float> distX(
      -15.0f, 15.0f); // 巨大なのでスポーン範囲も広く
  std::uniform_real_distribution<float> distZ(-0.5f, 0.5f);

  auto entity = ctx.world.CreateEntity();

  // Transform
  auto &tr = ctx.world.Add<components::Transform>(entity);
  // 直径20なので間隔は25以上に
  tr.position = {distX(rng), 50.0f + static_cast<float>(m_spawnedCount) * 25.0f,
                 distZ(rng)};
  tr.scale = {BALL_RADIUS * 2.0f, BALL_RADIUS * 2.0f, BALL_RADIUS * 2.0f};

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
  ball.velocity = {0.0f, 0.0f, 0.0f}; // 初速0、重力で落下
  ball.settled = false;
  m_balls.push_back(ball);

  m_spawnedCount++;
}

void LoadingScene::UpdatePhysics(core::GameContext &ctx, float dt) {
  const float floorY =
      -10.0f + 0.25f + BALL_RADIUS;        // 水槽の底(-10.0) + 厚み + 半径
  const float wallX = 30.0f - BALL_RADIUS; // 壁位置に合わせて拡張(30.0)

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

    // 位置を更新
    tr->position.x += ball.velocity.x * dt;
    tr->position.y += ball.velocity.y * dt;
    tr->position.z += ball.velocity.z * dt;

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
    const float wallMinZ = -0.5f + BALL_RADIUS;
    const float wallMaxZ = 0.5f - BALL_RADIUS;

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
      float minDist = BALL_RADIUS * 2.0f;

      if (distSq < minDist * minDist && distSq > 0.001f) {
        float dist = std::sqrt(distSq);
        float overlap = minDist - dist;

        // 分離ベクトル
        float nx = dx / dist;
        float ny = dy / dist;
        float nz = dz / dist;

        if (other.settled) {
          // 相手が静止しているなら自分だけが100%押し返される
          tr->position.x -= nx * overlap;
          tr->position.y -= ny * overlap;
          tr->position.z -= nz * overlap;

          // 速度の反射（簡易）
          ball.velocity.x *= -RESTITUTION;
          ball.velocity.y *= -RESTITUTION;
          ball.velocity.z *= -RESTITUTION;
        } else if (j > i) {
          // 両方動いているなら半分ずつ（二重処理防止のため j > i の時のみ）
          tr->position.x -= nx * overlap * 0.5f;
          tr->position.y -= ny * overlap * 0.5f;
          tr->position.z -= nz * overlap * 0.5f;

          otherTr->position.x += nx * overlap * 0.5f;
          otherTr->position.y += ny * overlap * 0.5f;
          otherTr->position.z += nz * overlap * 0.5f;

          std::swap(ball.velocity, other.velocity);
        }
      }
    }

    // 静止判定（速度が十分小さくなれば、場所に関わらず静止）
    float speedSq = ball.velocity.x * ball.velocity.x +
                    ball.velocity.y * ball.velocity.y +
                    ball.velocity.z * ball.velocity.z;
    if (speedSq < SETTLE_THRESHOLD * SETTLE_THRESHOLD) {
      ball.settled = true;
      ball.velocity = {0, 0, 0};
    }
  }
}

bool LoadingScene::AreAllBallsSettled() const {
  if (m_spawnedCount < TOTAL_BALLS) {
    return false;
  }

  for (const auto &ball : m_balls) {
    if (!ball.settled) {
      return false;
    }
  }
  return true;
}

void LoadingScene::UpdateFade(core::GameContext &ctx, float dt) {
  if (!m_fadeStarted) {
    m_fadeDelay -= dt;
    if (m_fadeDelay <= 0.0f) {
      m_fadeStarted = true;
    }
    return;
  }

  m_fadeAlpha += FADE_SPEED * dt;
  if (m_fadeAlpha >= 1.0f) {
    m_fadeAlpha = 1.0f;
    // 次のシーンへ遷移
    if (ctx.sceneManager && m_nextSceneFactory) {
      ctx.sceneManager->ChangeScene(m_nextSceneFactory());
    }
  }
}

void LoadingScene::OnUpdate(core::GameContext &ctx) {
  float dt = ctx.dt;

  // ボールをスポーン
  if (m_spawnedCount < TOTAL_BALLS) {
    m_spawnTimer += dt;
    if (m_spawnTimer >= SPAWN_INTERVAL) {
      SpawnBall(ctx);
      m_spawnTimer = 0.0f;
    }
  }

  // 物理更新
  UpdatePhysics(ctx, dt);

  // テキスト更新：ロード進行度に応じて「.」を増やす
  if (ctx.world.IsAlive(m_textEntity)) {
    auto *textComp = ctx.world.Get<components::UIText>(m_textEntity);
    if (textComp) {
      // 進行度を計算（0.0〜1.0）
      float progress = static_cast<float>(m_spawnedCount) / TOTAL_BALLS;
      // 「.」の数を0〜3個に
      int dotCount = static_cast<int>(progress * 4.0f);
      if (dotCount > 3)
        dotCount = 3;

      std::wstring dots(dotCount, L'.');
      textComp->text = L"ゴルファー練習中" + dots;
    }
  }

  // 全ボール静止後のフェード処理
  if (AreAllBallsSettled()) {
    UpdateFade(ctx, dt);
  }

  // ESCで強制スキップ
  if (ctx.input.GetKeyDown(VK_ESCAPE)) {
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
}

} // namespace game::scenes
