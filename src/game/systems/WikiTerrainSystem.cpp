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
#include <random>    // for std::mt19937, std::uniform_*_distribution

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
  CreateDecorations(ctx, fieldWidth, fieldDepth, m_biome);
}

void WikiTerrainSystem::CreateFloor(core::GameContext &ctx,
                                    const graphics::WikiTextureResult &result,
                                    float width, float depth,
                                    const std::string &pageTitle) {
  // 1. 地形解像度・設定の決定
  int resX = 64;
  int resZ = static_cast<int>(depth);
  resZ = (std::max)(64, resZ);

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

  // configにバイオーム設定を反映
  config.biome = biome;

  XMFLOAT4 terrainColor = {1.0f, 1.0f, 1.0f, 1.0f};

  // 物理パラメータの統一 (環境によらず一定)
  config.friction = 0.5f;    // 標準的な芝の摩擦
  config.restitution = 0.3f; // 標準的な反発係数

  switch (biome) {
  case 0: // 草原 - 標準的なゴルフ場
    terrainColor = {0.4f, 0.8f, 0.4f, 1.0f};
    break;
  case 1: // 砂漠 - 大きな砂丘
    config.heightScale = 2.5f;
    terrainColor = {0.9f, 0.8f, 0.5f, 1.0f};
    break;
  case 2: // 氷原 - なだらかで広い
    config.heightScale = 1.0f;
    terrainColor = {0.8f, 0.9f, 1.0f, 1.0f};
    break;
  case 3: // 岩場 - 急峻で複雑
    config.heightScale = 3.0f;
    terrainColor = {0.6f, 0.5f, 0.5f, 1.0f};
    break;
  }

  // バイオームID保存
  m_biome = biome;

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
    ctx.world.Add<TerrainObject>(e);
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
        (std::max)(2, (int)(resZ * (tile.height / (float)result.height)));
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
        int gx = (std::clamp)(static_cast<int>(gridU * (resX - 1) + 0.5f), 0,
                              resX - 1);
        int gz = (std::clamp)(static_cast<int>(gridV * (resZ - 1) + 0.5f), 0,
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
        case 4: { // Ice
          float shade =
              0.95f + 0.05f * std::sin(worldX * 0.2f + worldZ * 0.15f);
          vcolor = {0.70f * shade, 0.88f * shade, 0.98f * shade, 0.85f};
          break;
        }
        case 5: { // Water
          float shade =
              0.90f + 0.10f * std::cos(worldX * 0.08f + worldZ * 0.06f);
          vcolor = {0.20f * shade, 0.45f * shade, 0.85f * shade, 0.7f};
          break;
        }
        case 6: { // Lava
          float shade =
              0.85f + 0.15f * std::sin(worldX * 0.15f + worldZ * 0.12f);
          vcolor = {0.95f * shade, 0.35f * shade, 0.12f * shade, 0.95f};
          break;
        }
        case 7: { // Stone
          float shade =
              0.88f + 0.12f * std::cos(worldX * 0.1f + worldZ * 0.08f);
          vcolor = {0.50f * shade, 0.48f * shade, 0.52f * shade, 1.0f};
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
    ctx.world.Add<TerrainObject>(e);
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
    ctx.world.Add<TerrainObject>(e);
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
    ctx.world.Add<TerrainObject>(e);
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

  float fx = u * (resX - 1);
  float fz = v * (resZ - 1);

  int ix = static_cast<int>(fx);
  int iz = static_cast<int>(fz);

  // Clamp indices for interpolation (safe up to res-2)
  ix = std::clamp(ix, 0, resX - 2);
  iz = std::clamp(iz, 0, resZ - 2);

  float dx = fx - ix;
  float dz = fz - iz;

  float h00 = m_terrainData->heightMap[iz * resX + ix];
  float h10 = m_terrainData->heightMap[iz * resX + (ix + 1)];
  float h01 = m_terrainData->heightMap[(iz + 1) * resX + ix];
  float h11 = m_terrainData->heightMap[(iz + 1) * resX + (ix + 1)];

  // Bilinear interpolation
  float h0 = h00 * (1.0f - dx) + h10 * dx;
  float h1 = h01 * (1.0f - dx) + h11 * dx;
  
  return h0 * (1.0f - dz) + h1 * dz;
}

void WikiTerrainSystem::CreateDecorations(core::GameContext &ctx,
                                          float fieldWidth, float fieldDepth,
                                          int biome) {
  // 装飾数設定（バイオームごとに調整）
  int numDecorations = 15;
  std::mt19937 rng(static_cast<unsigned>(biome * 12345 + 67890));
  std::uniform_real_distribution<float> distX(-fieldWidth * 0.4f,
                                              fieldWidth * 0.4f);
  std::uniform_real_distribution<float> distZ(-fieldDepth * 0.4f,
                                              fieldDepth * 0.4f);
  std::uniform_real_distribution<float> distScale(0.5f, 1.5f);
  std::uniform_real_distribution<float> distRot(0.0f, 6.28f);

  // バイオーム別装飾設定
  struct DecorationInfo {
    XMFLOAT4 color;
    XMFLOAT3 baseScale;
    std::string meshName;
  };

  std::vector<DecorationInfo> decorations;

  switch (biome) {
  case 0: // 草原 - 木（縦長の緑色キューブ）
    decorations.push_back(
        {{0.15f, 0.5f, 0.15f, 1.0f}, {0.3f, 2.0f, 0.3f}, "builtin/cube"});
    decorations.push_back(
        {{0.2f, 0.6f, 0.2f, 1.0f}, {0.8f, 0.5f, 0.8f}, "builtin/cube"});
    break;
  case 1: // 砂漠 - サボテン風（縦長の緑、岩）
    decorations.push_back(
        {{0.2f, 0.55f, 0.2f, 1.0f}, {0.15f, 1.2f, 0.15f}, "builtin/cube"});
    decorations.push_back(
        {{0.7f, 0.6f, 0.45f, 1.0f}, {0.8f, 0.4f, 0.6f}, "builtin/cube"});
    numDecorations = 12;
    break;
  case 2: // 氷原 - 氷塊（青白いキューブ）
    decorations.push_back(
        {{0.75f, 0.88f, 0.98f, 0.8f}, {0.6f, 0.8f, 0.5f}, "builtin/cube"});
    decorations.push_back(
        {{0.85f, 0.92f, 1.0f, 0.9f}, {0.4f, 1.2f, 0.3f}, "builtin/cube"});
    numDecorations = 10;
    break;
  case 3: // 岩場 - 岩石（灰色キューブ）、溶岩光（赤オレンジ）
    decorations.push_back(
        {{0.45f, 0.42f, 0.48f, 1.0f}, {1.0f, 0.6f, 0.8f}, "builtin/cube"});
    decorations.push_back(
        {{0.95f, 0.4f, 0.15f, 0.9f}, {0.3f, 0.2f, 0.3f}, "builtin/cube"});
    numDecorations = 18;
    break;
  default:
    return;
  }

  if (decorations.empty())
    return;

  std::uniform_int_distribution<size_t> distType(0, decorations.size() - 1);

  for (int i = 0; i < numDecorations; ++i) {
    float x = distX(rng);
    float z = distZ(rng);
    float terrainHeight = GetHeight(x, z);

    // 装飾タイプ選択
    const auto &deco = decorations[distType(rng)];

    auto e = ctx.world.CreateEntity();
    Transform &transform = ctx.world.Add<Transform>(e);
    float scale = distScale(rng);
    transform.position = {x, terrainHeight + deco.baseScale.y * scale * 0.5f,
                          z};
    transform.scale = {deco.baseScale.x * scale, deco.baseScale.y * scale,
                       deco.baseScale.z * scale};

    // Y軸回転
    float rot = distRot(rng);
    XMVECTOR quat = XMQuaternionRotationRollPitchYaw(0.0f, rot, 0.0f);
    XMFLOAT4 rotF;
    XMStoreFloat4(&rotF, quat);
    transform.rotation = rotF;

    auto &mr = ctx.world.Add<MeshRenderer>(e);
    mr.mesh = ctx.resource.LoadMesh(deco.meshName);
    mr.shader = ctx.resource.LoadShader("Basic", L"Assets/shaders/BasicVS.hlsl",
                                        L"Assets/shaders/BasicPS.hlsl");
    mr.color = deco.color;

    // 当たり判定なし（ユーザー指示）
    // RigidBody、Colliderは追加しない

    // RigidBody、Colliderは追加しない

    m_entities.push_back(e);
    ctx.world.Add<TerrainObject>(e);
  }

  LOG_INFO("WikiTerrain", "Created {} decorations for biome {}", numDecorations,
           biome);
}

} // namespace game::systems
