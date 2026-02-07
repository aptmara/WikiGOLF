/**
 * @file TitleScene.cpp
 * @brief タイトル画面シーン実装
 */

#include "TitleScene.h"
#include "../../core/GameContext.h"
#include "../../core/Input.h"
#include "../../core/Logger.h"
#include "../../core/SceneManager.h"
#include "../components/Camera.h"
#include "../components/MeshRenderer.h"
#include "../components/Skybox.h"
#include "../components/Transform.h"
#include "../components/UIButton.h"
#include "../components/UIText.h"
#include "LoadingScene.h"
#include "WikiGolfScene.h"
#include <DirectXMath.h>
#include <Windows.h>
#include <algorithm>
#include <format>

using namespace DirectX;

namespace game::scenes {

void TitleScene::OnEnter(core::GameContext &ctx) {
  LOG_INFO("TitleScene", "OnEnter");

  m_time = 0.0f;
  m_cameraPitch = -0.12f;
  m_cameraDist = 14.0f;
  m_cameraHeight = 5.0f;
  m_clubs.clear();
  m_ringObjects.clear();
  m_reflections.clear();
  m_particles.clear();
  m_uiElements.clear();

  // カメラ
  m_cameraEntity = CreateEntity(ctx.world);
  auto &camTr = ctx.world.Add<components::Transform>(m_cameraEntity);
  camTr.position = {0.0f, m_cameraHeight, -m_cameraDist};
  XMStoreFloat4(&camTr.rotation,
                XMQuaternionRotationRollPitchYaw(m_cameraPitch, 0.0f, 0.0f));
  auto &cam = ctx.world.Add<components::Camera>(m_cameraEntity);
  cam.farZ = 750.0f; // 描画距離を拡張

  // スカイボックス（キューブマップは起動後に設定される想定）
  m_skyboxEntity = CreateEntity(ctx.world);
  ctx.world.Add<components::Skybox>(m_skyboxEntity);

  // 床
  m_floorEntity = CreateEntity(ctx.world);
  auto &floorTr = ctx.world.Add<components::Transform>(m_floorEntity);
  floorTr.scale = {28.0f, 0.6f, 28.0f};

  auto &floorMr = ctx.world.Add<components::MeshRenderer>(m_floorEntity);
  floorMr.mesh = ctx.resource.LoadMesh("builtin/cube");
  floorMr.shader = ctx.resource.LoadShader("Basic", L"shaders/BasicVS.hlsl",
                                           L"shaders/BasicPS.hlsl");
  floorMr.color = {0.08f, 0.1f, 0.14f, 1.0f};

  // 演出用ボール
  m_ballEntity = CreateEntity(ctx.world);
  auto &ballTr = ctx.world.Add<components::Transform>(m_ballEntity);
  ballTr.position = {0.0f, 1.2f, 0.0f};
  ballTr.scale = {1.3f, 1.3f, 1.3f};

  auto &ballMr = ctx.world.Add<components::MeshRenderer>(m_ballEntity);
  ballMr.mesh = ctx.resource.LoadMesh("builtin/sphere");
  ballMr.shader = ctx.resource.LoadShader("Basic", L"shaders/BasicVS.hlsl",
                                          L"shaders/BasicPS.hlsl");
  ballMr.color = {1.0f, 0.65f, 0.2f, 1.0f};

  // タイトルテキスト
  auto titleEntity = CreateEntity(ctx.world);
  auto &title = ctx.world.Add<components::UIText>(titleEntity);
  title.text = L"Wiki Golf";
  title.x = 640.0f;
  title.y = 140.0f;
  title.style = graphics::TextStyle::LuxuryTitle();
  title.layer = 200;

  // スタートガイド
  auto guideEntity = CreateEntity(ctx.world);
  auto &guide = ctx.world.Add<components::UIText>(guideEntity);
  guide.text = L"Press Space / Enter / Click to Start";
  guide.x = 640.0f;
  guide.y = 260.0f;
  guide.style = graphics::TextStyle::Guide();
  guide.style.fontSize = 30.0f;
  guide.layer = 190;

  // スタートボタン（マウス操作向けのヒット領域）
  auto startBtn = CreateEntity(ctx.world);
  auto &btn = ctx.world.Add<components::UIButton>(startBtn);
  btn = components::UIButton::Create(L"スタート", "start_game", 440.0f, 320.0f,
                                     400.0f, 90.0f);
  btn.textStyle = graphics::TextStyle::LuxuryButton();
  btn.normalColor = {0.05f, 0.2f, 0.35f, 0.8f};
  btn.hoverColor = {0.08f, 0.3f, 0.45f, 0.9f};
  btn.pressedColor = {0.02f, 0.15f, 0.25f, 0.9f};
  btn.disabledColor = {0.05f, 0.05f, 0.05f, 0.5f};

  m_uiElements.push_back({titleEntity, title.x, title.y, 1.0f, 1.0f, false,
                          false, title.text, {1.0f, 1.0f, 1.0f, 1.0f}});
  m_uiElements.push_back({guideEntity, guide.x, guide.y, 1.0f, 1.0f, false,
                          false, guide.text, {1.0f, 1.0f, 1.0f, 1.0f}});
}

void TitleScene::OnUpdate(core::GameContext &ctx) {
  m_time += ctx.dt;

  // カメラをわずかにスイング
  if (auto *camTr = ctx.world.Get<components::Transform>(m_cameraEntity)) {
    float sway = std::sin(m_time * 0.35f) * 0.15f;
    float bob = std::sin(m_time * 0.7f) * 0.2f;
    camTr->position = {std::sin(m_time * 0.25f) * 1.0f,
                       m_cameraHeight + bob, -m_cameraDist + sway};
    XMStoreFloat4(
        &camTr->rotation,
        XMQuaternionRotationRollPitchYaw(m_cameraPitch + bob * 0.05f,
                                         sway * 0.5f, 0.0f));
  }

  // ボール回転
  if (auto *ballTr = ctx.world.Get<components::Transform>(m_ballEntity)) {
    XMStoreFloat4(&ballTr->rotation,
                  XMQuaternionRotationRollPitchYaw(m_time * 0.7f,
                                                   m_time * 0.9f, 0.0f));
  }

  // 簡易UIアニメーション（ガイドの点滅）
  if (m_uiElements.size() > 1) {
    auto &guideElem = m_uiElements[1];
    if (auto *ui = ctx.world.Get<components::UIText>(guideElem.entity)) {
      float pulse = 0.5f + 0.5f * std::sin(m_time * 4.0f);
      ui->style.color.w = 0.6f + 0.4f * pulse;
    }
  }

  // 入力チェック
  bool startRequested = false;
  startRequested |= ctx.input.GetKeyDown(VK_SPACE);
  startRequested |= ctx.input.GetKeyDown(VK_RETURN);
  startRequested |= ctx.input.GetMouseButtonDown(0);

  if (startRequested && ctx.sceneManager) {
    LOG_INFO("TitleScene", "Start requested, moving to LoadingScene");
    auto nextFactory = []() {
      return std::make_unique<WikiGolfScene>();
    };
    ctx.sceneManager->ChangeScene(
        std::make_unique<LoadingScene>(nextFactory));
  }
}

void TitleScene::OnExit(core::GameContext &ctx) {
  LOG_INFO("TitleScene", "OnExit");
  Scene::OnExit(ctx);
}

} // namespace game::scenes
