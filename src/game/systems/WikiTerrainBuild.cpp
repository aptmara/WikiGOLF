/**
 * @file WikiTerrainBuild.cpp
 * @brief インクリメンタルな地形構築を実装します。
 */

#include "WikiTerrainSystem.h"
#include "../../core/GameContext.h"
#include "../../core/Logger.h"
#include "../../core/Profiler.h"
#include "../../ecs/World.h"
#include "../../resources/ResourceManager.h"
#include "../components/MeshRenderer.h"
#include "../components/PhysicsComponents.h"
#include "../components/Transform.h"
#include "../components/WikiComponents.h"
#include "TerrainLayoutRules.h"
#include "../utils/GameplayPhysicsConstants.h"
#include "../utils/ParRules.h"
#include "../../graphics/TangentGenerator.h"
#include <DirectXMath.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <future>
#include <memory>
#include <string>

namespace game::systems {

using namespace DirectX;
using namespace game::components;

/**
 * @brief インクリメンタルな地形構築を開始します。
 */
void WikiTerrainSystem::BeginBuildField(
    const std::string              &pageTitle,
    const graphics::WikiTextureResult &textureResult,
    float fieldWidth, float fieldDepth,
    const std::vector<std::string> &pageCategories)
{
  // 既存の非同期タスクが残っていれば回収
  if (m_terrainFuture.valid()) {
    m_terrainFuture.get();
  }

  m_buildPageTitle      = pageTitle;
  m_buildFieldWidth     = fieldWidth;
  m_buildFieldDepth     = fieldDepth;
  m_buildPageCategories = pageCategories;
  m_buildTexWidth       = textureResult.width;
  m_buildTexHeight      = textureResult.height;
  m_buildLinks          = textureResult.links;

  // タイルコピー（Tile は ComPtr を持つので浅コピーで参照カウントが増える）
  m_buildTiles = textureResult.tiles;
  if (m_buildTiles.empty() && textureResult.texture) {
    // 旧APIとの後方互換：単一テクスチャをタイルとして扱う
    graphics::WikiTextureResult::Tile t;
    t.texture  = textureResult.texture;
    t.srv      = textureResult.srv;
    t.width    = textureResult.width;
    t.height   = textureResult.height;
    t.offsetY  = 0.0f;
    m_buildTiles.push_back(t);
  }

  m_buildTileIndex = 0;
  m_tileMeshCaches.clear();
  m_buildProgress  = 0.0f;

  // フィールド寸法に対してセル間隔が大きくなりすぎない解像度を使用する。
  const TerrainResolution terrainResolution =
      TerrainLayoutRules::CalculateResolution(fieldWidth, fieldDepth);
  m_buildResX = terrainResolution.x;
  m_buildResZ = terrainResolution.z;

  // TerrainGenerator を別スレッドで開始（CPU演算のみ、ECS/GPU不使用）
  const std::string seedText  = pageTitle;
  const int biome =
      TerrainLayoutRules::DetermineBiome(pageCategories, pageTitle);
  m_biome = biome;

  TerrainConfig config;
  config.worldWidth   = fieldWidth;
  config.worldDepth   = fieldDepth;
  config.resolutionX  = m_buildResX;
  config.resolutionZ  = m_buildResZ;
  config.heightScale  = 1.5f;
  config.biome        = biome;
  config.friction     = 0.5f;
  config.restitution  = 0.3f;
  switch (biome) {
    case 1: config.heightScale = 2.5f; break;
    case 2: config.heightScale = 1.0f; break;
    case 3: config.heightScale = 3.0f; break;
    default: break;
  }

  // ホール位置を計算してTerrainGeneratorに渡す
  std::vector<DirectX::XMFLOAT2> holePositions;
  float texW = (float)m_buildTexWidth;
  float texH = (float)m_buildTexHeight;
  if (texW > 0 && texH > 0) {
    for (const auto &link : m_buildLinks) {
      float cx = link.x + link.width  * 0.5f;
      float cy = link.y + link.height * 0.5f;
      holePositions.push_back({
          (cx / texW - 0.5f) * fieldWidth,
          (0.5f - cy / texH) * fieldDepth
      });
    }
  }

  const bool useTutorialPreset = m_tutorialMode && pageTitle == "チュートリアル";

  // ラムダにコピーして非同期実行（thisへの参照を持たない）
  m_terrainFuture = std::async(
      std::launch::async,
      [seedText, holePositions, config, useTutorialPreset]() mutable {
          if (useTutorialPreset) {
              return TerrainGenerator::GenerateTutorialTerrain(config, holePositions);
          }
          return TerrainGenerator::GenerateTerrain(seedText, holePositions, config);
      });

  m_buildPhase = BuildPhase::TerrainGenAsync;
  LOG_INFO("WikiTerrain", "BeginBuildField: async terrain gen started ({}x{})",
           m_buildResX, m_buildResZ);
}

/**
 * @brief 地形構築を1ステップ進めます。
 * @return 完了したら true
 */
bool WikiTerrainSystem::StepBuildField(core::GameContext &ctx)
{
  PROFILE_SCOPE("StepBuildField");
  switch (m_buildPhase) {

  // 非同期待ち
  case BuildPhase::TerrainGenAsync: {
    auto status = m_terrainFuture.wait_for(std::chrono::milliseconds(0));
    if (status != std::future_status::ready) {
      return false; // まだ完成していない
    }
    m_terrainData = std::make_shared<TerrainData>(m_terrainFuture.get());
    LOG_INFO("WikiTerrain", "StepBuildField: terrain data ready");
    m_buildPhase   = BuildPhase::CreatePhysics;
    m_buildProgress = 0.10f;
    return false;
  }

  // 物理エンティティの作成
  case BuildPhase::CreatePhysics: {
    using namespace game::components;

    auto e = ctx.world.CreateEntity();
    auto &transform = ctx.world.Add<Transform>(e);
    transform.position = {0.0f, 0.0f, 0.0f};

    auto &rb = ctx.world.Add<RigidBody>(e);
    rb.isStatic      = true;
    rb.restitution   = m_terrainData->config.restitution;
    rb.rollingFriction = m_terrainData->config.friction;

    auto &tc = ctx.world.Add<TerrainCollider>(e);
    tc.data = m_terrainData;

    m_floorEntity = e;
    m_entities.push_back(e);
    ctx.world.Add<TerrainObject>(e);

    // シェーダー・テクスチャをキャッシュ（後のステップでも使う）
    const std::vector<std::string> albedoPaths = {
        "Assets/textures/terrain_materials/terrain_00_fairway_albedo.png",
        "Assets/textures/terrain_materials/terrain_01_rough_albedo.png",
        "Assets/textures/terrain_materials/terrain_02_bunker_albedo.png",
        "Assets/textures/terrain_materials/terrain_03_green_albedo.png",
        "Assets/textures/terrain_materials/terrain_04_ice_albedo.png",
        "Assets/textures/terrain_materials/terrain_05_water_albedo.png",
        "Assets/textures/terrain_materials/terrain_06_lava_albedo.png",
        "Assets/textures/terrain_materials/terrain_07_stone_albedo.png"};
    const std::vector<std::string> normalPaths = {
        "Assets/textures/terrain_materials/terrain_00_fairway_normal_dx.png",
        "Assets/textures/terrain_materials/terrain_01_rough_normal_dx.png",
        "Assets/textures/terrain_materials/terrain_02_bunker_normal_dx.png",
        "Assets/textures/terrain_materials/terrain_03_green_normal_dx.png",
        "Assets/textures/terrain_materials/terrain_04_ice_normal_dx.png",
        "Assets/textures/terrain_materials/terrain_05_water_normal_dx.png",
        "Assets/textures/terrain_materials/terrain_06_lava_normal_dx.png",
        "Assets/textures/terrain_materials/terrain_07_stone_normal_dx.png"};

    m_buildAlbedoSRV   = ctx.resource.LoadTextureArraySRV("TerrainAlbedoArray", albedoPaths);
    m_buildNormalSRV   = ctx.resource.LoadTextureArraySRV("TerrainNormalArray",  normalPaths);
    m_buildTerrainShader = ctx.resource.LoadShader(
        "Terrain", L"Assets/shaders/TerrainVS.hlsl", L"Assets/shaders/TerrainPS.hlsl");
    m_buildBasicShader   = ctx.resource.LoadShader(
        "Basic", L"Assets/shaders/BasicVS.hlsl", L"Assets/shaders/BasicPS.hlsl");

    m_buildTileIndex = 0;
    m_tileMeshCaches.clear();
    m_buildPhase   = BuildPhase::CreateTileMesh;
    m_buildProgress = 0.15f;
    LOG_INFO("WikiTerrain", "StepBuildField: physics entity created");
    return false;
  }

  // ビジュアルメッシュの生成
  case BuildPhase::CreateTileMesh: {
    if (m_buildTileIndex >= m_buildTiles.size()) {
      // 全タイル処理完了 → オーバーレイへ
      m_buildTileIndex = 0;
      m_buildPhase   = BuildPhase::CreateTileOverlay;
      m_buildProgress = 0.60f;
      return false;
    }

    const auto &tile = m_buildTiles[m_buildTileIndex];
    if (!tile.srv) {
      ++m_buildTileIndex;
      return false;
    }

    const int   resX      = m_buildResX;
    const int   totalResZ = m_buildResZ;
    const float fieldW    = m_buildFieldWidth;
    const float fieldD    = m_buildFieldDepth;
    const float totalH    = (float)m_buildTexHeight;

    float vStart = tile.offsetY / totalH;
    float vEnd   = (tile.offsetY + (float)tile.height) / totalH;

    int tileResZ = (std::max)(2, (int)(totalResZ * (tile.height / totalH)));

    std::vector<graphics::Vertex> vertices;
    std::vector<uint32_t>         indices;
    vertices.reserve(resX * tileResZ);
    indices.reserve((resX - 1) * (tileResZ - 1) * 6);

    for (int z = 0; z < tileResZ; ++z) {
      float vLocal  = (float)z / (tileResZ - 1);
      float vGlobal = vStart + vLocal * (vEnd - vStart);
      float worldZ  = fieldD * (0.5f - vGlobal);

      for (int x = 0; x < resX; ++x) {
        float u      = (float)x / (resX - 1);
        float worldX = fieldW * (u - 0.5f);
        float h      = GetHeight(worldX, worldZ);

        float hL = GetHeight(worldX - 0.1f, worldZ);
        float hR = GetHeight(worldX + 0.1f, worldZ);
        float hD = GetHeight(worldX, worldZ - 0.1f);
        float hU = GetHeight(worldX, worldZ + 0.1f);
        DirectX::XMVECTOR n = DirectX::XMVectorSet(hL - hR, 0.2f, hD - hU, 0.0f);
        n = DirectX::XMVector3Normalize(n);
        DirectX::XMFLOAT3 normal;
        DirectX::XMStoreFloat3(&normal, n);

        float gridU = worldX / fieldW + 0.5f;
        float gridV = 0.5f - worldZ / fieldD;
        int gx = (std::clamp)(static_cast<int>(gridU * (resX - 1) + 0.5f), 0, resX - 1);
        int gz = (std::clamp)(static_cast<int>(gridV * (totalResZ - 1) + 0.5f), 0, totalResZ - 1);
        uint8_t mat = m_terrainData->materialMap[gz * resX + gx];

        float matAlpha = (static_cast<float>(mat) + 0.5f) / 255.0f;
        XMFLOAT3 visualColor =
            TerrainLayoutRules::SampleVisualMaterialColor(
                *m_terrainData, gridU, gridV);
        DirectX::XMFLOAT4 vcolor = {visualColor.x, visualColor.y,
                                    visualColor.z, matAlpha};

        graphics::Vertex vert;
        vert.position = {worldX, h, worldZ};
        vert.normal   = normal;
        vert.texCoord = {u * (fieldW / 2.0f), vGlobal * (fieldD / 2.0f)};
        vert.color    = vcolor;
        vertices.push_back(vert);
      }
    }

    for (int z = 0; z < tileResZ - 1; ++z) {
      for (int x = 0; x < resX - 1; ++x) {
        uint32_t i0 = z * resX + x;
        uint32_t i1 = z * resX + (x + 1);
        uint32_t i2 = (z + 1) * resX + x;
        uint32_t i3 = (z + 1) * resX + (x + 1);
        indices.insert(indices.end(), {i0, i1, i2, i2, i1, i3});
      }
    }

    graphics::ComputeTangents(vertices, indices);

    auto handle = ctx.resource.CreateDynamicMesh(
        "TerrainTile_" + std::to_string(tile.offsetY), vertices, indices);

    auto e = ctx.world.CreateEntity();
    auto &transform = ctx.world.Add<game::components::Transform>(e);
    transform.position = {0.0f, 0.0f, 0.0f};

    auto &mr     = ctx.world.Add<game::components::MeshRenderer>(e);
    mr.mesh      = handle;
    mr.shader    = m_buildTerrainShader;
    mr.color     = {1.0f, 1.0f, 1.0f, 1.0f};
    mr.textureSRV   = m_buildAlbedoSRV;
    mr.hasTexture   = static_cast<bool>(m_buildAlbedoSRV);
    mr.normalMapSRV = m_buildNormalSRV;
    mr.hasNormalMap = static_cast<bool>(m_buildNormalSRV);
    mr.isTransparent = false;
    mr.customFlags   = {2.0f, 0.0f, 0.0f, 0.0f};
    mr.minimapMode   = game::components::MinimapRenderMode::VertexColor;

    // ミニマップ専用の間引き済みメッシュ（本描画用のフル解像度メッシュとは別物）
    {
      std::vector<uint32_t> minimapIndices;
      std::vector<graphics::Vertex> minimapVerts =
          TerrainLayoutRules::BuildMinimapTerrainGrid(
              vertices, resX, tileResZ, minimapIndices);
      auto minimapHandle = ctx.resource.CreateDynamicMesh(
          "TerrainTileMinimap_" + std::to_string(tile.offsetY), minimapVerts,
          minimapIndices);
      mr.minimapMesh = minimapHandle;
    }

    m_entities.push_back(e);
    ctx.world.Add<game::components::TerrainObject>(e);

    // Overlay用にキャッシュ保存
    TileMeshCache cache;
    cache.vertices  = vertices;
    cache.indices   = indices;
    cache.resX      = resX;
    cache.tileResZ  = tileResZ;
    cache.vStart    = vStart;
    cache.vEnd      = vEnd;
    cache.maxHeight = TerrainLayoutRules::ComputeMaxVertexHeight(vertices);
    m_tileMeshCaches.push_back(std::move(cache));

    float tileProgress = (float)(m_buildTileIndex + 1) / (float)(std::max<size_t>(1, m_buildTiles.size()));
    m_buildProgress = 0.15f + 0.45f * tileProgress;
    ++m_buildTileIndex;
    return false;
  }

  // オーバーレイの生成
  case BuildPhase::CreateTileOverlay: {
    if (m_buildTileIndex >= m_buildTiles.size()) {
      m_buildPhase   = BuildPhase::CreateWalls;
      m_buildProgress = 0.90f;
      return false;
    }
    if (m_buildTileIndex >= m_tileMeshCaches.size()) {
      ++m_buildTileIndex;
      return false;
    }

    const auto &tile  = m_buildTiles[m_buildTileIndex];
    const auto &cache = m_tileMeshCaches[m_buildTileIndex];

    if (!tile.srv) {
      ++m_buildTileIndex;
      return false;
    }

    const int resX = cache.resX;
    int tileResZ   = cache.tileResZ;
    std::vector<graphics::Vertex> ov = cache.vertices;
    for (size_t i = 0; i < ov.size(); ++i) {
      ov[i].position.y += game::physics::kTerrainVisualSurfaceOffset;
      ov[i].color = {1.0f, 1.0f, 1.0f, 1.0f};
      float u      = (float)(i % resX) / (resX - 1);
      int   zIdx   = (int)(i / resX);
      float vLocal = (float)zIdx / (tileResZ - 1);
      ov[i].texCoord = {u, vLocal};
    }

    auto ovHandle = ctx.resource.CreateDynamicMesh(
        "TerrainTileOverlay_" + std::to_string(tile.offsetY), ov, cache.indices);

    auto ovE = ctx.world.CreateEntity();
    auto &ovT = ctx.world.Add<game::components::Transform>(ovE);
    ovT.position = {0.0f, 0.0f, 0.0f};

    auto &ovMr     = ctx.world.Add<game::components::MeshRenderer>(ovE);
    ovMr.mesh      = ovHandle;
    ovMr.shader    = m_buildBasicShader;
    ovMr.color     = {1.0f, 1.0f, 1.0f, 1.0f};
    ovMr.textureSRV  = tile.srv;
    ovMr.hasTexture  = true;
    ovMr.isTransparent = true;
    ovMr.blendMode   = game::components::BlendMode::Multiply;
    ovMr.customFlags = {1.0f, 0.0f, 1.0f, 0.0f};
    ovMr.minimapMode = game::components::MinimapRenderMode::Textured;

    // ミニマップ専用の平面2三角形メッシュ（本描画用の地形追従メッシュとは別物）
    {
      const float zTop = m_buildFieldDepth * (0.5f - cache.vStart);
      const float zBottom = m_buildFieldDepth * (0.5f - cache.vEnd);
      const float flatY = game::physics::ToVisualSurfaceHeight(cache.maxHeight);
      std::vector<uint32_t> minimapOvIndices;
      std::vector<graphics::Vertex> minimapOvVerts =
          TerrainLayoutRules::BuildMinimapOverlayQuad(
          m_buildFieldWidth, zTop, zBottom, flatY, minimapOvIndices);
      auto minimapOvHandle = ctx.resource.CreateDynamicMesh(
          "TerrainTileOverlayMinimap_" + std::to_string(tile.offsetY),
          minimapOvVerts, minimapOvIndices);
      ovMr.minimapMesh = minimapOvHandle;
    }

    m_entities.push_back(ovE);
    ctx.world.Add<game::components::TerrainObject>(ovE);

    float ovProgress = (float)(m_buildTileIndex + 1) / (float)(std::max<size_t>(1, m_buildTiles.size()));
    m_buildProgress = 0.60f + 0.30f * ovProgress;
    ++m_buildTileIndex;
    return false;
  }

  // 壁の生成
  case BuildPhase::CreateWalls: {
    CreateWalls(ctx, m_buildFieldWidth, m_buildFieldDepth);
    m_buildPhase   = BuildPhase::CreateDecorations;
    m_buildProgress = 0.94f;
    return false;
  }

  // 装飾の生成
  case BuildPhase::CreateDecorations: {
    CreateDecorations(ctx, m_buildFieldWidth, m_buildFieldDepth, m_biome);
    CreateSurfaceGrass(ctx, m_buildFieldWidth, m_buildFieldDepth);
    m_buildPhase   = BuildPhase::Done;
    m_buildProgress = 1.0f;
    m_tileMeshCaches.clear(); // メモリ解放
    LOG_INFO("WikiTerrain", "StepBuildField: complete (tiles={}, entities={})",
             m_buildTiles.size(), m_entities.size());
    return true;
  }

  case BuildPhase::Done:
    return true;

  default:
    return true;
  }
}


} // namespace game::systems

