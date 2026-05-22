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
#include "../../graphics/TangentGenerator.h"
#include <algorithm> // 最大値計算用
#include <random>    // 乱数生成用

namespace game::systems {

using namespace DirectX;
using namespace game::components;

namespace {

constexpr float kTerrainOverlayHeightOffset = 0.02f;
constexpr float kTerrainOverlayAlpha = 0.34f;

int DetermineBiomeFromCategories(const std::vector<std::string> &categories,
                                 const std::string &pageTitle) {
  for (const auto &cat : categories) {
    if (cat.find("歴史") != std::string::npos ||
        cat.find("戦争") != std::string::npos ||
        cat.find("事件") != std::string::npos ||
        cat.find("政治") != std::string::npos ||
        cat.find("古代") != std::string::npos) {
      return 1;
    }
    if (cat.find("科学") != std::string::npos ||
        cat.find("技術") != std::string::npos ||
        cat.find("数学") != std::string::npos ||
        cat.find("物理") != std::string::npos ||
        cat.find("コンピュータ") != std::string::npos ||
        cat.find("宇宙") != std::string::npos) {
      return 2;
    }
    if (cat.find("地理") != std::string::npos ||
        cat.find("地形") != std::string::npos ||
        cat.find("生物") != std::string::npos ||
        cat.find("植物") != std::string::npos ||
        cat.find("動物") != std::string::npos ||
        cat.find("山") != std::string::npos) {
      return 3;
    }
  }

  std::hash<std::string> hasher;
  return static_cast<int>(hasher(pageTitle) % 4);
}

} // namespace

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

  for (auto e : m_entities) {
    if (ctx.world.IsAlive(e)) {
      ctx.world.DestroyEntity(e);
    }
  }
  m_entities.clear();
  m_floorEntity = 0xFFFFFFFF;
}

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

  // 地形解像度を事前計算
  m_buildResX = 64;
  m_buildResZ = static_cast<int>(fieldDepth);
  m_buildResZ = (std::max)(64, m_buildResZ);
  m_buildResZ = (std::min)(m_buildResZ, 256); // タスク1で設けた上限と一致

  // TerrainGenerator を別スレッドで開始（CPU演算のみ、ECS/GPU不使用）
  const std::string seedText  = pageTitle;
  const int   biome = DetermineBiomeFromCategories(pageCategories, pageTitle);
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

  // ラムダにコピーして非同期実行（thisへの参照を持たない）
  m_terrainFuture = std::async(
      std::launch::async,
      [seedText, holePositions, config]() mutable {
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
        DirectX::XMFLOAT4 vcolor;
        switch (mat) {
          case 0:  vcolor = {0.35f, 0.55f, 0.25f, matAlpha}; break;
          case 1:  vcolor = {0.25f, 0.45f, 0.20f, matAlpha}; break;
          case 2:  vcolor = {0.90f, 0.85f, 0.70f, matAlpha}; break;
          case 3:  vcolor = {0.40f, 0.75f, 0.30f, matAlpha}; break;
          case 4:  vcolor = {0.70f, 0.88f, 0.98f, matAlpha}; break;
          case 5:  vcolor = {0.20f, 0.45f, 0.85f, matAlpha}; break;
          case 6:  vcolor = {0.95f, 0.35f, 0.12f, matAlpha}; break;
          case 7:  vcolor = {0.50f, 0.48f, 0.52f, matAlpha}; break;
          default: vcolor = {1.0f,  1.0f,  1.0f,  matAlpha}; break;
        }

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
    mr.hasTexture   = true;
    mr.normalMapSRV = m_buildNormalSRV;
    mr.hasNormalMap = true;
    mr.isTransparent = false;
    mr.customFlags   = {2.0f, 0.0f, 0.0f, 0.0f};

    m_entities.push_back(e);
    ctx.world.Add<game::components::TerrainObject>(e);

    // Overlay用にキャッシュ保存
    TileMeshCache cache;
    cache.vertices = vertices;
    cache.indices  = indices;
    cache.resX     = resX;
    cache.tileResZ = tileResZ;
    cache.vStart   = vStart;
    cache.vEnd     = vEnd;
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
      ov[i].position.y += kTerrainOverlayHeightOffset;
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
}

/**
 * @brief 床オブジェクトを生成します。
 */
void WikiTerrainSystem::CreateFloor(core::GameContext &ctx,
                                    const graphics::WikiTextureResult &result,
                                    float width, float depth,
                                    const std::string &pageTitle,
                                    const std::vector<std::string> &pageCategories) {
  // 地形解像度と設定の決定
  int resX = 64;
  // フィールド拡張による頂点数爆発を防ぐためメッシュ解像度の上限を256に制限
  int resZ = static_cast<int>(depth);
  resZ = (std::max)(64, resZ);
  resZ = (std::min)(resZ, 256); // 上限: 64×256=16384頂点で固定

  TerrainConfig config;
  config.worldWidth = width;
  config.worldDepth = depth;
  config.resolutionX = resX;
  config.resolutionZ = resZ;
  config.heightScale = 1.5f;

  // バイオーム決定
  int biome = DetermineBiomeFromCategories(pageCategories, pageTitle);

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
  m_terrainData = std::make_shared<TerrainData>(
      TerrainGenerator::GenerateTerrain(seedText, holePositions, config));

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

        XMFLOAT4 vcolor;
        // アルファにはマテリアルID(0-7)を入れる。精度誤差を防ぐため中央値を狙う。
        float matAlpha = (static_cast<float>(mat) + 0.5f) / 255.0f;

        switch (mat) {
        case 0: vcolor = {0.35f, 0.55f, 0.25f, matAlpha}; break; // フェアウェイ
        case 1: vcolor = {0.25f, 0.45f, 0.20f, matAlpha}; break; // ラフ
        case 2: vcolor = {0.90f, 0.85f, 0.70f, matAlpha}; break; // バンカー
        case 3: vcolor = {0.40f, 0.75f, 0.30f, matAlpha}; break; // グリーン
        case 4: vcolor = {0.70f, 0.88f, 0.98f, matAlpha}; break; // 氷
        case 5: vcolor = {0.20f, 0.45f, 0.85f, matAlpha}; break; // 水
        case 6: vcolor = {0.95f, 0.35f, 0.12f, matAlpha}; break; // 溶岩
        case 7: vcolor = {0.50f, 0.48f, 0.52f, matAlpha}; break; // 石
        default: vcolor = {1.0f, 1.0f, 1.0f, matAlpha}; break;
        }

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
    meshRenderer.hasTexture = true;
    meshRenderer.normalMapSRV = terrainNormalSRV;
    meshRenderer.hasNormalMap = true;
    meshRenderer.isTransparent = false;
    meshRenderer.customFlags = {2.0f, 0.0f, 0.0f, 0.0f}; // x:UVスケール、y:未使用

    m_entities.push_back(e);
    ctx.world.Add<TerrainObject>(e);

    // オーバーレイの生成
    std::vector<graphics::Vertex> overlayVertices = vertices;
    for (size_t i = 0; i < overlayVertices.size(); ++i) {
      overlayVertices[i].position.y += kTerrainOverlayHeightOffset;
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

    m_entities.push_back(overlayEntity);
    ctx.world.Add<TerrainObject>(overlayEntity);
  }
}

/**
 * @brief フィールド外周の壁オブジェクトを生成します。
 */
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
    mr.isTransparent = true;

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

/**
 * @brief 記事内の画像領域に対応した障害物オブジェクトを生成します。
 */
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

/**
 * @brief 見出し情報に対応した段差オブジェクトを生成します。
 */
void WikiTerrainSystem::CreateHeadingSteps(
    core::GameContext &ctx, const graphics::WikiTextureResult &result,
    float fieldWidth, float fieldDepth) {}

/**
 * @brief 指定したワールド座標における地形の高さを取得します。
 */
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

  // 補間のためインデックスの範囲を制限
  ix = std::clamp(ix, 0, resX - 2);
  iz = std::clamp(iz, 0, resZ - 2);

  float dx = fx - ix;
  float dz = fz - iz;

  float h00 = m_terrainData->heightMap[iz * resX + ix];
  float h10 = m_terrainData->heightMap[iz * resX + (ix + 1)];
  float h01 = m_terrainData->heightMap[(iz + 1) * resX + ix];
  float h11 = m_terrainData->heightMap[(iz + 1) * resX + (ix + 1)];

  // バイリニア補間を実行
  float h0 = h00 * (1.0f - dx) + h10 * dx;
  float h1 = h01 * (1.0f - dx) + h11 * dx;
  
  return h0 * (1.0f - dz) + h1 * dz;
}

/**
 * @brief バイオームに応じた装飾オブジェクトを生成します。
 */
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
    if (deco.color.w < 1.0f) {
      mr.isTransparent = true;
    }

    // 当たり判定は追加しない

    m_entities.push_back(e);
    ctx.world.Add<TerrainObject>(e);
  }

  LOG_INFO("WikiTerrain", "Created {} decorations for biome {}", numDecorations,
           biome);
}

} // namespace game::systems
