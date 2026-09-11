/**
 * @file WikiTerrainSystem.cpp
 * @brief Wiki地形生成システム実装
*/

#include "WikiTerrainSystem.h"
#include "HtmlTerrainRegions.h"
#include "../utils/GameplayPhysicsConstants.h"
#include "../../core/DisplaySettings.h"
#include "../../core/GameContext.h"
#include "../../core/Logger.h"
#include "../../core/StringUtils.h"
#include "../../ecs/World.h"
#include "../../resources/ResourceManager.h"
#include "../components/GrassRenderBatch.h"
#include "../components/MeshRenderer.h"
#include "../components/PhysicsComponents.h"
#include "../components/Transform.h"
#include "../components/WikiComponents.h"
#include "TerrainGenerator.h"
#include "TerrainLayoutRules.h"
#include "TerrainObstacleLayout.h"
#include "WikiClient.h"
#include "core/Profiler.h"
#include <DirectXMath.h>
#include "../../graphics/TangentGenerator.h"
#include <algorithm> // 最大値計算用
#include <cfloat>    // FLT_MAX
#include <cmath>     // std::lround
#include <random>    // 乱数生成用

namespace game::systems {

using namespace DirectX;
using namespace game::components;

/**
 * @brief 現在生成されている地形データを全削除します。
*/
void WikiTerrainSystem::Clear(core::GameContext &ctx) {
  // 非同期タスクが走っていれば待つ（デストラクタ前の安全確保）
  if (m_terrainFuture.valid()) {
    m_terrainFuture.get();
  }
  m_buildPhase   = BuildPhase::Idle;
  m_buildProgress = 0.0f;
  m_tileMeshCaches.clear();
  m_buildTileIndex = 0;
  m_buildTiles.clear();
  m_buildLinks.clear();
  ClearSurfaceGrass(ctx);
  m_grassFieldWidth = 0.0f;
  m_grassFieldDepth = 0.0f;
  m_grassViewChunkX = (std::numeric_limits<int>::max)();
  m_grassViewChunkZ = (std::numeric_limits<int>::max)();

  for (auto e : m_entities) {
    if (ctx.world.IsAlive(e)) {
      ctx.world.DestroyEntity(e);
    }
  }
  m_entities.clear();
  m_floorEntity = 0xFFFFFFFF;
}

/**
 * @brief フィールドを再構築します（同期版）。
*/
void WikiTerrainSystem::BuildField(core::GameContext &ctx,
                                   const std::string &pageTitle,
                                   const graphics::WikiTextureResult &result,
                                   float fieldWidth, float fieldDepth,
                                   const std::vector<std::string> &pageCategories) {
  Clear(ctx);

  LOG_INFO("WikiTerrain", "Building field {}x{} with {} tiles", fieldWidth,
           fieldDepth, (int)result.tiles.size());

  CreateFloor(ctx, result, fieldWidth, fieldDepth, pageTitle, pageCategories);
  CreateWalls(ctx, fieldWidth, fieldDepth);
  CreateDecorations(ctx, fieldWidth, fieldDepth, m_biome);
  CreateSurfaceGrass(ctx, fieldWidth, fieldDepth);
}

/**
 * @brief 床オブジェクトを生成します。
*/
void WikiTerrainSystem::CreateFloor(core::GameContext &ctx,
                                    const graphics::WikiTextureResult &result,
                                    float width, float depth,
                                    const std::string &pageTitle,
                                    const std::vector<std::string> &pageCategories) {
  // 非同期生成経路と同じセル間隔・上限を使用する。
  const TerrainResolution terrainResolution =
      TerrainLayoutRules::CalculateResolution(width, depth);
  const int resX = terrainResolution.x;
  const int resZ = terrainResolution.z;

  TerrainConfig config;
  config.htmlCourse = result.layoutWidth > 0;
  config.htmlRegions = HtmlRegions(result);
  config.worldWidth = width;
  config.worldDepth = depth;
  config.resolutionX = resX;
  config.resolutionZ = resZ;
  config.heightScale = 1.5f;

  // バイオーム決定
  int biome = TerrainLayoutRules::DetermineBiome(pageCategories, pageTitle);

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

  // 地形用テクスチャ配列のロード
  std::vector<std::string> albedoPaths = {
      "Assets/textures/terrain_materials/terrain_00_fairway_albedo.png",
      "Assets/textures/terrain_materials/terrain_01_rough_albedo.png",
      "Assets/textures/terrain_materials/terrain_02_bunker_albedo.png",
      "Assets/textures/terrain_materials/terrain_03_green_albedo.png",
      "Assets/textures/terrain_materials/terrain_04_ice_albedo.png",
      "Assets/textures/terrain_materials/terrain_05_water_albedo.png",
      "Assets/textures/terrain_materials/terrain_06_lava_albedo.png",
      "Assets/textures/terrain_materials/terrain_07_stone_albedo.png"};
  std::vector<std::string> normalPaths = {
      "Assets/textures/terrain_materials/terrain_00_fairway_normal_dx.png",
      "Assets/textures/terrain_materials/terrain_01_rough_normal_dx.png",
      "Assets/textures/terrain_materials/terrain_02_bunker_normal_dx.png",
      "Assets/textures/terrain_materials/terrain_03_green_normal_dx.png",
      "Assets/textures/terrain_materials/terrain_04_ice_normal_dx.png",
      "Assets/textures/terrain_materials/terrain_05_water_normal_dx.png",
      "Assets/textures/terrain_materials/terrain_06_lava_normal_dx.png",
      "Assets/textures/terrain_materials/terrain_07_stone_normal_dx.png"};

  auto terrainAlbedoSRV =
      ctx.resource.LoadTextureArraySRV("TerrainAlbedoArray", albedoPaths);
  auto terrainNormalSRV =
      ctx.resource.LoadTextureArraySRV("TerrainNormalArray", normalPaths);
  auto terrainShader = ctx.resource.LoadShader(
      "Terrain", L"Assets/shaders/TerrainVS.hlsl", L"Assets/shaders/TerrainPS.hlsl");

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

  // 地形データの生成
  if (m_tutorialMode &&
      (pageTitle == "チュートリアル" || pageTitle == "フェアウェイ")) {
    m_terrainData = std::make_shared<TerrainData>(
        TerrainGenerator::GenerateTutorialTerrain(config, holePositions));
  } else {
    m_terrainData = std::make_shared<TerrainData>(
        TerrainGenerator::GenerateTerrain(seedText, holePositions, config));
  }

  // 物理エンティティの作成
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

  // 各タイルのメッシュとエンティティ生成
  std::vector<graphics::WikiTextureResult::Tile> tilesToProcess;
  // グラフィックス名前空間で定義されたタイル型を使用してテクスチャ結果を取得

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
        float gridV = 0.5f - worldZ / depth;
        int gx = (std::clamp)(static_cast<int>(gridU * (resX - 1) + 0.5f), 0,
                              resX - 1);
        int gz = (std::clamp)(static_cast<int>(gridV * (resZ - 1) + 0.5f), 0,
                              resZ - 1);
        uint8_t mat = m_terrainData->materialMap[gz * resX + gx];

        // アルファにはマテリアルID(0-7)を入れる。精度誤差を防ぐため中央値を狙う。
        float matAlpha = (static_cast<float>(mat) + 0.5f) / 255.0f;
        XMFLOAT3 visualColor =
            TerrainLayoutRules::SampleVisualMaterialColor(
                *m_terrainData, gridU, gridV);
        XMFLOAT4 vcolor = {visualColor.x, visualColor.y, visualColor.z,
                           matAlpha};

        graphics::Vertex vert;
        vert.position = {worldX, h, worldZ};
        vert.normal = normal;
        vert.texCoord = {u * (width / 2.0f), vGlobal * (depth / 2.0f)}; // テクスチャリピート
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

    // 接線生成 (Normal Map用)
    graphics::ComputeTangents(vertices, indices);

    resources::MeshHandle handle = ctx.resource.CreateDynamicMesh(
        "TerrainTile_" + std::to_string(tile.offsetY), vertices, indices);

    auto e = ctx.world.CreateEntity();
    Transform &transform = ctx.world.Add<Transform>(e);
    transform.position = {0.0f, 0.0f, 0.0f};

    MeshRenderer &meshRenderer = ctx.world.Add<MeshRenderer>(e);
    meshRenderer.mesh = handle;
    meshRenderer.shader = terrainShader;
    meshRenderer.color = {1.0f, 1.0f, 1.0f, 1.0f};
    meshRenderer.textureSRV = terrainAlbedoSRV;
    meshRenderer.hasTexture = static_cast<bool>(terrainAlbedoSRV);
    meshRenderer.normalMapSRV = terrainNormalSRV;
    meshRenderer.hasNormalMap = static_cast<bool>(terrainNormalSRV);
    meshRenderer.isTransparent = false;
    meshRenderer.customFlags = {2.0f, 0.0f, 0.0f, 0.0f}; // x:UVスケール、y:未使用
    meshRenderer.minimapMode = MinimapRenderMode::VertexColor;

    // ミニマップ専用の間引き済みメッシュ（本描画用のフル解像度メッシュとは別物）
    float tileMaxHeight = 0.0f;
    {
      std::vector<uint32_t> minimapIndices;
      std::vector<graphics::Vertex> minimapVerts =
          TerrainLayoutRules::BuildMinimapTerrainGrid(
              vertices, resX, tileResZ, minimapIndices);
      resources::MeshHandle minimapHandle = ctx.resource.CreateDynamicMesh(
          "TerrainTileMinimap_" + std::to_string(tile.offsetY), minimapVerts,
          minimapIndices);
      meshRenderer.minimapMesh = minimapHandle;
      tileMaxHeight = TerrainLayoutRules::ComputeMaxVertexHeight(vertices);
    }

    m_entities.push_back(e);
    ctx.world.Add<TerrainObject>(e);

    // オーバーレイの生成
    std::vector<graphics::Vertex> overlayVertices = vertices;
    for (size_t i = 0; i < overlayVertices.size(); ++i) {
      overlayVertices[i].position.y +=
          game::physics::kTerrainVisualSurfaceOffset;
      overlayVertices[i].color = {1.0f, 1.0f, 1.0f, 1.0f};

      // オーバーレイのUVは元の 0..1 に戻す
      float u = (float)(i % resX) / (resX - 1);
      int zIdx = (int)(i / resX);
      float vLocal = (float)zIdx / (tileResZ - 1);
      overlayVertices[i].texCoord = {u, vLocal};
    }

    resources::MeshHandle overlayHandle = ctx.resource.CreateDynamicMesh(
        "TerrainTileOverlay_" + std::to_string(tile.offsetY), overlayVertices,
        indices);

    auto overlayEntity = ctx.world.CreateEntity();
    Transform &overlayTransform = ctx.world.Add<Transform>(overlayEntity);
    overlayTransform.position = {0.0f, 0.0f, 0.0f};

    MeshRenderer &overlayRenderer = ctx.world.Add<MeshRenderer>(overlayEntity);
    overlayRenderer.mesh = overlayHandle;
    overlayRenderer.shader =
        ctx.resource.LoadShader("Basic", L"Assets/shaders/BasicVS.hlsl",
                                L"Assets/shaders/BasicPS.hlsl");
    overlayRenderer.color = {1.0f, 1.0f, 1.0f, 1.0f}; // 乗算描画時はアルファ1.0
    overlayRenderer.textureSRV = tile.srv;
    overlayRenderer.hasTexture = true;
    overlayRenderer.isTransparent = true;
    overlayRenderer.blendMode = BlendMode::Multiply;
    overlayRenderer.customFlags = {1.0f, 0.0f, 1.0f, 0.0f}; // readabilityMode=0 (乗算で対応)
    overlayRenderer.minimapMode = MinimapRenderMode::Textured;

    // ミニマップ専用の平面2三角形メッシュ（本描画用の地形追従メッシュとは別物）
    {
      const float flatY = game::physics::ToVisualSurfaceHeight(tileMaxHeight);
      std::vector<uint32_t> minimapOvIndices;
      std::vector<graphics::Vertex> minimapOvVerts =
          TerrainLayoutRules::BuildMinimapOverlayQuad(
          width, zTop, zBottom, flatY, minimapOvIndices);
      resources::MeshHandle minimapOvHandle = ctx.resource.CreateDynamicMesh(
          "TerrainTileOverlayMinimap_" + std::to_string(tile.offsetY),
          minimapOvVerts, minimapOvIndices);
      overlayRenderer.minimapMesh = minimapOvHandle;
    }

    m_entities.push_back(overlayEntity);
    ctx.world.Add<TerrainObject>(overlayEntity);
  }
}

/**
 * @brief フィールド外周の壁オブジェクトを生成します。
*/
void WikiTerrainSystem::CreateWalls(core::GameContext &ctx, float width,
                                    float depth) {
  const auto walls = TerrainObstacleLayout::BuildWalls(width, depth);
  const auto createWall = [&](const WallLayout &layout) {
    auto e = ctx.world.CreateEntity();
    Transform &transform = ctx.world.Add<Transform>(e);
    transform.position = layout.position;
    transform.rotation = layout.rotation;
    transform.scale = layout.scale;

    auto &mr = ctx.world.Add<MeshRenderer>(e);
    mr.mesh = ctx.resource.LoadMesh("builtin/cube");
    mr.shader = ctx.resource.LoadShader("Wall", L"Assets/shaders/WallVS.hlsl",
                                        L"Assets/shaders/WallPS.hlsl");
    mr.color = {0.0f, 0.8f, 1.0f, 0.2f};
    mr.isTransparent = true;

    ctx.world.Add<Wall>(e);

    auto &rb = ctx.world.Add<RigidBody>(e);
    rb.isStatic = true;
    rb.restitution = 0.5f;

    auto &col = ctx.world.Add<Collider>(e);
    col.type = ColliderType::Box;
    col.size = layout.colliderSize;

    m_entities.push_back(e);
    ctx.world.Add<TerrainObject>(e);
  };
  for (const auto &wall : walls) {
    createWall(wall);
  }
}

/**
 * @brief 記事内の画像領域に対応した障害物オブジェクトを生成します。
*/
void WikiTerrainSystem::CreateImageObstacles(
    core::GameContext &ctx, const graphics::WikiTextureResult &result,
    float fieldWidth, float fieldDepth) {
  const auto obstacles = TerrainObstacleLayout::BuildImageObstacles(
      result, fieldWidth, fieldDepth);
  for (const auto &obstacle : obstacles) {
    auto e = ctx.world.CreateEntity();
    Transform &transform = ctx.world.Add<Transform>(e);
    transform.position = obstacle.position;
    transform.scale = obstacle.scale;

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
    col.size = {1.0f, 1.0f, 1.0f};

    m_entities.push_back(e);
    ctx.world.Add<TerrainObject>(e);
  }
}

/**
 * @brief 見出し情報に対応した段差オブジェクトを生成します。
*/
void WikiTerrainSystem::CreateHeadingSteps(
    core::GameContext &ctx, const graphics::WikiTextureResult &result,
    float fieldWidth, float fieldDepth) {}

} // namespace game::systems
