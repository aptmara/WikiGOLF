/**
 * @file WikiTerrainSystem.cpp
 * @brief Wiki地形生成システム実装
 */

#include "WikiTerrainSystem.h"
#include "../../core/GameContext.h"
#include "../../core/Logger.h"
#include "../../ecs/World.h"
#include "../components/MeshRenderer.h"
#include "../components/PhysicsComponents.h"
#include "../components/Transform.h"


namespace game::systems {

using namespace DirectX;
using namespace game::components;

void WikiTerrainSystem::Clear(core::GameContext &ctx) {
  for (auto e : m_entities) {
    if (ctx.world.IsAlive(e)) {
      ctx.world.DestroyEntity(e);
    }
  }
  m_entities.clear();
  m_floorEntity = 0xFFFFFFFF;
}

void WikiTerrainSystem::BuildField(core::GameContext &ctx,
                                   const graphics::WikiTextureResult &result,
                                   float fieldWidth, float fieldDepth) {
  Clear(ctx);

  LOG_INFO("WikiTerrain", "Building field {}x{} with {} images, {} headings",
           fieldWidth, fieldDepth, result.images.size(),
           result.headings.size());

  CreateFloor(ctx, result, fieldWidth, fieldDepth);
  CreateWalls(ctx, fieldWidth, fieldDepth);
  CreateImageObstacles(ctx, result, fieldWidth, fieldDepth);
  CreateHeadingSteps(ctx, result, fieldWidth, fieldDepth);
}

void WikiTerrainSystem::CreateFloor(core::GameContext &ctx,
                                    const graphics::WikiTextureResult &result,
                                    float width, float depth) {
  auto e = ctx.world.CreateEntity();
  auto &t = ctx.world.Add<Transform>(e);
  t.position = {0.0f, 0.0f, 0.0f};
  t.scale = {width, 0.5f, depth};

  auto &mr = ctx.world.Add<MeshRenderer>(e);
  mr.mesh = ctx.resource.LoadMesh("builtin/plane");
  mr.shader = ctx.resource.LoadShader("Basic", L"shaders/BasicVS.hlsl",
                                      L"shaders/BasicPS.hlsl");
  mr.color = {1.0f, 1.0f, 1.0f, 1.0f};

  if (result.srv) {
    mr.textureSRV = result.srv;
    mr.hasTexture = true;
  }

  auto &rb = ctx.world.Add<RigidBody>(e);
  rb.isStatic = true;
  rb.restitution = 0.2f; // 床はあまり跳ねない
  rb.rollingFriction = 0.5f;

  auto &col = ctx.world.Add<Collider>(e);
  col.type = ColliderType::Box;
  col.size = {width * 0.5f, 0.25f, depth * 0.5f}; // Planeは薄いのでYは小さく

  m_floorEntity = e;
  m_entities.push_back(e);
}

void WikiTerrainSystem::CreateWalls(core::GameContext &ctx, float width,
                                    float depth) {
  float wallHeight = 2.0f;
  float wallThickness = 1.0f;
  float halfW = width * 0.5f;
  float halfD = depth * 0.5f;

  // 4方向の壁
  struct WallDef {
    float x, z, w, d;
  };
  WallDef walls[] = {
      {0.0f, halfD + wallThickness * 0.5f, width, wallThickness},  // 奥
      {0.0f, -halfD - wallThickness * 0.5f, width, wallThickness}, // 手前
      {-halfW - wallThickness * 0.5f, 0.0f, wallThickness, depth}, // 左
      {halfW + wallThickness * 0.5f, 0.0f, wallThickness, depth}   // 右
  };

  for (const auto &w : walls) {
    auto e = ctx.world.CreateEntity();
    auto &t = ctx.world.Add<Transform>(e);
    t.position = {w.x, wallHeight * 0.5f, w.z};
    t.scale = {w.w, wallHeight, w.d};

    auto &mr = ctx.world.Add<MeshRenderer>(e);
    mr.mesh = ctx.resource.LoadMesh("builtin/cube");
    mr.shader = ctx.resource.LoadShader("Basic", L"shaders/BasicVS.hlsl",
                                        L"shaders/BasicPS.hlsl");
    mr.color = {0.8f, 0.8f, 0.8f, 1.0f}; // グレーの壁

    auto &rb = ctx.world.Add<RigidBody>(e);
    rb.isStatic = true;
    rb.restitution = 0.8f; // 壁はよく跳ねる（クッションショット用）

    auto &col = ctx.world.Add<Collider>(e);
    col.type = ColliderType::Box;
    col.size = {w.w * 0.5f, wallHeight * 0.5f, w.d * 0.5f};

    m_entities.push_back(e);
  }
}

void WikiTerrainSystem::CreateImageObstacles(
    core::GameContext &ctx, const graphics::WikiTextureResult &result,
    float fieldWidth, float fieldDepth) {
  float texW = (float)result.width;
  float texH = (float)result.height;

  for (const auto &img : result.images) {
    // 画像のワールド座標への変換
    float centerX = img.x + img.width * 0.5f;
    float centerY = img.y + img.height * 0.5f;

    float worldX = (centerX / texW - 0.5f) * fieldWidth;
    float worldZ = (0.5f - centerY / texH) * fieldDepth;
    float worldW = (img.width / texW) * fieldWidth;
    float worldD = (img.height / texH) * fieldDepth;

    float height = 0.6f; // 障害物の高さ

    // 本体
    auto e = ctx.world.CreateEntity();
    auto &t = ctx.world.Add<Transform>(e);
    t.position = {worldX, height * 0.5f, worldZ};
    t.scale = {worldW, height, worldD};

    auto &mr = ctx.world.Add<MeshRenderer>(e);
    mr.mesh = ctx.resource.LoadMesh("builtin/cube");
    mr.shader = ctx.resource.LoadShader("Basic", L"shaders/BasicVS.hlsl",
                                        L"shaders/BasicPS.hlsl");
    mr.color = {0.9f, 0.9f, 0.9f, 1.0f};

    auto &rb = ctx.world.Add<RigidBody>(e);
    rb.isStatic = true;
    rb.restitution = 0.5f;

    auto &col = ctx.world.Add<Collider>(e);
    col.type = ColliderType::Box;
    col.size = {worldW * 0.5f, height * 0.5f, worldD * 0.5f};

    m_entities.push_back(e);
  }
}

void WikiTerrainSystem::CreateHeadingSteps(
    core::GameContext &ctx, const graphics::WikiTextureResult &result,
    float fieldWidth, float fieldDepth) {
  float texW = (float)result.width;
  float texH = (float)result.height;

  for (const auto &head : result.headings) {
    // 見出しのワールド座標への変換
    float centerX = head.x + head.width * 0.5f;
    float centerY = head.y + head.height * 0.5f;

    float worldX = (centerX / texW - 0.5f) * fieldWidth;
    float worldZ = (0.5f - centerY / texH) * fieldDepth;
    float worldW = (head.width / texW) * fieldWidth;
    float worldD = (head.height / texH) * fieldDepth;

    // 見出し部分は少し盛り上げる（0.2f）
    float height = 0.2f;

    auto e = ctx.world.CreateEntity();
    auto &t = ctx.world.Add<Transform>(e);
    t.position = {worldX, height * 0.5f, worldZ};
    t.scale = {worldW, height, worldD};

    // 見出し部分は透明なコライダーとして配置（見た目は床テクスチャにあるので）
    // ただしデバッグ用に半透明表示しておく
    auto &mr = ctx.world.Add<MeshRenderer>(e);
    mr.mesh = ctx.resource.LoadMesh("builtin/cube");
    mr.shader = ctx.resource.LoadShader("Basic", L"shaders/BasicVS.hlsl",
                                        L"shaders/BasicPS.hlsl");
    mr.color = {0.5f, 0.5f, 1.0f, 0.3f}; // 青半透明

    auto &rb = ctx.world.Add<RigidBody>(e);
    rb.isStatic = true;

    auto &col = ctx.world.Add<Collider>(e);
    col.type = ColliderType::Box;
    col.size = {worldW * 0.5f, height * 0.5f, worldD * 0.5f};

    m_entities.push_back(e);
  }
}

} // namespace game::systems
