#include "../../core/GameContext.h"
#include "../../core/Input.h"
#include "../../core/Logger.h"
#include "../../core/StringUtils.h"
#include "../../ecs/World.h"
#include "../components/Camera.h"
#include "../components/MeshRenderer.h"
#include "../components/PhysicsComponents.h"
#include "../components/Transform.h"
#include "../components/UIText.h"
#include "../components/WikiComponents.h"
#include "../systems/PhysicsSystem.h"
#include "../systems/WikiClient.h"
#include "../systems/WikiGameSystem.h"
#include "WikiPinballScene.h"
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

// Helper removed: Use core::ToWString instead

// Windowsマクロ対策
#undef Get
#undef Reset
#undef min
#undef max

namespace game::scenes {

using namespace DirectX;
using namespace game::components;

// 静的剛体オブジェクトとして壁を生成
static void CreateWall(core::GameContext &ctx, float x, float y, float z,
                       float w, float h, float d, XMFLOAT4 color) {
  auto e = ctx.world.CreateEntity();
  auto &t = ctx.world.Add<Transform>(e);
  t.position = {x, y, z};
  t.scale = {w, h, d};

  auto &mr = ctx.world.Add<MeshRenderer>(e);
  mr.mesh = ctx.resource.LoadMesh("cube");
  mr.shader = ctx.resource.LoadShader("Basic", L"Assets/shaders/BasicVS.hlsl",
                                      L"Assets/shaders/BasicPS.hlsl");
  mr.color = color;

  auto &rb = ctx.world.Add<RigidBody>(e);
  rb.isStatic = true;
  rb.restitution = 0.5f;

  auto &c = ctx.world.Add<Collider>(e);
  c.type = ColliderType::Box;
  c.size = {0.5f, 0.5f, 0.5f};
}

void WikiPinballScene::OnEnter(core::GameContext &ctx) {
  LOG_INFO("WikiPinball", "OnEnter");
  ctx.world.Reset();

  ctx.resource.LoadShader("Basic", L"Assets/shaders/BasicVS.hlsl",
                          L"Assets/shaders/BasicPS.hlsl");

  // カメラエンティティの生成
  auto cam = ctx.world.CreateEntity();
  auto &t = ctx.world.Add<Transform>(cam);
  t.position = {0.0f, 30.0f, -5.0f};
  t.rotation = {0.707f, 0.0f, 0.0f, 0.707f};

  auto &camComp = ctx.world.Add<Camera>(cam);
  camComp.fov = 45.0f;
  camComp.aspectRatio = 1280.0f / 720.0f;
  camComp.nearZ = 0.1f;
  camComp.farZ = 100.0f;

  CreateBoundaries(ctx);
  CreateFlippers(ctx);

  // WikiClient から記事を取得
  game::systems::WikiClient wikiClient;
  std::string startPage = wikiClient.FetchRandomPageTitle();
  std::string targetPage = wikiClient.FetchTargetPageTitle();

  // 開始と目的が同じ場合は再取得
  if (startPage == targetPage) {
    targetPage = wikiClient.FetchTargetPageTitle();
  }

  LOG_INFO("WikiPinball", "Start: {}, Target: {}", startPage, targetPage);

  // 記事の内部リンクを取得して障害物として配置
  auto links = wikiClient.FetchPageLinks(startPage, 20);
  LOG_INFO("WikiPinball", "Fetched {} links", links.size());

  // リンクを障害物として配置（グリッド状に）
  const float startX = -4.0f;
  const float startZ = 8.0f;
  const float spacingX = 2.5f;
  const float spacingZ = 2.0f;
  const int cols = 4;

  for (size_t i = 0; i < links.size() && i < 16; ++i) {
    int row = static_cast<int>(i) / cols;
    int col = static_cast<int>(i) % cols;
    float x = startX + col * spacingX;
    float z = startZ - row * spacingZ;
    CreateLinkObstacle(ctx, x, z, links[i].title);
  }

  SpawnBall(ctx);

  // UI作成
  // ヘッダー（現在記事→目的記事）
  m_titleEntity = ctx.world.CreateEntity();
  auto &ht = ctx.world.Add<UIText>(m_titleEntity);
  ht.text = L"📍 " + core::ToWString(startPage) + L" → 🎯 " +
            core::ToWString(targetPage);
  ht.x = 10;
  ht.y = 10;
  ht.visible = true;
  ht.layer = 10;

  // スコア
  m_scoreEntity = ctx.world.CreateEntity();
  auto &st = ctx.world.Add<UIText>(m_scoreEntity);
  st.text = L"Score: 0  Lives: 3  Moves: 0";
  st.x = 10;
  st.y = 40;
  st.visible = true;
  st.layer = 10;

  // 情報パネル（遷移待ちリンク表示）
  m_infoEntity = ctx.world.CreateEntity();
  auto &it = ctx.world.Add<UIText>(m_infoEntity);
  it.text = L"リンクを壊して↑で遷移";
  it.x = 10;
  it.y = 680;
  it.visible = true;
  it.layer = 10;

  // 記事概要UI
  auto introE = ctx.world.CreateEntity();
  graphics::TextStyle introStyle = graphics::TextStyle::Default();
  introStyle.fontSize = 16.0f;
  introStyle.color = {0.9f, 0.9f, 0.9f, 0.9f};
  introStyle.hasShadow = true;
  introStyle.shadowColor = {0.0f, 0.0f, 0.0f, 1.0f};

  auto introUI = UIText::Create(L"", 20.0f, 80.0f, introStyle);
  introUI.width = 300.0f;
  introUI.height = 600.0f;

  ctx.world.Add<UIText>(introE, introUI);

  // ゲーム状態を初期化
  m_score = 0;
  WikiGameState state;
  state.scoreEntity = m_scoreEntity;
  state.infoEntity = m_infoEntity;
  state.headerEntity = m_titleEntity;
  state.introEntity = introE;
  state.score = 0;
  state.currentPage = startPage;
  state.currentIntro = ""; // 初期化時はいったん空、TransitionToPageで埋まる
  state.targetPage = targetPage;
  state.pendingLink = "";
  state.moveCount = 0;
  state.lives = 3;
  state.gameCleared = false;
  ctx.world.SetGlobal(state);
}

void WikiPinballScene::OnUpdate(core::GameContext &ctx) {
  auto *state = ctx.world.GetGlobal<WikiGameState>();

  // リトライはゲームオーバー/クリア時のみ許可
  if (state && (state->lives <= 0 || state->gameCleared) &&
      ctx.input.GetKeyDown('R')) {
    OnEnter(ctx);
    return;
  }

  // 物理演算とゲームロジックの更新
  // 物理挙動はPhysicsSystem、ルールやスコアはWikiGameSystemで処理
  game::systems::PhysicsSystem(ctx, ctx.dt);
  game::systems::WikiGameSystem(ctx);

  if (!state || state->gameCleared)
    return;

  // ↑キーで遷移
  if (ctx.input.GetKeyDown(VK_UP)) {
    if (!state->pendingLink.empty()) {
      TransitionToPage(ctx, state->pendingLink);
      return;
    }
  }

  // ボールの落下（ロスト）を判定
  if (ctx.world.IsAlive(m_ballEntity)) {
    auto *t = ctx.world.Get<Transform>(m_ballEntity);
    if (t && t->position.z < -15.0f) {
      ctx.world.DestroyEntity(m_ballEntity);

      state->lives--;
      if (state->lives > 0) {
        // UI更新
        auto *scoreUI = ctx.world.Get<UIText>(state->scoreEntity);
        if (scoreUI) {
          scoreUI->text = L"Score: " + std::to_wstring(state->score) +
                          L"  Lives: " + std::to_wstring(state->lives) +
                          L"  Moves: " + std::to_wstring(state->moveCount);
        }
        SpawnBall(ctx);
      } else {
        // ゲームオーバー
        auto *infoUI = ctx.world.Get<UIText>(state->infoEntity);
        if (infoUI) {
          infoUI->text = L"💀 GAME OVER - Rでリトライ";
        }
      }
    }
  }
}

void WikiPinballScene::OnExit(core::GameContext &ctx) { ctx.world.Reset(); }

void WikiPinballScene::SetupTable(core::GameContext &ctx) {}

void WikiPinballScene::SpawnBall(core::GameContext &ctx) {
  m_ballEntity = ctx.world.CreateEntity();
  auto &t = ctx.world.Add<Transform>(m_ballEntity);
  t.position = {2.0f, 0.5f, 5.0f};

  auto &mr = ctx.world.Add<MeshRenderer>(m_ballEntity);
  mr.mesh = ctx.resource.LoadMesh("sphere");
  mr.shader = ctx.resource.LoadShader("Basic", L"Assets/shaders/BasicVS.hlsl",
                                      L"Assets/shaders/BasicPS.hlsl");
  mr.color = {1.0f, 0.0f, 0.0f, 1.0f};

  auto &rb = ctx.world.Add<RigidBody>(m_ballEntity);
  rb.mass = 1.0f;
  rb.restitution = 0.8f;
  rb.drag = 0.001f;
  rb.velocity = {-1.0f, 0.0f, -6.0f};

  auto &c = ctx.world.Add<Collider>(m_ballEntity);
  c.type = ColliderType::Sphere;
  c.radius = 0.25f;
}

void WikiPinballScene::CreateBoundaries(core::GameContext &ctx) {
  // 床オブジェクトの生成
  auto floor = ctx.world.CreateEntity();
  auto &t = ctx.world.Add<Transform>(floor);
  t.position = {0.0f, -0.5f, 0.0f};
  t.scale = {12.0f, 1.0f, 25.0f};

  auto &mr = ctx.world.Add<MeshRenderer>(floor);
  mr.mesh = ctx.resource.LoadMesh("cube");
  mr.shader = ctx.resource.LoadShader("Basic", L"Assets/shaders/BasicVS.hlsl",
                                      L"Assets/shaders/BasicPS.hlsl");
  mr.color = {0.0f, 0.3f, 0.0f, 1.0f};

  auto &c = ctx.world.Add<Collider>(floor);
  c.type = ColliderType::Box;
  c.size = {0.5f, 0.5f, 0.5f};

  // 周囲の壁オブジェクトの生成
  CreateWall(ctx, -6.5f, 0.5f, 0.0f, 1.0f, 2.0f, 25.0f,
             {0.5f, 0.5f, 0.5f, 1.0f});
  CreateWall(ctx, 6.5f, 0.5f, 0.0f, 1.0f, 2.0f, 25.0f,
             {0.5f, 0.5f, 0.5f, 1.0f});
  CreateWall(ctx, 0.0f, 0.5f, 12.5f, 13.0f, 2.0f, 1.0f,
             {0.5f, 0.5f, 0.5f, 1.0f});
}

void WikiPinballScene::CreateHeading(core::GameContext &ctx, float x, float z,
                                     const std::wstring &text) {
  auto e = ctx.world.CreateEntity();
  auto &t = ctx.world.Add<Transform>(e);
  t.position = {x, 0.5f, z};
  t.scale = {1.5f, 0.5f, 0.5f};

  auto &mr = ctx.world.Add<MeshRenderer>(e);
  mr.mesh = ctx.resource.LoadMesh("cube");
  mr.shader = ctx.resource.LoadShader("Basic", L"Assets/shaders/BasicVS.hlsl",
                                      L"Assets/shaders/BasicPS.hlsl");
  mr.color = {0.0f, 0.0f, 1.0f, 1.0f};

  auto &h = ctx.world.Add<Heading>(e);
  h.fullText = "Heading";

  auto &rb = ctx.world.Add<RigidBody>(e);
  rb.isStatic = true;

  auto &c = ctx.world.Add<Collider>(e);
  c.type = ColliderType::Box;
  c.size = {0.5f, 0.5f, 0.5f};
}

void WikiPinballScene::CreateFlippers(core::GameContext &ctx) {
  // 左フリッパーの生成
  {
    auto e = ctx.world.CreateEntity();
    auto &t = ctx.world.Add<Transform>(e);
    t.position = {-2.0f, 0.5f, -8.0f};
    t.scale = {2.0f, 1.0f, 0.5f};
    // 初期回転なし

    auto &mr = ctx.world.Add<MeshRenderer>(e);
    mr.mesh = ctx.resource.LoadMesh("builtin/cube");
    mr.shader = ctx.resource.LoadShader("Basic", L"Assets/shaders/BasicVS.hlsl",
                                        L"Assets/shaders/BasicPS.hlsl");
    mr.color = {1.0f, 1.0f, 0.0f, 1.0f};

    auto &rb = ctx.world.Add<RigidBody>(e);
    rb.isStatic = true; // 制御の都合上静的剛体とし、衝突反発はプログラムで管理するため弾性を0に設定
    rb.restitution = 0.0f;

    auto &c = ctx.world.Add<Collider>(e);
    c.type = ColliderType::Box;
    c.size = {0.5f, 0.5f, 0.5f};

    auto &f = ctx.world.Add<Flipper>(e);
    f.side = Flipper::Left;
    f.maxAngle = 45.0f;
    f.turnSpeed = 15.0f;
  }

  // 右フリッパーの生成
  {
    auto e = ctx.world.CreateEntity();
    auto &t = ctx.world.Add<Transform>(e);
    t.position = {2.0f, 0.5f, -8.0f};
    t.scale = {2.0f, 1.0f, 0.5f};

    auto &mr = ctx.world.Add<MeshRenderer>(e);
    mr.mesh = ctx.resource.LoadMesh("builtin/cube");
    mr.shader = ctx.resource.LoadShader("Basic", L"Assets/shaders/BasicVS.hlsl",
                                        L"Assets/shaders/BasicPS.hlsl");
    mr.color = {1.0f, 1.0f, 0.0f, 1.0f};

    auto &rb = ctx.world.Add<RigidBody>(e);
    rb.isStatic = true;
    rb.restitution = 0.0f;

    auto &c = ctx.world.Add<Collider>(e);
    c.type = ColliderType::Box;
    c.size = {0.5f, 0.5f, 0.5f};

    auto &f = ctx.world.Add<Flipper>(e);
    f.side = Flipper::Right;
    f.maxAngle = 45.0f;
    f.turnSpeed = 15.0f;
  }
}

void WikiPinballScene::CreateLinkObstacle(core::GameContext &ctx, float x,
                                          float z,
                                          const std::string &linkTarget) {
  auto e = ctx.world.CreateEntity();
  auto &t = ctx.world.Add<Transform>(e);
  t.position = {x, 0.5f, z};
  t.scale = {2.0f, 0.5f, 0.8f};

  auto &mr = ctx.world.Add<MeshRenderer>(e);
  mr.mesh = ctx.resource.LoadMesh("cube");
  mr.shader = ctx.resource.LoadShader("Basic", L"Assets/shaders/BasicVS.hlsl",
                                      L"Assets/shaders/BasicPS.hlsl");
  // 青緑色でリンク障害物を表示
  mr.color = {0.0f, 0.6f, 0.8f, 1.0f};

  auto &h = ctx.world.Add<Heading>(e);
  h.fullText = linkTarget;
  h.linkTarget = linkTarget;
  h.maxHealth = 1; // 一回の衝突で破壊される設定
  h.currentHealth = 1;

  auto &rb = ctx.world.Add<RigidBody>(e);
  rb.isStatic = true;
  rb.restitution = 0.7f;

  auto &c = ctx.world.Add<Collider>(e);
  c.type = ColliderType::Box;
  c.size = {0.5f, 0.5f, 0.5f};
}

void WikiPinballScene::TransitionToPage(core::GameContext &ctx,
                                        const std::string &pageName) {
  auto *state = ctx.world.GetGlobal<WikiGameState>();
  if (!state)
    return;

  // クリアチェック
  if (pageName == state->targetPage) {
    state->gameCleared = true;

    // スコア計算（少ない移動回数ほど高得点）
    int bonus = 5000 - state->moveCount * 500;
    if (bonus < 0)
      bonus = 0;
    state->score += bonus;

    // ヘッダーUI更新（クリア表示）
    auto *headerUI = ctx.world.Get<UIText>(state->headerEntity);
    if (headerUI) {
      headerUI->text =
          L"🎊 GOAL! " + core::ToWString(state->targetPage) + L" に到達！ 🎊";
    }

    // スコアUI更新（最終スコア）
    auto *scoreUI = ctx.world.Get<UIText>(state->scoreEntity);
    if (scoreUI) {
      scoreUI->text = L"✨ Final Score: " + std::to_wstring(state->score) +
                      L" (Moves: " + std::to_wstring(state->moveCount) + L")";
    }

    // 情報パネル更新
    auto *infoUI = ctx.world.Get<UIText>(state->infoEntity);
    if (infoUI) {
      infoUI->text = L"🎉 クリアおめでとう！ Rでリトライ";
    }

    // ボールを停止
    ctx.world.Query<RigidBody>().Each([](ecs::Entity, RigidBody &rb) {
      if (!rb.isStatic) {
        rb.velocity = {0, 0, 0};
        rb.acceleration = {0, 0, 0};
      }
    });

    return;
  }

  // 遷移処理
  state->currentPage = pageName;
  state->moveCount++;
  state->pendingLink = "";

  // 既存の障害物を削除
  std::vector<ecs::Entity> toRemove;
  ctx.world.Query<Heading>().Each(
      [&](ecs::Entity entity, Heading &) { toRemove.push_back(entity); });
  for (auto e : toRemove) {
    ctx.world.DestroyEntity(e);
  }

  // 新しい記事のリンクを取得
  game::systems::WikiClient wikiClient;

  // 記事テキスト取得
  std::string extract = wikiClient.FetchPageExtract(pageName, 400);
  state->currentIntro = extract;

  auto links = wikiClient.FetchPageLinks(pageName, 20);
  LOG_INFO("WikiPinball", "Transitioned to: {}, {} links", pageName,
           links.size());

  // リンクを障害物として配置
  const float startX = -4.0f;
  const float startZ = 8.0f;
  const float spacingX = 2.5f;
  const float spacingZ = 2.0f;
  const int cols = 4;

  for (size_t i = 0; i < links.size() && i < 16; ++i) {
    int row = static_cast<int>(i) / cols;
    int col = static_cast<int>(i) % cols;
    float x = startX + col * spacingX;
    float z = startZ - row * spacingZ;
    CreateLinkObstacle(ctx, x, z, links[i].title);
  }

  // UI更新
  auto *headerUI = ctx.world.Get<UIText>(state->headerEntity);
  if (headerUI) {
    headerUI->text = L"📍 " + core::ToWString(state->currentPage) + L" → 🎯 " +
                     core::ToWString(state->targetPage);
  }

  auto *scoreUI = ctx.world.Get<UIText>(state->scoreEntity);
  if (scoreUI) {
    scoreUI->text = L"Score: " + std::to_wstring(state->score) + L"  Lives: " +
                    std::to_wstring(state->lives) + L"  Moves: " +
                    std::to_wstring(state->moveCount);
  }

  auto *infoUI = ctx.world.Get<UIText>(state->infoEntity);
  if (infoUI) {
    infoUI->text = L"リンクを壊して↑で遷移";
  }

  auto *introUI = ctx.world.Get<UIText>(state->introEntity);
  if (introUI) {
    introUI->text = core::ToWString(state->currentIntro);
  }
}

} // namespace game::scenes
