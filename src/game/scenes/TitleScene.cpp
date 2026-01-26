/**
 * @file TitleScene.cpp
 * @brief タイトル画面シーンの実装
 */

#include "TitleScene.h"
#include "../../audio/AudioSystem.h"
#include "../../core/GameContext.h"
#include "../../core/Input.h"
#include "../../core/SceneManager.h"
#include "../components/Camera.h"
#include "../components/MeshRenderer.h"
#include "../components/Transform.h"
#include "../components/UIText.h"
#include "LoadingScene.h"
#include "WikiGolfScene.h"
#include <DirectXMath.h>
#include <cmath>

namespace game::scenes {

void TitleScene::OnEnter(core::GameContext &ctx) {
  LOG_INFO("TitleScene", "OnEnter");

  m_time = 0.0f;
  m_lightPhase = 0.0f;
  m_decorEntities.clear();
  m_lightBeams.clear();
  m_badgeEntities.clear();

  // マウスカーソル表示
  ctx.input.SetMouseCursorVisible(true);

  // BGM再生
  if (ctx.audio) {
    ctx.audio->PlayBGM(ctx, "bgm_title.mp3");
  }

  // シェーダー・メッシュ
  auto shader =
      ctx.resource.LoadShader("Basic", L"Assets/shaders/BasicVS.hlsl",
                              L"Assets/shaders/BasicPS.hlsl");

  // カメラ
  m_cameraEntity = ctx.world.CreateEntity();
  auto &camTr = ctx.world.Add<components::Transform>(m_cameraEntity);
  camTr.position = {0.0f, 8.0f, -55.0f};
  auto camRot =
      DirectX::XMQuaternionRotationRollPitchYaw(-0.08f, 0.0f, 0.0f);
  DirectX::XMStoreFloat4(&camTr.rotation, camRot);
  auto &cam = ctx.world.Add<components::Camera>(m_cameraEntity);
  cam.fov = DirectX::XMConvertToRadians(55.0f);
  cam.nearZ = 0.1f;
  cam.farZ = 200.0f;
  cam.isMainCamera = true;

  // 床
  m_floorEntity = ctx.world.CreateEntity();
  auto &floorTr = ctx.world.Add<components::Transform>(m_floorEntity);
  floorTr.position = {0.0f, -6.0f, 12.0f};
  floorTr.scale = {140.0f, 2.0f, 140.0f};
  auto &floorMr = ctx.world.Add<components::MeshRenderer>(m_floorEntity);
  floorMr.mesh = ctx.resource.LoadMesh("builtin/cube");
  floorMr.shader = shader;
  floorMr.color = {0.02f, 0.08f, 0.12f, 1.0f};
  floorMr.isVisible = true;

  auto addDecorPanel = [&](DirectX::XMFLOAT3 pos, DirectX::XMFLOAT3 scale,
                           DirectX::XMFLOAT4 color) {
    auto e = ctx.world.CreateEntity();
    auto &tr = ctx.world.Add<components::Transform>(e);
    tr.position = pos;
    tr.scale = scale;
    auto &mr = ctx.world.Add<components::MeshRenderer>(e);
    mr.mesh = ctx.resource.LoadMesh("builtin/cube");
    mr.shader = shader;
    mr.color = color;
    mr.isVisible = true;
    m_decorEntities.push_back(e);
  };

  // 背景パネルとリムライト風アクセント
  addDecorPanel({0.0f, 12.0f, 45.0f}, {180.0f, 60.0f, 4.0f},
                {0.03f, 0.1f, 0.18f, 1.0f});
  addDecorPanel({0.0f, -5.0f, 30.0f}, {180.0f, 6.0f, 6.0f},
                {0.08f, 0.28f, 0.5f, 0.7f});
  addDecorPanel({0.0f, 24.0f, 30.0f}, {180.0f, 6.0f, 6.0f},
                {0.22f, 0.46f, 0.72f, 0.55f});

  // 背景ライトスリット
  for (int i = -4; i <= 4; i += 2) {
    auto beam = ctx.world.CreateEntity();
    auto &tr = ctx.world.Add<components::Transform>(beam);
    tr.position = {static_cast<float>(i) * 18.0f, 8.0f, 32.0f};
    tr.scale = {10.0f, 42.0f, 3.0f};
    auto &mr = ctx.world.Add<components::MeshRenderer>(beam);
    mr.mesh = ctx.resource.LoadMesh("builtin/cube");
    mr.shader = shader;
    mr.color = {0.12f, 0.45f, 0.78f, 0.25f};
    mr.isVisible = true;
    m_lightBeams.push_back(beam);
  }

  // デコレーションボール
  m_ballEntity = ctx.world.CreateEntity();
  auto &ballTr = ctx.world.Add<components::Transform>(m_ballEntity);
  ballTr.position = {0.0f, 4.5f, 0.0f};
  ballTr.scale = {16.0f, 16.0f, 16.0f};
  auto &ballMr = ctx.world.Add<components::MeshRenderer>(m_ballEntity);
  ballMr.mesh = ctx.resource.LoadMesh("models/golfball.fbx");
  ballMr.shader = shader;
  ballMr.color = {1.0f, 1.0f, 1.0f, 1.0f};
  ballMr.isVisible = true;

  // デコレーションクラブ
  m_clubEntity = ctx.world.CreateEntity();
  auto &clubTr = ctx.world.Add<components::Transform>(m_clubEntity);
  clubTr.position = {10.0f, 4.0f, 12.0f};
  clubTr.scale = {0.9f, 0.9f, 0.9f};
  auto &clubMr = ctx.world.Add<components::MeshRenderer>(m_clubEntity);
  clubMr.mesh = ctx.resource.LoadMesh("models/golf_club.fbx");
  clubMr.shader = shader;
  clubMr.color = {0.9f, 0.95f, 1.0f, 1.0f};
  clubMr.isVisible = true;

  // UIスタイル
  m_titleStyle = graphics::TextStyle::Title();
  m_titleStyle.fontSize = 72.0f;
  m_titleStyle.color = {0.95f, 0.98f, 1.0f, 1.0f};
  m_titleStyle.outlineColor = {0.0f, 0.12f, 0.25f, 0.85f};
  m_titleStyle.outlineWidth = 2.8f;
  m_titleStyle.shadowColor = {0.0f, 0.0f, 0.0f, 0.7f};

  m_subStyle = graphics::TextStyle::ModernBlack();
  m_subStyle.fontSize = 26.0f;
  m_subStyle.align = graphics::TextAlign::Center;
  m_subStyle.color = {0.75f, 0.84f, 0.96f, 1.0f};
  m_subStyle.hasOutline = true;
  m_subStyle.outlineColor = {0.08f, 0.18f, 0.32f, 0.7f};
  m_subStyle.outlineWidth = 1.6f;

  m_startStyle = graphics::TextStyle::ModernBlack();
  m_startStyle.fontSize = 30.0f;
  m_startStyle.align = graphics::TextAlign::Center;
  m_startStyle.color = {0.95f, 0.95f, 0.95f, 1.0f};
  m_startStyle.hasOutline = true;
  m_startStyle.outlineColor = {0.0f, 0.0f, 0.0f, 0.8f};
  m_startStyle.outlineWidth = 2.0f;
  m_startStyle.hasShadow = true;
  m_startStyle.shadowColor = {0.0f, 0.0f, 0.0f, 0.65f};

  m_hintStyle = graphics::TextStyle::ModernBlack();
  m_hintStyle.fontSize = 18.0f;
  m_hintStyle.align = graphics::TextAlign::Center;
  m_hintStyle.color = {0.65f, 0.78f, 0.95f, 0.9f};

  m_badgeStyle = graphics::TextStyle::ModernBlack();
  m_badgeStyle.fontSize = 20.0f;
  m_badgeStyle.align = graphics::TextAlign::Center;
  m_badgeStyle.color = {0.9f, 0.95f, 1.0f, 1.0f};
  m_badgeStyle.hasOutline = true;
  m_badgeStyle.outlineColor = {0.05f, 0.12f, 0.2f, 0.7f};
  m_badgeStyle.outlineWidth = 1.5f;

  // タイトルテキスト
  m_titleTextEntity = ctx.world.CreateEntity();
  auto &tText = ctx.world.Add<components::UIText>(m_titleTextEntity);
  tText.text = L"WIKI GOLF";
  tText.x = 0.0f;
  tText.y = 120.0f;
  tText.width = 1280.0f;
  tText.style = m_titleStyle;
  tText.visible = true;

  // サブタイトル
  m_subTextEntity = ctx.world.CreateEntity();
  auto &sub = ctx.world.Add<components::UIText>(m_subTextEntity);
  sub.text = L"THE ENCYCLOPEDIA COURSE AWAITS";
  sub.x = 0.0f;
  sub.y = 190.0f;
  sub.width = 1280.0f;
  sub.style = m_subStyle;
  sub.visible = true;

  // スタートボタンテキスト
  m_startEntity = ctx.world.CreateEntity();
  auto &sText = ctx.world.Add<components::UIText>(m_startEntity);
  sText.text = L"CLICK / ENTER TO START";
  sText.x = 0.0f;
  sText.y = 420.0f;
  sText.width = 1280.0f;
  sText.style = m_startStyle;
  sText.visible = true;

  // ヒント
  m_hintEntity = ctx.world.CreateEntity();
  auto &hText = ctx.world.Add<components::UIText>(m_hintEntity);
  hText.text = L"ESC to quit | Move the mouse to feel the breeze";
  hText.x = 0.0f;
  hText.y = 470.0f;
  hText.width = 1280.0f;
  hText.style = m_hintStyle;
  hText.visible = true;

  // フッターメッセージ
  m_footerEntity = ctx.world.CreateEntity();
  auto &fText = ctx.world.Add<components::UIText>(m_footerEntity);
  fText.text = L"Procedural courses crafted from the encyclopedia itself.";
  fText.x = 0.0f;
  fText.y = 520.0f;
  fText.width = 1280.0f;
  fText.style = m_badgeStyle;
  fText.visible = true;

  // バッジ風プレート
  auto spawnBadge = [&](float x, float y, const wchar_t *msg) {
    auto e = ctx.world.CreateEntity();
    auto &text = ctx.world.Add<components::UIText>(e);
    text.text = msg;
    text.x = x;
    text.y = y;
    text.width = 320.0f;
    text.style = m_badgeStyle;
    text.visible = true;
    m_badgeEntities.push_back(e);
  };

  spawnBadge(180.0f, 320.0f, L"Procedural fairways");
  spawnBadge(480.0f, 350.0f, L"Dynamic wind & shots");
  spawnBadge(780.0f, 320.0f, L"Wikipedia-driven holes");

  LOG_INFO("TitleScene", "OnEnter complete");
}

void TitleScene::OnUpdate(core::GameContext &ctx) {
  m_time += ctx.dt;
  m_lightPhase += ctx.dt;

  // クリックまたはEnter/Spaceでゲーム開始
  bool start = ctx.input.GetMouseButtonDown(0) ||
               ctx.input.GetKeyDown(VK_SPACE) ||
               ctx.input.GetKeyDown(VK_RETURN);
  if (start) {
    if (ctx.audio) {
      ctx.audio->PlaySE(ctx, "se_decide.mp3");
    }

    if (ctx.sceneManager) {
      // LoadingSceneを経由してWikiGolfSceneへ遷移
      auto loadingScene = std::make_unique<LoadingScene>(
          []() { return std::make_unique<WikiGolfScene>(); });
      ctx.sceneManager->ChangeScene(std::move(loadingScene));
    }
  }

  // カメラ演出
  if (ctx.world.IsAlive(m_cameraEntity)) {
    auto *tr = ctx.world.Get<components::Transform>(m_cameraEntity);
    if (tr) {
      float orbit = std::sin(m_time * 0.35f) * 6.5f;
      float dolly = std::cos(m_time * 0.22f) * 4.0f;
      tr->position.x = orbit;
      tr->position.y = 8.0f + std::sin(m_time * 0.6f) * 0.6f;
      tr->position.z = -55.0f + dolly;

      auto rot = DirectX::XMQuaternionRotationRollPitchYaw(
          -0.08f + std::sin(m_time * 0.7f) * 0.02f, orbit * 0.003f, 0.0f);
      DirectX::XMStoreFloat4(&tr->rotation, rot);
    }
  }

  // ボール演出
  if (ctx.world.IsAlive(m_ballEntity)) {
    auto *tr = ctx.world.Get<components::Transform>(m_ballEntity);
    if (tr) {
      float bob = std::sin(m_time * 2.1f) * 0.8f + 5.0f;
      tr->position.y = bob;
      auto rot = DirectX::XMQuaternionRotationRollPitchYaw(
          m_time * 0.9f, m_time * 1.2f, m_time * 0.6f);
      DirectX::XMStoreFloat4(&tr->rotation, rot);
    }
  }

  // クラブ演出（ボールをなぞる軌道）
  if (ctx.world.IsAlive(m_clubEntity)) {
    auto *tr = ctx.world.Get<components::Transform>(m_clubEntity);
    if (tr) {
      float radius = 18.0f;
      float angle = m_time * 0.8f;
      tr->position.x = std::cos(angle) * radius;
      tr->position.z = std::sin(angle) * radius + 4.0f;
      tr->position.y = 3.5f + std::sin(m_time * 1.8f) * 1.0f;

      auto rot = DirectX::XMQuaternionRotationRollPitchYaw(
          -0.3f + std::sin(m_time * 1.2f) * 0.2f, angle + DirectX::XM_PI,
          0.0f);
      DirectX::XMStoreFloat4(&tr->rotation, rot);
    }
  }

  // ライトスリット揺れ
  for (size_t i = 0; i < m_lightBeams.size(); ++i) {
    auto *tr = ctx.world.Get<components::Transform>(m_lightBeams[i]);
    auto *mr = ctx.world.Get<components::MeshRenderer>(m_lightBeams[i]);
    if (tr && mr) {
      float phase = m_lightPhase * 0.7f + static_cast<float>(i) * 0.6f;
      tr->position.x = (-36.0f + static_cast<float>(i) * 18.0f) +
                       std::sin(phase) * 6.0f;
      tr->rotation = {0.0f, std::sin(phase * 0.8f) * 0.08f, 0.0f, 1.0f};
      mr->color.w = 0.18f + 0.12f * (0.5f + 0.5f * std::sin(phase * 1.6f));
    }
  }

  // バッジの微動
  for (size_t i = 0; i < m_badgeEntities.size(); ++i) {
    if (auto *badge = ctx.world.Get<components::UIText>(m_badgeEntities[i])) {
      auto style = m_badgeStyle;
      float wobble =
          0.75f + 0.25f * (0.5f + 0.5f * std::sin(m_time * 1.8f + i));
      style.color.x *= wobble;
      style.color.y *= wobble;
      style.color.z *= wobble;
      badge->style = style;
    }
  }

  // テキストの軽いアニメーション
  if (auto *startText = ctx.world.Get<components::UIText>(m_startEntity)) {
    auto style = m_startStyle;
    float pulse = 0.65f + 0.35f * (0.5f + 0.5f * std::sin(m_time * 4.0f));
    style.color.w *= pulse;
    style.shadowColor.w *= pulse;
    startText->style = style;
  }

  if (auto *hint = ctx.world.Get<components::UIText>(m_hintEntity)) {
    auto style = m_hintStyle;
    float wave = 0.55f + 0.45f * (0.5f + 0.5f * std::sin(m_time * 2.0f));
    style.color.x *= wave;
    style.color.y *= wave;
    style.color.z *= wave;
    hint->style = style;
  }

  // ESCで終了
  if (ctx.input.GetKeyDown(VK_ESCAPE)) {
    ctx.shouldClose = true;
  }
}

void TitleScene::OnExit(core::GameContext &ctx) {
  LOG_INFO("TitleScene", "OnExit");
  // UI破棄
  ctx.world.DestroyEntity(m_titleTextEntity);
  ctx.world.DestroyEntity(m_subTextEntity);
  ctx.world.DestroyEntity(m_startEntity);
  ctx.world.DestroyEntity(m_hintEntity);
  ctx.world.DestroyEntity(m_footerEntity);
  for (auto e : m_badgeEntities) {
    ctx.world.DestroyEntity(e);
  }
  m_badgeEntities.clear();

  // 3Dオブジェクト破棄
  ctx.world.DestroyEntity(m_cameraEntity);
  ctx.world.DestroyEntity(m_ballEntity);
  ctx.world.DestroyEntity(m_clubEntity);
  ctx.world.DestroyEntity(m_floorEntity);
  for (auto e : m_decorEntities) {
    ctx.world.DestroyEntity(e);
  }
  m_decorEntities.clear();
  for (auto e : m_lightBeams) {
    ctx.world.DestroyEntity(e);
  }
  m_lightBeams.clear();
}

} // namespace game::scenes
