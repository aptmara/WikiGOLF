/**
 * @file LoadingSceneGameplay.cpp
 * @brief LoadingSceneの責務別実装です。
*/

#include "LoadingScene.h"
#include "../../core/GameContext.h"
#include "../../core/Input.h"
#include "../../core/Logger.h"
#include "../../core/SceneManager.h"
#include "../../graphics/TextRenderer.h"
#include "../../graphics/WikiTextureGenerator.h"
#include "../components/Camera.h"
#include "../components/MeshRenderer.h"
#include "../components/Transform.h"
#include "../components/UIText.h"
#include "../systems/WikiClient.h"
#include "../systems/WikiShortestPath.h"
#include "LoadingSceneUtils.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <d2d1_1.h>
#include <filesystem>
#include <random>
#include <thread>

namespace game::scenes {

void LoadingScene::BuildGameplayPreloadQueue() {
  // クラブアイコン（Club_01〜Club_09）
  static const std::array<const char *, 9> kClubTextures = {
      "Assets/textures/Club_01_1W_Driver.png",
      "Assets/textures/Club_02_3W_Wood.png",
      "Assets/textures/Club_03_5W_Wood.png",
      "Assets/textures/Club_04_5I_Iron.png",
      "Assets/textures/Club_05_7I_Iron.png",
      "Assets/textures/Club_06_9I_Iron.png",
      "Assets/textures/Club_07_PW_PitchingWedge.png",
      "Assets/textures/Club_08_SW_SandWedge.png",
      "Assets/textures/Club_09_PT_Putter.png",
  };

  // ミニマップ用アイコン、地形/判定バッジUI画像
  static const std::array<const char *, 11> kUiTextures = {
      "Assets/textures/golf_ball_icon_transparent.png",
      "Assets/textures/golf_hole_icon_transparent.png",
      "Assets/textures/ui_terrain_bunker.png",
      "Assets/textures/ui_terrain_fairway.png",
      "Assets/textures/ui_terrain_green.png",
      "Assets/textures/ui_terrain_ob.png",
      "Assets/textures/ui_terrain_rough.png",
      "Assets/textures/ui_judge_great.png",
      "Assets/textures/ui_judge_miss.png",
      "Assets/textures/ui_judge_nice.png",
      "Assets/textures/ui_judge_perfect.png",
  };

  // ショット/判定/地形/カップ/ゴール/ワープ/OB経路で実際に再生されるSE
  static const std::array<const char *, 22> kGameplaySounds = {
      "Assets/sounds/se_shot.mp3",
      "Assets/sounds/se_shot_hard.mp3",
      "Assets/sounds/se_shot_soft.mp3",
      "Assets/sounds/se_shot_charge.mp3",
      "Assets/sounds/se_cancel.mp3",
      "Assets/sounds/se_judge_perfect.mp3",
      "Assets/sounds/se_judge_great.mp3",
      "Assets/sounds/se_judge_nice.mp3",
      "Assets/sounds/se_judge_miss.mp3",
      "Assets/sounds/se_judge_ob.mp3",
      "Assets/sounds/judge_Bad.wav",
      "Assets/sounds/se_Fairway.wav",
      "Assets/sounds/se_Fairway.mp3",
      "Assets/sounds/se_Rough.wav",
      "Assets/sounds/se_Rough.mp3",
      "Assets/sounds/se_Bunker_new.mp3",
      "Assets/sounds/se_Bunker.mp3",
      "Assets/sounds/se_Green.mp3",
      "Assets/sounds/se_OB.wav",
      "Assets/sounds/se_cupin.mp3",
      "Assets/sounds/se_holeInOne.mp3",
      "Assets/sounds/se_warp.mp3",
  };

  for (const char *path : kClubTextures) {
    if (!std::filesystem::exists(path))
      continue;
    std::string p = path;
    m_preloadTasks.emplace_back([p](core::GameContext &ctx) {
      if (ctx.textRenderer)
        ctx.textRenderer->LoadBitmapFromFile(p);
    });
  }

  for (const char *path : kUiTextures) {
    if (!std::filesystem::exists(path))
      continue;
    std::string p = path;
    m_preloadTasks.emplace_back([p](core::GameContext &ctx) {
      if (ctx.textRenderer)
        ctx.textRenderer->LoadBitmapFromFile(p);
    });
  }

  for (const char *path : kGameplaySounds) {
    if (!std::filesystem::exists(path))
      continue;
    std::string p = path;
    m_preloadTasks.emplace_back(
        [p](core::GameContext &ctx) { ctx.resource.LoadAudio(p); });
  }

  LOG_INFO("LoadingScene", "Gameplay preload queue built: {} tasks",
           m_preloadTasks.size());
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
  static std::mt19937 rng(std::random_device{}());

  const float renderRadius = BALL_RADIUS * BALL_MODEL_SCALE;
  const float spawnHalfX = ARENA_HALF_WIDTH - renderRadius - 0.5f;
  const float spawnHalfZ = ARENA_HALF_DEPTH - renderRadius - 0.2f;

  std::uniform_real_distribution<float> distX(-spawnHalfX, spawnHalfX);
  std::uniform_real_distribution<float> distZ(-spawnHalfZ, spawnHalfZ);
  std::uniform_real_distribution<float> distHeight(24.0f, 40.0f);

  // 積み上げ演出では横速度を弱める
  std::uniform_real_distribution<float> distVelX(-6.0f, 6.0f);
  std::uniform_real_distribution<float> distVelZ(-4.0f, 4.0f);
  std::uniform_real_distribution<float> spinDist(-1.5f, 1.5f);

  std::uniform_real_distribution<float> angleDist(-DirectX::XM_PI,
                                                  DirectX::XM_PI);

  auto entity = CreateEntity(ctx.world);

  auto &tr = ctx.world.Add<components::Transform>(entity);
  tr.position = {distX(rng), distHeight(rng), distZ(rng)};

  const float renderScale =
      BALL_RADIUS * 2.0f * BALL_MODEL_SCALE * MODEL_ASSET_SCALE_FACTOR;
  tr.scale = {renderScale, renderScale, renderScale};

  auto randomRotation = DirectX::XMQuaternionRotationRollPitchYaw(
      angleDist(rng) * 0.25f,
      angleDist(rng) * 0.25f,
      angleDist(rng) * 0.25f);
  DirectX::XMStoreFloat4(&tr.rotation, randomRotation);

  auto &mr = ctx.world.Add<components::MeshRenderer>(entity);
  mr.mesh = m_ballMeshHandle;
  mr.shader = ctx.resource.LoadShader("Basic",
                                      L"Assets/shaders/BasicVS.hlsl",
                                      L"Assets/shaders/BasicPS.hlsl");

  std::uniform_real_distribution<float> colorDist(0.0f, 1.0f);
  if (colorDist(rng) < 0.05f) {
    const float intensity = 1.8f;
    const std::array<DirectX::XMFLOAT3, 5> neonColors = {{
        {0.1f * intensity, 0.6f * intensity, 1.0f * intensity},
        {0.9f * intensity, 0.2f * intensity, 0.6f * intensity},
        {1.0f * intensity, 0.8f * intensity, 0.1f * intensity},
        {0.2f * intensity, 0.9f * intensity, 0.4f * intensity},
        {0.6f * intensity, 0.3f * intensity, 1.0f * intensity}
    }};

    std::uniform_int_distribution<size_t> paletteDist(0,
                                                      neonColors.size() - 1);
    DirectX::XMFLOAT3 c = neonColors[paletteDist(rng)];
    mr.color = {c.x, c.y, c.z, 1.0f};
  } else {
    mr.color = {1.0f, 1.0f, 1.0f, 1.0f};
  }

  mr.isVisible = true;
  mr.normalMapSRV = ctx.resource.LoadTextureSRV("Assets/models/golfball_n.png");
  mr.hasNormalMap = static_cast<bool>(mr.normalMapSRV);

  BallState ball{};
  ball.entity = entity;
  ball.velocity = {distVelX(rng), -12.0f, distVelZ(rng)};
  ball.angularVelocity = {
      spinDist(rng) * 0.8f,
      spinDist(rng),
      spinDist(rng) * 0.6f
  };
  ball.settled = false;

  m_balls.push_back(ball);
  m_spawnedCount++;
}

} // namespace game::scenes

