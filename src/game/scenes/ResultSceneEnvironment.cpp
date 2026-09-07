/**
 * @file ResultSceneEnvironment.cpp
 * @brief ResultSceneの責務別実装です。
 */

#define NOMINMAX
#include "ResultScene.h"
#include "../../audio/AudioSystem.h"
#include "../../core/GameContext.h"
#include "../../core/Input.h"
#include "../../core/Logger.h"
#include "../../core/SceneManager.h"
#include "../../core/StringUtils.h"
#include "../../ecs/World.h"
#include "../../graphics/GraphicsDevice.h"
#include "../../graphics/TextRenderer.h"
#include "../components/Camera.h"
#include "../components/MeshRenderer.h"
#include "../components/Skybox.h"
#include "../components/Transform.h"
#include "../components/UIButton.h"
#include "../components/UIImage.h"
#include "../components/UIText.h"
#include "../components/WikiComponents.h"
#include "../systems/SkyboxRenderSystem.h"
#include "TitleScene.h"
#include "WikiGolfScene.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <random>

namespace game::scenes {

using namespace game::components;
using namespace DirectX;

void ResultScene::CreateVisualEnvironment(core::GameContext &ctx) {
  auto basicShader = ctx.resource.LoadShader(
      "Basic", L"Assets/shaders/BasicVS.hlsl", L"Assets/shaders/BasicPS.hlsl");
  auto globeMesh = ctx.resource.LoadMesh(
      "Assets/models/Wikipedia_puzzle_globe_3D_render.stl");
  auto planeMesh = ctx.resource.LoadMesh("builtin/plane");
  auto ringMesh = ctx.resource.LoadMesh(
      "builtin/torus");

  // 地面のエンティティを生成します。
  m_floorEntity = CreateEntity(ctx.world);
  auto &floorTr = ctx.world.Add<Transform>(m_floorEntity);
  floorTr.position = {0.0f, -2.0f, 0.0f};
  floorTr.scale = {200.0f, 1.0f, 200.0f};
  auto &floorMr = ctx.world.Add<MeshRenderer>(m_floorEntity);
  floorMr.mesh = planeMesh;
  floorMr.shader = basicShader;
  floorMr.color = {0.1f, 0.1f, 0.2f, 0.9f};
  floorMr.isTransparent = true;
  floorMr.isVisible = true;

  // 地球儀のエンティティを生成します。
  m_globeEntity = CreateEntity(ctx.world);
  auto &globeTr = ctx.world.Add<Transform>(m_globeEntity);
  globeTr.position = {0.0f, 2.5f, 0.0f};
  globeTr.scale = {2.5f, 2.5f, 2.5f};
  auto &globeMr = ctx.world.Add<MeshRenderer>(m_globeEntity);
  globeMr.mesh = globeMesh;
  globeMr.shader = basicShader;
  globeMr.color = {1.0f, 1.0f, 1.0f, 1.0f};
  globeMr.isVisible = true;

  // 3枚の装飾リングを生成します。
  for (int i = 0; i < 3; ++i) {
    auto e = CreateEntity(ctx.world);
    auto &t = ctx.world.Add<Transform>(e);
    t.position = globeTr.position;
    float r = 4.0f + i * 1.5f;
    t.scale = {r, 0.1f, r};

    auto &mr = ctx.world.Add<MeshRenderer>(e);
    mr.mesh = ringMesh;
    mr.shader = basicShader;
    mr.color = XMFLOAT4(0.0f, 0.8f, 1.0f, 0.5f);
    if (i == 1) {
      mr.color = XMFLOAT4(1.0f, 0.84f, 0.0f, 0.8f);
    }
    mr.isTransparent = true;
    mr.isVisible = true;

    RingObject ro;
    ro.entity = e;
    ro.baseRadius = r;
    ro.phase = (float)i * 1.5f;
    ro.rotationSpeed = 0.3f + (float)i * 0.1f;
    m_rings.push_back(ro);
  }

  // カメラを生成します。
  m_cameraEntity = CreateEntity(ctx.world);
  auto &cam = ctx.world.Add<Camera>(m_cameraEntity);
  cam.fov = XMConvertToRadians(60.0f);
  cam.nearZ = 0.1f;
  cam.farZ = 300.0f;
  cam.isMainCamera = true;
  auto &camTr = ctx.world.Add<Transform>(m_cameraEntity);
  camTr.position = {0.0f, 4.0f, -15.0f};
}

/**
 * @brief 豪華な演出のUI表示要素を生成します。
 */

} // namespace game::scenes
