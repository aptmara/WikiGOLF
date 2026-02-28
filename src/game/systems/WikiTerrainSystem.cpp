/**
 * @file WikiTerrainSystem.cpp
 * @brief Wiki地形生成システム実装
 */

#include "WikiTerrainSystem.h"
#include "../../core/GameContext.h"
#include "../../core/Logger.h"
#include "../../core/StringUtils.h"
#include "../../ecs/World.h"
#include "../../resources/ResourceManager.h"
#include "../components/MeshRenderer.h"
#include "../components/PhysicsComponents.h"
#include "../components/Transform.h"
#include "../components/WikiComponents.h"
#include "TerrainGenerator.h"
#include "WikiClient.h"
#include <algorithm> // for std::max

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

  LOG_INFO("WikiTerrain", "Building field {}x{} with {} tiles", fieldWidth,
           fieldDepth, (int)result.tiles.size());

  CreateFloor(ctx, result, fieldWidth, fieldDepth, pageTitle);
  CreateWalls(ctx, fieldWidth, fieldDepth);
}

void WikiTerrainSystem::CreateFloor(core::GameContext &ctx,
                                    const graphics::WikiTextureResult &result,
                                    float width, float depth,
                                    const std::string &pageTitle) {
  // 1. 地形解像度・設定の決定
  int resX = 64;
  int resZ = static_cast<int>(depth);
  resZ = std::max(64, resZ);

  TerrainConfig config;
  config.worldWidth = width;
  config.worldDepth = depth;
  config.resolutionX = resX;
  config.resolutionZ = resZ;
  config.heightScale = 1.5f;

  // バイオーム決定
  WikiClient client;
  auto categories = client.FetchPageCategories(pageTitle);
  int biome = 0;
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

  // 物理パラメータの統一 (環境によらず一定)
  config.friction = 0.5f;    // 標準的な芝の摩擦
  config.restitution = 0.3f; // 標準的な反発係数

  switch (biome) {
  case 0: // 草原
    terrainColor = {0.4f, 0.8f, 0.4f, 1.0f};
    break;
  case 1: // 砂漠
    config.heightScale = 2.5f;
    terrainColor = {0.9f, 0.8f, 0.5f, 1.0f};
    break;
  case 2: // 氷原
    config.heightScale = 1.0f;
    terrainColor = {0.8f, 0.9f, 1.0f, 1.0f};
    break;
  case 3: // 岩場
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

  // 2. 地形データ生成（物理・基準用）
  m_terrainData = std::make_shared<TerrainData>(
      TerrainGenerator::GenerateTerrain(seedText, holePositions, config));

  // 3. 物理エンティティ作成（不可視）
  {
    auto e = ctx.world.CreateEntity();
    auto &transform = ctx.world.Add<Transform>(e);
    transform.position = {0.0f, 0.0f, 0.0f};

    auto &rb = ctx.world.Add<RigidBody>(e);
    rb.isStatic = true;
    rb.restitution = config.restitution;
    rb.rollingFriction = config.friction;

    auto &tc = ctx.world.Add<TerrainCollider>(e);
    tc.data = m_terrainData;

    m_floorEntity = e;
    m_entities.push_back(e);
  }

  // 4. タイルごとのビジュアルメッシュ生成とエンティティ作成
  std::vector<graphics::WikiTextureResult::Tile> tilesToProcess;
  // NOTE: WikiTextureResult::Tile is inside namespace graphics?
  // graphics namespace is used in WikiTextureGenerator.h.
  // result is graphics::WikiTextureResult.
  // Tile is nested struct.

  // Check proper type name: graphics::WikiTextureResult::Tile

  if (result.tiles.empty()) {
    graphics::WikiTextureResult::Tile legacyTile;
    legacyTile.texture = result.texture;
    legacyTile.srv = result.srv;
    legacyTile.width = result.width;
    legacyTile.height = result.height;
    legacyTile.offsetY = 0.0f;
    tilesToProcess.push_back(legacyTile);
  } else {
    tilesToProcess = result.tiles;
  }

  for (const auto &tile : tilesToProcess) {
    if (!tile.srv)
      continue;

    float vStart = tile.offsetY / (float)result.height;
    float vEnd = (tile.offsetY + (float)tile.height) / (float)result.height;

    float zTop = depth * (0.5f - vStart);
    float zBottom = depth * (0.5f - vEnd);
    float tileDepth = zTop - zBottom;
    float zCenter = (zTop + zBottom) * 0.5f;

    int tileResZ =
        std::max(2, (int)(resZ * (tile.height / (float)result.height)));
    std::vector<graphics::Vertex> vertices;
    std::vector<uint32_t> indices;

    for (int z = 0; z < tileResZ; ++z) {
      float vLocal = (float)z / (tileResZ - 1); // 0..1 (Tile内UV)
      float vGlobal = vStart + vLocal * (vEnd - vStart);
      float worldZ = depth * (0.5f - vGlobal);

      for (int x = 0; x < resX; ++x) {
        float u = (float)x / (resX - 1);
        float worldX = width * (u - 0.5f);

        float h = GetHeight(worldX, worldZ);

        float hL = GetHeight(worldX - 0.1f, worldZ);
        float hR = GetHeight(worldX + 0.1f, worldZ);
        float hD = GetHeight(worldX, worldZ - 0.1f);
        float hU = GetHeight(worldX, worldZ + 0.1f);
        XMVECTOR n = XMVectorSet(hL - hR, 0.2f, hD - hU, 0.0f);
        n = XMVector3Normalize(n);
        XMFLOAT3 normal;
        XMStoreFloat3(&normal, n);

        // マテリアルマップから頂点カラー決定
        float gridU = worldX / width + 0.5f;
        float gridV = 0.5f - worldZ / depth; // worldZは正手前が-なので反転
        int gx = std::clamp(static_cast<int>(gridU * (resX - 1) + 0.5f), 0,
                            resX - 1);
        int gz = std::clamp(static_cast<int>(gridV * (resZ - 1) + 0.5f), 0,
                            resZ - 1);
        uint8_t mat = m_terrainData->materialMap[gz * resX + gx];

        XMFLOAT4 vcolor;
        // マテリアル別に色味を少しバリエーション付ける
        switch (mat) {
        case 0: { // Fairway
          float shade = 0.9f + 0.1f * std::sin(worldX * 0.1f + worldZ * 0.08f);
          vcolor = {0.18f * shade, 0.62f * shade, 0.24f * shade, 1.0f};
          break;
        }
        case 1: { // Rough
          float shade =
              0.85f + 0.15f * std::cos(worldX * 0.12f + worldZ * 0.1f);
          vcolor = {0.12f * shade, 0.32f * shade, 0.14f * shade, 1.0f};
          break;
        }
        case 2: { // Bunker
          float shade =
              0.92f + 0.08f * std::sin(worldX * 0.05f + worldZ * 0.04f);
          vcolor = {0.88f * shade, 0.80f * shade, 0.62f * shade, 1.0f};
          break;
        }
        case 3: { // Green
          float shade =
              0.95f + 0.05f * std::cos(worldX * 0.15f + worldZ * 0.09f);
          vcolor = {0.28f * shade, 0.82f * shade, 0.26f * shade, 1.0f};
          break;
        }
        default:
          vcolor = {1.0f, 1.0f, 1.0f, 1.0f};
          break;
        }

        graphics::Vertex vert;
        vert.position = {worldX, h, worldZ};
        vert.normal = normal;
        vert.texCoord = {u, vLocal};
        vert.color = vcolor;

        vertices.push_back(vert);
      }
    }

    for (int z = 0; z < tileResZ - 1; ++z) {
      for (int x = 0; x < resX - 1; ++x) {
        uint32_t i0 = z * resX + x;
        uint32_t i1 = z * resX + (x + 1);
        uint32_t i2 = (z + 1) * resX + x;
        uint32_t i3 = (z + 1) * resX + (x + 1);

        indices.push_back(i0);
        indices.push_back(i1);
        indices.push_back(i2);
        indices.push_back(i2);
        indices.push_back(i1);
        indices.push_back(i3);
      }
    }

    // Explicit MeshHandle type
    resources::MeshHandle handle = ctx.resource.CreateDynamicMesh(
        "TerrainTile_" + std::to_string(tile.offsetY), vertices, indices);

    auto e = ctx.world.CreateEntity();
    Transform &transform = ctx.world.Add<Transform>(e);
    transform.position = {0.0f, 0.0f, 0.0f};

    MeshRenderer &meshRenderer = ctx.world.Add<MeshRenderer>(e);
    meshRenderer.mesh = handle;
    meshRenderer.shader =
        ctx.resource.LoadShader("Basic", L"Assets/shaders/BasicVS.hlsl",
                                L"Assets/shaders/BasicPS.hlsl");
    meshRenderer.color = terrainColor;
    meshRenderer.textureSRV = tile.srv;
    meshRenderer.hasTexture = true;

    m_entities.push_back(e);
  }
}

void WikiTerrainSystem::CreateWalls(core::GameContext &ctx, float width,
                                    float depth) {
  float wallHeight = 100.0f;
  float halfW = width * 0.5f;
  float halfD = depth * 0.5f;
  float wallThickness = 6.0f;

  auto CreateWall = [&](XMFLOAT3 pos, XMFLOAT3 scale, XMFLOAT4 rot,
                        XMFLOAT3 colliderSize) {
    auto e = ctx.world.CreateEntity();
    Transform &transform = ctx.world.Add<Transform>(e);
    transform.position = pos;
    transform.rotation = rot;
    transform.scale = scale;

    auto &mr = ctx.world.Add<MeshRenderer>(e);
    mr.mesh = ctx.resource.LoadMesh("builtin/plane");
    mr.shader = ctx.resource.LoadShader("Basic", L"Assets/shaders/BasicVS.hlsl",
                                        L"Assets/shaders/BasicPS.hlsl");
    mr.color = {0.0f, 0.8f, 1.0f, 0.2f};

    ctx.world.Add<Wall>(e);

    auto &rb = ctx.world.Add<RigidBody>(e);
    rb.isStatic = true;
    rb.restitution = 0.5f;

    auto &col = ctx.world.Add<Collider>(e);
    col.type = ColliderType::Box;
    col.size = colliderSize;

    m_entities.push_back(e);
  };

  {
    XMVECTOR rot = XMQuaternionRotationRollPitchYaw(0, 0, -XM_PIDIV2);
    XMFLOAT4 rotF;
    XMStoreFloat4(&rotF, rot);
    CreateWall({-halfW - wallThickness * 0.5f, wallHeight * 0.5f, 0.0f},
               {wallHeight, 1.0f, depth}, rotF, {1.0f, wallThickness, 1.0f});
  }

  {
    XMVECTOR rot = XMQuaternionRotationRollPitchYaw(0, 0, XM_PIDIV2);
    XMFLOAT4 rotF;
    XMStoreFloat4(&rotF, rot);
    CreateWall({halfW + wallThickness * 0.5f, wallHeight * 0.5f, 0.0f},
               {wallHeight, 1.0f, depth}, rotF, {1.0f, wallThickness, 1.0f});
  }

  {
    XMVECTOR rot = XMQuaternionRotationRollPitchYaw(-XM_PIDIV2, 0, 0);
    XMFLOAT4 rotF;
    XMStoreFloat4(&rotF, rot);
    CreateWall({0.0f, wallHeight * 0.5f, halfD + wallThickness * 0.5f},
               {width, 1.0f, wallHeight}, rotF, {1.0f, wallThickness, 1.0f});
  }

  {
    XMVECTOR rot = XMQuaternionRotationRollPitchYaw(XM_PIDIV2, 0, 0);
    XMFLOAT4 rotF;
    XMStoreFloat4(&rotF, rot);
    CreateWall({0.0f, wallHeight * 0.5f, -halfD - wallThickness * 0.5f},
               {width, 1.0f, wallHeight}, rotF, {1.0f, wallThickness, 1.0f});
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
    Transform &transform = ctx.world.Add<Transform>(e);
    transform.position = {worldX, height * 0.5f, worldZ};
    transform.scale = {worldW, height, worldD};

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
    col.size = {0.5f, 0.5f, 0.5f};

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
