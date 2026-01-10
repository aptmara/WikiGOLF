/**
 * @file WikiTerrainSystem.cpp
 * @brief Wiki地形生成システム実装
 */

#include "WikiTerrainSystem.h"
#include "../../core/GameContext.h"
#include "../../core/Logger.h"
#include "../../core/StringUtils.h"
#include "../../ecs/World.h"
#include "../components/MeshRenderer.h"
#include "../components/PhysicsComponents.h"
#include "../components/Transform.h"
#include "../components/WikiComponents.h"
#include "TerrainGenerator.h"
#include "WikiClient.h"

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
                                   const std::string &pageTitle,
                                   const graphics::WikiTextureResult &result,
                                   float fieldWidth, float fieldDepth) {
  Clear(ctx);

  LOG_INFO("WikiTerrain", "Building field {}x{} with {} images, {} headings",
           fieldWidth, fieldDepth, result.images.size(),
           result.headings.size());

  CreateFloor(ctx, result, fieldWidth, fieldDepth, pageTitle);
  CreateWalls(ctx, fieldWidth, fieldDepth);
  // CreateImageObstacles(ctx, result, fieldWidth, fieldDepth);
}

void WikiTerrainSystem::CreateFloor(core::GameContext &ctx,
                                    const graphics::WikiTextureResult &result,
                                    float width, float depth,
                                    const std::string &pageTitle) {
  // 地形生成設定
  TerrainConfig config;
  config.worldWidth = width;
  config.worldDepth = depth;
  config.resolutionX = 128; // 高解像度
  config.resolutionZ = 128;
  config.heightScale = 1.5f; // 高低差を抑えて読みやすく

  // バイオーム決定 (カテゴリベース)
  WikiClient client;
  auto categories = client.FetchPageCategories(pageTitle);

  int biome = 0; // Default: 0
  bool found = false;

  for (const auto &cat : categories) {
    if (cat.find("歴史") != std::string::npos ||
        cat.find("戦争") != std::string::npos ||
        cat.find("事件") != std::string::npos ||
        cat.find("政治") != std::string::npos ||
        cat.find("古代") != std::string::npos) {
      biome = 1;
      found = true;
      break;
    }
    if (cat.find("科学") != std::string::npos ||
        cat.find("技術") != std::string::npos ||
        cat.find("数学") != std::string::npos ||
        cat.find("物理") != std::string::npos ||
        cat.find("コンピュータ") != std::string::npos ||
        cat.find("宇宙") != std::string::npos) {
      biome = 2;
      found = true;
      break;
    }
    if (cat.find("地理") != std::string::npos ||
        cat.find("地形") != std::string::npos ||
        cat.find("生物") != std::string::npos ||
        cat.find("植物") != std::string::npos ||
        cat.find("動物") != std::string::npos ||
        cat.find("山") != std::string::npos) {
      biome = 3;
      found = true;
      break;
    }
  }

  if (!found) {
    std::hash<std::string> hasher;
    size_t h = hasher(pageTitle);
    biome = h % 4;
  }

  XMFLOAT4 terrainColor = {1.0f, 1.0f, 1.0f, 1.0f};

  switch (biome) {
  case 0: // 草原
    config.friction = 0.5f;
    config.restitution = 0.3f;
    terrainColor = {0.4f, 0.8f, 0.4f, 1.0f};
    break;
  case 1: // 砂漠
    config.friction = 2.5f;
    config.restitution = 0.1f;
    config.heightScale = 2.5f;
    terrainColor = {0.9f, 0.8f, 0.5f, 1.0f};
    break;
  case 2: // 氷原
    config.friction = 0.05f;
    config.restitution = 0.6f;
    config.heightScale = 1.0f;
    terrainColor = {0.8f, 0.9f, 1.0f, 1.0f};
    break;
  case 3: // 岩場
    config.friction = 0.6f;
    config.restitution = 0.8f;
    config.heightScale = 3.0f;
    terrainColor = {0.6f, 0.5f, 0.5f, 1.0f};
    break;
  }

  std::string seedText = pageTitle;

  // リンクのワールド座標を計算
  std::vector<DirectX::XMFLOAT2> holePositions;
  float texW = (float)result.width;
  float texH = (float)result.height;

  for (const auto &link : result.links) {
    float texCenterX = link.x + link.width * 0.5f;
    float texCenterY = link.y + link.height * 0.5f;
    float worldX = (texCenterX / texW - 0.5f) * width;
    float worldZ = (0.5f - texCenterY / texH) * depth;
    holePositions.push_back({worldX, worldZ});
  }

  // 地形データ生成
  m_terrainData = std::make_shared<TerrainData>(
      TerrainGenerator::GenerateTerrain(seedText, holePositions, config));

  // 単一メッシュを生成
  auto meshHandle = ctx.resource.CreateDynamicMesh(
      "TerrainFull", m_terrainData->vertices, m_terrainData->indices);

  // エンティティ作成
  auto e = ctx.world.CreateEntity();
  auto &t = ctx.world.Add<Transform>(e);
  t.position = {0.0f, 0.0f, 0.0f};
  t.scale = {1.0f, 1.0f, 1.0f};

  auto &mr = ctx.world.Add<MeshRenderer>(e);
  mr.mesh = meshHandle;
  mr.shader =
      ctx.resource.LoadShader("Terrain", L"Assets/shaders/TerrainVS.hlsl",
                              L"Assets/shaders/TerrainPS.hlsl");
  mr.color = terrainColor;
  if (result.srv) {
    mr.textureSRV = result.srv;
    mr.hasTexture = true;
  }

  auto &rb = ctx.world.Add<RigidBody>(e);
  rb.isStatic = true;
  rb.restitution = 0.2f;
  rb.rollingFriction = 0.5f;

  auto &tc = ctx.world.Add<TerrainCollider>(e);
  tc.data = m_terrainData;
  m_floorEntity = e;

  m_entities.push_back(e);

  LOG_INFO("WikiTerrain", "Generated terrain mesh with {} vertices",
           m_terrainData->vertices.size());
}

void WikiTerrainSystem::CreateWalls(core::GameContext &ctx, float width,
                                    float depth) {
  float wallHeight = 8.0f;
  float wallThickness = 6.0f;
  float halfW = width * 0.5f;
  float halfD = depth * 0.5f;

  struct WallDef {
    float x, z, w, d;
  };
  WallDef walls[] = {
      {0.0f, halfD + wallThickness * 0.5f, width + wallThickness * 2,
       wallThickness},
      {0.0f, -halfD - wallThickness * 0.5f, width + wallThickness * 2,
       wallThickness},
      {-halfW - wallThickness * 0.5f, 0.0f, wallThickness, depth},
      {halfW + wallThickness * 0.5f, 0.0f, wallThickness, depth}};

  for (const auto &w : walls) {
    auto e = ctx.world.CreateEntity();
    auto &t = ctx.world.Add<Transform>(e);
    t.position = {w.x, wallHeight * 0.5f, w.z};
    t.scale = {w.w, wallHeight, w.d};

    auto &mr = ctx.world.Add<MeshRenderer>(e);
    mr.mesh = ctx.resource.LoadMesh("builtin/cube");
    mr.shader = ctx.resource.LoadShader("Basic", L"Assets/shaders/BasicVS.hlsl",
                                        L"Assets/shaders/BasicPS.hlsl");
    mr.color = {0.8f, 0.8f, 0.8f, 1.0f};

    auto &rb = ctx.world.Add<RigidBody>(e);
    rb.isStatic = true;
    rb.restitution = 0.8f;

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

  if (texW <= 0.0f || texH <= 0.0f)
    return;

  for (const auto &img : result.images) {
    float centerX = img.x + img.width * 0.5f;
    float centerY = img.y + img.height * 0.5f;

    float worldX = (centerX / texW - 0.5f) * fieldWidth;
    float worldZ = (0.5f - centerY / texH) * fieldDepth;
    float worldW = (img.width / texW) * fieldWidth;
    float worldD = (img.height / texH) * fieldDepth;

    float height = 2.0f;

    auto e = ctx.world.CreateEntity();
    auto &t = ctx.world.Add<Transform>(e);
    t.position = {worldX, height * 0.5f, worldZ};
    t.scale = {worldW, height, worldD};

    auto &mr = ctx.world.Add<MeshRenderer>(e);
    mr.mesh = ctx.resource.LoadMesh("builtin/cube");
    mr.shader = ctx.resource.LoadShader("Basic", L"Assets/shaders/BasicVS.hlsl",
                                        L"Assets/shaders/BasicPS.hlsl");
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
    float fieldWidth, float fieldDepth) {}

float WikiTerrainSystem::GetHeight(float x, float z) const {
  if (!m_terrainData)
    return 0.0f;

  float worldW = m_terrainData->config.worldWidth;
  float worldD = m_terrainData->config.worldDepth;
  int resX = m_terrainData->config.resolutionX;
  int resZ = m_terrainData->config.resolutionZ;

  float u = x / worldW + 0.5f;
  float v = 0.5f - z / worldD;

  if (u < 0.0f || u >= 1.0f || v < 0.0f || v >= 1.0f)
    return 0.0f;

  int ix = (int)(u * (resX - 1));
  int iz = (int)(v * (resZ - 1));

  if (ix < 0)
    ix = 0;
  if (ix >= resX)
    ix = resX - 1;
  if (iz < 0)
    iz = 0;
  if (iz >= resZ)
    iz = resZ - 1;

  return m_terrainData->heightMap[iz * resX + ix];
}

} // namespace game::systems
