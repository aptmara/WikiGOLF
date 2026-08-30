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
#include "../components/GrassRenderBatch.h"
#include "../components/MeshRenderer.h"
#include "../components/PhysicsComponents.h"
#include "../components/Transform.h"
#include "../components/WikiComponents.h"
#include "TerrainGenerator.h"
#include "WikiClient.h"
#include "core/Profiler.h"
#include <DirectXMath.h>
#include "../../graphics/TangentGenerator.h"
#include <algorithm> // 最大値計算用
#include <cmath>     // std::lround
#include <random>    // 乱数生成用

namespace game::systems {

using namespace DirectX;
using namespace game::components;

namespace {

constexpr float kTerrainOverlayHeightOffset = 0.02f;
constexpr float kTerrainOverlayAlpha = 0.34f;
constexpr float kTerrainVertexSpacing = 0.8f;

struct TerrainResolution {
  int x;
  int z;
};

TerrainResolution CalculateTerrainResolution(float width, float depth) {
  TerrainResolution resolution;
  resolution.x = std::clamp(
      static_cast<int>(std::ceil(width / kTerrainVertexSpacing)) + 1, 96,
      160);
  resolution.z = std::clamp(
      static_cast<int>(std::ceil(depth / kTerrainVertexSpacing)) + 1, 96,
      320);
  return resolution;
}

float LerpValue(float a, float b, float t) { return a + (b - a) * t; }

XMFLOAT3 SampleVisualMaterialColor(const TerrainData &data, float u, float v) {
  const int resX = data.config.resolutionX;
  const int resZ = data.config.resolutionZ;
  float fx = std::clamp(u, 0.0f, 1.0f) * static_cast<float>(resX - 1);
  float fz = std::clamp(v, 0.0f, 1.0f) * static_cast<float>(resZ - 1);
  int x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, resX - 1);
  int z0 = std::clamp(static_cast<int>(std::floor(fz)), 0, resZ - 1);
  int x1 = std::min(x0 + 1, resX - 1);
  int z1 = std::min(z0 + 1, resZ - 1);
  float tx = fx - static_cast<float>(x0);
  float tz = fz - static_cast<float>(z0);

  const XMFLOAT3 &c00 = data.visualMaterialColors[z0 * resX + x0];
  const XMFLOAT3 &c10 = data.visualMaterialColors[z0 * resX + x1];
  const XMFLOAT3 &c01 = data.visualMaterialColors[z1 * resX + x0];
  const XMFLOAT3 &c11 = data.visualMaterialColors[z1 * resX + x1];
  XMFLOAT3 result;
  result.x = LerpValue(LerpValue(c00.x, c10.x, tx),
                       LerpValue(c01.x, c11.x, tx), tz);
  result.y = LerpValue(LerpValue(c00.y, c10.y, tx),
                       LerpValue(c01.y, c11.y, tx), tz);
  result.z = LerpValue(LerpValue(c00.z, c10.z, tx),
                       LerpValue(c01.z, c11.z, tx), tz);
  return result;
}

// ミニマップ(256x256)向けに積極的に間引いた地形タイルの分割数。
// フル解像度(最大64x256)に対し十分小さく、俯瞰視点では見分けがつかない。
constexpr int kMinimapTileGridRes = 8;

/// @brief フル解像度のタイル頂点グリッドから、ミニマップ専用の間引きグリッドを生成する
/// @details 世界座標のタイル被覆範囲・頂点カラーは維持しつつ、ミニマップシェーダーが
///          使用しない法線/接線の計算は省略する（既定値を詰めるだけ）。
std::vector<graphics::Vertex> BuildMinimapTerrainGrid(
    const std::vector<graphics::Vertex> &src, int srcResX, int srcResZ,
    std::vector<uint32_t> &outIndices) {
  const int decX = (std::max)(2, (std::min)(srcResX, kMinimapTileGridRes));
  const int decZ = (std::max)(2, (std::min)(srcResZ, kMinimapTileGridRes));

  std::vector<graphics::Vertex> out;
  out.reserve(static_cast<size_t>(decX) * decZ);
  for (int z = 0; z < decZ; ++z) {
    int srcZ = static_cast<int>(std::lround(
        (float)z * (float)(srcResZ - 1) / (float)(decZ - 1)));
    srcZ = std::clamp(srcZ, 0, srcResZ - 1);
    for (int x = 0; x < decX; ++x) {
      int srcX = static_cast<int>(std::lround(
          (float)x * (float)(srcResX - 1) / (float)(decX - 1)));
      srcX = std::clamp(srcX, 0, srcResX - 1);

      graphics::Vertex v = src[srcZ * srcResX + srcX];
      v.normal = {0.0f, 1.0f, 0.0f};
      v.tangent = {1.0f, 0.0f, 0.0f};
      v.bitangent = {0.0f, 0.0f, 1.0f};
      out.push_back(v);
    }
  }

  outIndices.clear();
  outIndices.reserve(static_cast<size_t>(decX - 1) * (decZ - 1) * 6);
  for (int z = 0; z < decZ - 1; ++z) {
    for (int x = 0; x < decX - 1; ++x) {
      uint32_t i0 = static_cast<uint32_t>(z * decX + x);
      uint32_t i1 = static_cast<uint32_t>(z * decX + (x + 1));
      uint32_t i2 = static_cast<uint32_t>((z + 1) * decX + x);
      uint32_t i3 = static_cast<uint32_t>((z + 1) * decX + (x + 1));
      outIndices.insert(outIndices.end(), {i0, i1, i2, i2, i1, i3});
    }
  }
  return out;
}

/// @brief 記事オーバーレイタイル用の、最小限の平面2三角形ミニマップメッシュを生成する
/// @details タイルのワールドX/Z範囲を覆う単一平面。UVは0..1、Yは指定の固定高さ。
///          本描画用メッシュ（地形追従・高解像度）とは別物として扱う。
std::vector<graphics::Vertex> BuildMinimapOverlayQuad(float fieldWidth,
                                                       float zTop,
                                                       float zBottom,
                                                       float flatY,
                                                       std::vector<uint32_t> &outIndices) {
  const float halfW = fieldWidth * 0.5f;
  auto makeVert = [](float x, float y, float z, float u, float v) {
    graphics::Vertex vert;
    vert.position = {x, y, z};
    vert.normal = {0.0f, 1.0f, 0.0f};
    vert.texCoord = {u, v};
    vert.color = {1.0f, 1.0f, 1.0f, 1.0f};
    vert.tangent = {1.0f, 0.0f, 0.0f};
    vert.bitangent = {0.0f, 0.0f, 1.0f};
    return vert;
  };

  std::vector<graphics::Vertex> out;
  out.reserve(4);
  out.push_back(makeVert(-halfW, flatY, zTop, 0.0f, 0.0f));    // 左上
  out.push_back(makeVert(halfW, flatY, zTop, 1.0f, 0.0f));     // 右上
  out.push_back(makeVert(-halfW, flatY, zBottom, 0.0f, 1.0f)); // 左下
  out.push_back(makeVert(halfW, flatY, zBottom, 1.0f, 1.0f));  // 右下

  outIndices = {0, 1, 2, 2, 1, 3};
  return out;
}

float ComputeMaxVertexHeight(const std::vector<graphics::Vertex> &vertices) {
  float maxY = 0.0f;
  bool first = true;
  for (const auto &v : vertices) {
    if (first || v.position.y > maxY) {
      maxY = v.position.y;
      first = false;
    }
  }
  return maxY;
}

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
  m_grassPatches.clear();

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

  // フィールド寸法に対してセル間隔が大きくなりすぎない解像度を使用する。
  const TerrainResolution terrainResolution =
      CalculateTerrainResolution(fieldWidth, fieldDepth);
  m_buildResX = terrainResolution.x;
  m_buildResZ = terrainResolution.z;

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
            SampleVisualMaterialColor(*m_terrainData, gridU, gridV);
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
          BuildMinimapTerrainGrid(vertices, resX, tileResZ, minimapIndices);
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
    cache.maxHeight = ComputeMaxVertexHeight(vertices);
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
    ovMr.minimapMode = game::components::MinimapRenderMode::Textured;

    // ミニマップ専用の平面2三角形メッシュ（本描画用の地形追従メッシュとは別物）
    {
      const float zTop = m_buildFieldDepth * (0.5f - cache.vStart);
      const float zBottom = m_buildFieldDepth * (0.5f - cache.vEnd);
      const float flatY = cache.maxHeight + kTerrainOverlayHeightOffset;
      std::vector<uint32_t> minimapOvIndices;
      std::vector<graphics::Vertex> minimapOvVerts = BuildMinimapOverlayQuad(
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
      CalculateTerrainResolution(width, depth);
  const int resX = terrainResolution.x;
  const int resZ = terrainResolution.z;

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
  if (m_tutorialMode && pageTitle == "チュートリアル") {
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
            SampleVisualMaterialColor(*m_terrainData, gridU, gridV);
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
          BuildMinimapTerrainGrid(vertices, resX, tileResZ, minimapIndices);
      resources::MeshHandle minimapHandle = ctx.resource.CreateDynamicMesh(
          "TerrainTileMinimap_" + std::to_string(tile.offsetY), minimapVerts,
          minimapIndices);
      meshRenderer.minimapMesh = minimapHandle;
      tileMaxHeight = ComputeMaxVertexHeight(vertices);
    }

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
    overlayRenderer.minimapMode = MinimapRenderMode::Textured;

    // ミニマップ専用の平面2三角形メッシュ（本描画用の地形追従メッシュとは別物）
    {
      const float flatY = tileMaxHeight + kTerrainOverlayHeightOffset;
      std::vector<uint32_t> minimapOvIndices;
      std::vector<graphics::Vertex> minimapOvVerts = BuildMinimapOverlayQuad(
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

void WikiTerrainSystem::CreateSurfaceGrass(core::GameContext &ctx,
                                           float fieldWidth,
                                           float fieldDepth) {
  if (!m_terrainData || fieldWidth <= 0.0f || fieldDepth <= 0.0f) {
    return;
  }

  const int resX = m_terrainData->config.resolutionX;
  const int resZ = m_terrainData->config.resolutionZ;
  if (resX < 2 || resZ < 2 || m_terrainData->materialMap.empty()) {
    return;
  }

  // 1パッチ内を高密度の芝床として生成し、パッチ自体は適度に広げて重ねる。
  // 小さな草株を大量に並べる方式より、連続面としてのラフを保ちやすい。
  constexpr float desiredSpacing = 0.72f;
  constexpr float maximumPatchCount = 28000.0f;
  const float fieldArea = fieldWidth * fieldDepth;
  const float spacing =
      std::max(desiredSpacing, std::sqrt(fieldArea / maximumPatchCount));
  const int columns =
      std::max(1, static_cast<int>(std::ceil(fieldWidth * 0.96f / spacing)));
  const int rows =
      std::max(1, static_cast<int>(std::ceil(fieldDepth * 0.96f / spacing)));
  std::mt19937 rng(static_cast<unsigned>(
      resX * 73856093u ^ resZ * 19349663u ^ (m_biome + 1) * 83492791u));
  std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
  std::uniform_real_distribution<float> distYaw(0.0f, XM_2PI);
  std::uniform_real_distribution<float> distJitter(-spacing * 0.30f,
                                                    spacing * 0.30f);

  auto materialAt = [&](float x, float z) {
    const float u = x / fieldWidth + 0.5f;
    const float v = 0.5f - z / fieldDepth;
    const int gx = std::clamp(
        static_cast<int>(u * static_cast<float>(resX - 1) + 0.5f), 0,
        resX - 1);
    const int gz = std::clamp(
        static_cast<int>(v * static_cast<float>(resZ - 1) + 0.5f), 0,
        resZ - 1);
    return static_cast<TerrainMaterial>(
        m_terrainData->materialMap[gz * resX + gx]);
  };

  // 近傍の高さから地形の法線を求め、株の「上」をその法線に合わせる
  // 回転を返す。坂の途中で根本が斜面にめり込んだり浮いたりしないよう、
  // ラフと境界のセミラフで共通利用する。
  auto computeSlopeAlignQuat = [&](float x, float z) {
    constexpr float normalSampleOffset = 0.15f;
    const float heightLeft = GetHeight(x - normalSampleOffset, z);
    const float heightRight = GetHeight(x + normalSampleOffset, z);
    const float heightDown = GetHeight(x, z - normalSampleOffset);
    const float heightUp = GetHeight(x, z + normalSampleOffset);
    XMVECTOR slopeNormal = XMVector3Normalize(XMVectorSet(
        (heightLeft - heightRight) / (2.0f * normalSampleOffset), 1.0f,
        (heightDown - heightUp) / (2.0f * normalSampleOffset), 0.0f));

    XMVECTOR worldUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    XMVECTOR alignAxis = XMVector3Cross(worldUp, slopeNormal);
    const float alignAxisLenSq = XMVectorGetX(XMVector3LengthSq(alignAxis));
    if (alignAxisLenSq <= 1e-8f) {
      return XMQuaternionIdentity();
    }
    const float cosAngle =
        std::clamp(XMVectorGetX(XMVector3Dot(worldUp, slopeNormal)), -1.0f,
                  1.0f);
    return XMQuaternionRotationAxis(XMVector3Normalize(alignAxis),
                                    std::acos(cosAngle));
  };

  // 同じ形のパッチが並んで見えないよう、配置ごとに複数のメッシュ
  // バリアント（別seedで生成した葉の配置）から選ぶ。
  constexpr int kGrassVariantCount = 4;
  resources::MeshHandle grassMeshVariants[kGrassVariantCount] = {
      ctx.resource.LoadMesh("builtin/grass_patch_0"),
      ctx.resource.LoadMesh("builtin/grass_patch_1"),
      ctx.resource.LoadMesh("builtin/grass_patch_2"),
      ctx.resource.LoadMesh("builtin/grass_patch_3"),
  };
  auto grassShader = ctx.resource.LoadShader(
      "Grass", L"Assets/shaders/GrassVS.hlsl",
      L"Assets/shaders/GrassPS.hlsl");

  constexpr float kGrassChunkSize = 12.0f;
  enum class GrassSurfaceGroup {
    Rough,
    SemiRough,
    Fairway,
    Green,
  };
  struct GrassBatchKey {
    int chunkX = 0;
    int chunkZ = 0;
    int variantIndex = 0;
    GrassSurfaceGroup surface = GrassSurfaceGroup::Rough;

    bool operator==(const GrassBatchKey &other) const {
      return chunkX == other.chunkX && chunkZ == other.chunkZ &&
             variantIndex == other.variantIndex && surface == other.surface;
    }
  };
  struct GrassBatchKeyHash {
    size_t operator()(const GrassBatchKey &key) const {
      size_t hash = static_cast<size_t>(key.chunkX);
      hash = hash * 31u + static_cast<size_t>(key.chunkZ);
      hash = hash * 31u + static_cast<size_t>(key.variantIndex);
      hash = hash * 31u + static_cast<size_t>(key.surface);
      return hash;
    }
  };

  std::unordered_map<GrassBatchKey, ecs::Entity, GrassBatchKeyHash>
      grassBatches;
  int batchCreated = 0;

  auto appendGrassInstance =
      [&](float x, float y, float z, float horizontalScale,
          float heightScale, const XMFLOAT4 &rotation, const XMFLOAT4 &color,
          resources::MeshHandle mesh, resources::MeshHandle lodMesh,
          int variantIndex, GrassSurfaceGroup surface, float lodSwitchDistance,
          float maxDrawDistance, bool twoSided) {
        GrassBatchKey key;
        key.chunkX = static_cast<int>(std::floor(x / kGrassChunkSize));
        key.chunkZ = static_cast<int>(std::floor(z / kGrassChunkSize));
        key.variantIndex = variantIndex;
        key.surface = surface;

        ecs::Entity batchEntity = 0xFFFFFFFF;
        const auto batchIt = grassBatches.find(key);
        if (batchIt == grassBatches.end()) {
          batchEntity = ctx.world.CreateEntity();
          auto &newBatch = ctx.world.Add<GrassRenderBatch>(batchEntity);
          newBatch.mesh = mesh;
          newBatch.lodMesh = lodMesh;
          newBatch.shader = grassShader;
          newBatch.lodSwitchDistance = lodSwitchDistance;
          newBatch.maxDrawDistance = maxDrawDistance;
          newBatch.maxThreeDOverheadRatio =
              surface == GrassSurfaceGroup::Fairway ? 0.75f : 1.1f;
          newBatch.twoSided = twoSided;
          grassBatches.emplace(key, batchEntity);
          m_entities.push_back(batchEntity);
          ctx.world.Add<TerrainObject>(batchEntity);
          ++batchCreated;
        } else {
          batchEntity = batchIt->second;
        }

        auto *batch = ctx.world.Get<GrassRenderBatch>(batchEntity);
        if (!batch) {
          return;
        }

        Transform transform;
        transform.position = {x, y, z};
        transform.scale = {horizontalScale, heightScale, horizontalScale};
        transform.rotation = rotation;

        GrassRenderInstance instance;
        XMStoreFloat4x4(&instance.world, transform.GetWorldMatrix());
        instance.color = color;
        instance.position = transform.position;

        const size_t instanceIndex = batch->instances.size();
        batch->instances.push_back(instance);

        const float horizontalExtent = horizontalScale;
        const float verticalExtent = std::max(0.08f, heightScale * 1.35f);
        batch->boundsMin.x =
            std::min(batch->boundsMin.x, x - horizontalExtent);
        batch->boundsMin.y =
            std::min(batch->boundsMin.y, y - verticalExtent);
        batch->boundsMin.z =
            std::min(batch->boundsMin.z, z - horizontalExtent);
        batch->boundsMax.x =
            std::max(batch->boundsMax.x, x + horizontalExtent);
        batch->boundsMax.y =
            std::max(batch->boundsMax.y, y + verticalExtent);
        batch->boundsMax.z =
            std::max(batch->boundsMax.z, z + horizontalExtent);

        GrassPatch grass;
        grass.entity = batchEntity;
        grass.instanceIndex = instanceIndex;
        grass.position = transform.position;
        grass.halfExtent = horizontalScale * 0.72f;
        m_grassPatches.push_back(grass);
      };

  int created = 0;
  int roughCount = 0;
  int semiRoughCount = 0;
  const float startX = -0.5f * static_cast<float>(columns - 1) * spacing;
  const float startZ = -0.5f * static_cast<float>(rows - 1) * spacing;
  const float horizontalScale = spacing * 1.08f;

  for (int row = 0; row < rows; ++row) {
    // 一行ごとに半間隔ずらす千鳥配置で、格子模様の集合体に見えるのを防ぐ。
    const float rowStagger = (row % 2 == 0) ? 0.0f : spacing * 0.5f;
    for (int column = 0; column < columns; ++column) {
      const float x = startX + static_cast<float>(column) * spacing +
                      rowStagger + distJitter(rng);
      const float z = startZ + static_cast<float>(row) * spacing +
                      distJitter(rng);
      const TerrainMaterial material = materialAt(x, z);
      const float edgeSample = std::max(0.8f, spacing * 1.4f);
      const bool bordersRough =
          material == TerrainMaterial::Fairway &&
          (materialAt(x - edgeSample, z) == TerrainMaterial::Rough ||
           materialAt(x + edgeSample, z) == TerrainMaterial::Rough ||
           materialAt(x, z - edgeSample) == TerrainMaterial::Rough ||
           materialAt(x, z + edgeSample) == TerrainMaterial::Rough);
      const bool isRough = material == TerrainMaterial::Rough;
      const bool isSemiRough = bordersRough && dist01(rng) < 0.62f;
      if (!isRough && !isSemiRough) {
        continue;
      }

      const float variation = dist01(rng);
      const float heightScale = isSemiRough ? (0.085f + variation * 0.025f)
                                            : (0.145f + variation * 0.045f);
      const XMFLOAT4 color =
          isSemiRough
              ? XMFLOAT4{0.46f + variation * 0.025f,
                         0.66f + variation * 0.035f,
                         0.27f + variation * 0.020f, 0.58f}
              : XMFLOAT4{0.38f + variation * 0.035f,
                         0.58f + variation * 0.045f,
                         0.21f + variation * 0.025f, 0.88f};
      if (isRough) {
        ++roughCount;
      } else {
        ++semiRoughCount;
      }

      const float yaw = distYaw(rng);
      const float terrainHeight = GetHeight(x, z);
      const XMVECTOR alignQuat = computeSlopeAlignQuat(x, z);
      const XMVECTOR yawQuat = XMQuaternionRotationRollPitchYaw(0.0f, yaw, 0.0f);

      XMFLOAT4 rotation;
      XMStoreFloat4(&rotation, XMQuaternionMultiply(yawQuat, alignQuat));

      // 格子座標から決定的にバリアントを選び、隣接パッチが同じ葉配置に
      // ならないようにする（乱数列は消費せず配置を安定させる）。
      const unsigned variantHash = static_cast<unsigned>(row) * 374761393u ^
                                   static_cast<unsigned>(column) * 668265263u;
      const int variantIndex =
          static_cast<int>((variantHash >> 13) % kGrassVariantCount);

      const GrassSurfaceGroup surface =
          isSemiRough ? GrassSurfaceGroup::SemiRough
                      : GrassSurfaceGroup::Rough;
      appendGrassInstance(
          x, terrainHeight + 0.003f, z, horizontalScale, heightScale,
          rotation, color, grassMeshVariants[variantIndex],
          resources::MeshHandle::Invalid(), variantIndex, surface, 0.0f,
          isSemiRough ? 46.0f : 60.0f, false);
      ++created;
    }
  }

  // Fairway / Greenは同寸法のセルを隙間なく並べ、近距離の3D葉から
  // 中遠距離の地表シェーダーへディザーフェードで連続させる。
  constexpr float desiredTurfSpacing = 1.05f;
  constexpr float maximumTurfPatchCount = 14000.0f;
  const float turfSpacing =
      std::max(desiredTurfSpacing,
               std::sqrt(fieldArea / maximumTurfPatchCount));
  const int turfColumns = std::max(
      1, static_cast<int>(std::ceil(fieldWidth * 0.96f / turfSpacing)));
  const int turfRows = std::max(
      1, static_cast<int>(std::ceil(fieldDepth * 0.96f / turfSpacing)));
  constexpr int kTurfVariantCount = 4;
  resources::MeshHandle denseTurfMeshVariants[kTurfVariantCount] = {
      ctx.resource.LoadMesh("builtin/turf_patch_dense_0"),
      ctx.resource.LoadMesh("builtin/turf_patch_dense_1"),
      ctx.resource.LoadMesh("builtin/turf_patch_dense_2"),
      ctx.resource.LoadMesh("builtin/turf_patch_dense_3"),
  };
  resources::MeshHandle denseFairwayMeshVariants[kTurfVariantCount] = {
      ctx.resource.LoadMesh("builtin/fairway_turf_patch_dense_0"),
      ctx.resource.LoadMesh("builtin/fairway_turf_patch_dense_1"),
      ctx.resource.LoadMesh("builtin/fairway_turf_patch_dense_2"),
      ctx.resource.LoadMesh("builtin/fairway_turf_patch_dense_3"),
  };

  const float turfHorizontalScale = turfSpacing;
  const float turfStartX =
      -0.5f * static_cast<float>(turfColumns - 1) * turfSpacing;
  const float turfStartZ =
      -0.5f * static_cast<float>(turfRows - 1) * turfSpacing;
  int turfCreated = 0;
  int fairwayCount = 0;
  int greenCount = 0;

  for (int row = 0; row < turfRows; ++row) {
    for (int column = 0; column < turfColumns; ++column) {
      const float x =
          turfStartX + static_cast<float>(column) * turfSpacing;
      const float z = turfStartZ + static_cast<float>(row) * turfSpacing;
      const TerrainMaterial material = materialAt(x, z);
      if (material != TerrainMaterial::Fairway &&
          material != TerrainMaterial::Green) {
        continue;
      }

      const bool isGreen = material == TerrainMaterial::Green;
      const float heightScale = isGreen ? 0.012f : 0.0375f;
      const XMFLOAT4 color =
          isGreen
              ? XMFLOAT4{0.53f, 0.71f, 0.32f, 0.06f}
              : XMFLOAT4{0.46f, 0.66f, 0.27f, 0.14f};

      const float bandWidth = isGreen ? 1.8f : 3.2f;
      const float bandCoordinate = isGreen ? z : x;
      const int bandIndex =
          static_cast<int>(std::floor(bandCoordinate / bandWidth));
      const float reverseYaw = (bandIndex % 2 == 0) ? 0.0f : XM_PI;
      const float baseYaw = isGreen ? XM_PIDIV2 : 0.0f;
      const float yaw = baseYaw + reverseYaw;

      const float terrainHeight = GetHeight(x, z);
      const XMVECTOR alignQuat = computeSlopeAlignQuat(x, z);
      const XMVECTOR yawQuat =
          XMQuaternionRotationRollPitchYaw(0.0f, yaw, 0.0f);

      XMFLOAT4 rotation;
      XMStoreFloat4(&rotation, XMQuaternionMultiply(yawQuat, alignQuat));

      const unsigned variantHash = static_cast<unsigned>(row) * 2246822519u ^
                                   static_cast<unsigned>(column) * 3266489917u;
      const int variantIndex =
          static_cast<int>((variantHash >> 15) % kTurfVariantCount);

      const GrassSurfaceGroup surface =
          isGreen ? GrassSurfaceGroup::Green : GrassSurfaceGroup::Fairway;
      const resources::MeshHandle turfMesh =
          isGreen ? denseTurfMeshVariants[variantIndex]
                  : denseFairwayMeshVariants[variantIndex];
      appendGrassInstance(
          x, terrainHeight + 0.0015f, z, turfHorizontalScale, heightScale,
          rotation, color, turfMesh, resources::MeshHandle::Invalid(),
          variantIndex, surface, 0.0f, isGreen ? 19.0f : 22.0f, true);
      ++turfCreated;
      if (isGreen) {
        ++greenCount;
      } else {
        ++fairwayCount;
      }
    }
  }

  LOG_INFO("WikiTerrain",
           "Created {} managed rough and transition grass patches "
           "(rough={}, semiRough={}, spacing={:.2f})",
           created, roughCount, semiRoughCount, spacing);
  LOG_INFO("WikiTerrain",
           "Created {} seamless mown turf cells "
           "(fairway={}, green={}, cellSize={:.2f})",
           turfCreated, fairwayCount, greenCount, turfSpacing);
  LOG_INFO("WikiTerrain",
           "Packed {} grass patches into {} spatial GPU instance batches "
           "(chunkSize={:.1f})",
           created + turfCreated, batchCreated, kGrassChunkSize);
}

void WikiTerrainSystem::UpdateSurfaceResponse(core::GameContext &ctx,
                                              ecs::Entity ballEntity,
                                              float dt) {
  if (m_grassPatches.empty() || !ctx.world.IsAlive(ballEntity) ||
      !std::isfinite(dt) || dt <= 0.0f) {
    return;
  }

  const auto *ballTransform = ctx.world.Get<Transform>(ballEntity);
  const auto *ballBody = ctx.world.Get<RigidBody>(ballEntity);
  const auto *state = ctx.world.GetGlobal<GolfGameState>();
  if (!ballTransform) {
    return;
  }

  float velocityX = ballBody ? ballBody->velocity.x : 0.0f;
  float velocityZ = ballBody ? ballBody->velocity.z : 0.0f;
  const float horizontalSpeed =
      std::sqrt(velocityX * velocityX + velocityZ * velocityZ);
  if (horizontalSpeed > 0.05f) {
    velocityX /= horizontalSpeed;
    velocityZ /= horizontalSpeed;
  }

  const bool touchesGrass =
      state && state->isBallGrounded &&
      (state->currentMaterial == TerrainMaterial::Fairway ||
       state->currentMaterial == TerrainMaterial::Rough ||
       state->currentMaterial == TerrainMaterial::Green);

  for (GrassPatch &grass : m_grassPatches) {
    const float dx = grass.position.x - ballTransform->position.x;
    const float dz = grass.position.z - ballTransform->position.z;
    const float distanceSq = dx * dx + dz * dz;
    const float patchReach = grass.halfExtent + 1.1f;
    const bool isNearBall =
        touchesGrass && distanceSq < patchReach * patchReach &&
        std::abs(grass.position.y - ballTransform->position.y) < 0.7f;
    if (!isNearBall && grass.response <= 0.0f) {
      continue;
    }

    auto *batch = ctx.world.Get<GrassRenderBatch>(grass.entity);
    if (!batch || grass.instanceIndex >= batch->instances.size()) {
      continue;
    }

    float targetResponse = 0.0f;

    if (isNearBall) {
      grass.interactionPoint = {ballTransform->position.x,
                                ballTransform->position.z};
      const float speedResponse =
          std::clamp(0.42f + horizontalSpeed * 0.045f, 0.42f, 1.0f);
      targetResponse = speedResponse;

      if (horizontalSpeed > 0.05f) {
        grass.interactionYaw = std::atan2(velocityZ, velocityX);
      } else if (distanceSq > 0.0001f) {
        grass.interactionYaw = std::atan2(dz, dx);
      }
    }

    const float responseSpeed =
        targetResponse > grass.response ? 18.0f : 3.8f;
    const float blend = 1.0f - std::exp(-responseSpeed * dt);
    grass.response += (targetResponse - grass.response) * blend;
    if (grass.response < 0.002f) {
      grass.response = 0.0f;
    }

    batch->instances[grass.instanceIndex].flags = {
        grass.interactionPoint.x, grass.interactionPoint.y,
        grass.interactionYaw, grass.response};
  }
}

/**
 * @brief バイオームに応じた装飾オブジェクトを生成します。
 */
void WikiTerrainSystem::CreateDecorations(core::GameContext &ctx,
                                          float fieldWidth, float fieldDepth,
                                          int biome) {
  if (!m_terrainData) {
    return;
  }

  int targetClusters = 15;
  if (biome == 1) {
    targetClusters = 12;
  } else if (biome == 2) {
    targetClusters = 10;
  } else if (biome == 3) {
    targetClusters = 16;
  }

  std::mt19937 rng(static_cast<unsigned>(biome * 12345 + 67890));
  std::uniform_real_distribution<float> distX(-fieldWidth * 0.4f,
                                               fieldWidth * 0.4f);
  std::uniform_real_distribution<float> distZ(-fieldDepth * 0.4f,
                                               fieldDepth * 0.4f);
  std::uniform_real_distribution<float> distScale(0.75f, 1.30f);
  std::uniform_real_distribution<float> distRot(0.0f, 6.28f);
  std::uniform_real_distribution<float> distOffset(-1.0f, 1.0f);

  const int resX = m_terrainData->config.resolutionX;
  const int resZ = m_terrainData->config.resolutionZ;
  const float sampleOffset =
      std::max(fieldWidth / static_cast<float>(resX - 1),
               fieldDepth / static_cast<float>(resZ - 1));

  auto materialAt = [&](float x, float z) -> uint8_t {
    float u = x / fieldWidth + 0.5f;
    float v = 0.5f - z / fieldDepth;
    if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) {
      return 255;
    }
    int gx = std::clamp(
        static_cast<int>(u * static_cast<float>(resX - 1) + 0.5f), 0,
        resX - 1);
    int gz = std::clamp(
        static_cast<int>(v * static_cast<float>(resZ - 1) + 0.5f), 0,
        resZ - 1);
    return m_terrainData->materialMap[gz * resX + gx];
  };
  auto materialAllowed = [&](uint8_t material) {
    switch (biome) {
    case 0:
      return material == 1;
    case 1:
      return material == 2 || material == 7;
    case 2:
      return material == 4 || material == 7;
    case 3:
      return material == 1 || material == 7;
    default:
      return false;
    }
  };

  auto slopeAt = [&](float x, float z) {
    float hL = GetHeight(x - sampleOffset, z);
    float hR = GetHeight(x + sampleOffset, z);
    float hD = GetHeight(x, z - sampleOffset);
    float hU = GetHeight(x, z + sampleOffset);
    float dx = (hR - hL) / (sampleOffset * 2.0f);
    float dz = (hU - hD) / (sampleOffset * 2.0f);
    return std::sqrt(dx * dx + dz * dz);
  };

  auto basicShader = ctx.resource.LoadShader(
      "Basic", L"Assets/shaders/BasicVS.hlsl", L"Assets/shaders/BasicPS.hlsl");
  auto cylinderMesh = ctx.resource.LoadMesh("builtin/cylinder_smooth");
  auto sphereMesh = ctx.resource.LoadMesh("builtin/sphere_smooth");
  auto rockMesh = ctx.resource.LoadMesh("builtin/rock");

  auto createPiece = [&](resources::MeshHandle mesh, const XMFLOAT4 &color,
                         const XMFLOAT3 &position, const XMFLOAT3 &scale,
                         float pitch, float yaw, float roll) {
    auto e = ctx.world.CreateEntity();
    Transform &transform = ctx.world.Add<Transform>(e);
    transform.position = position;
    transform.scale = scale;
    XMVECTOR quat = XMQuaternionRotationRollPitchYaw(pitch, yaw, roll);
    XMFLOAT4 rotF;
    XMStoreFloat4(&rotF, quat);
    transform.rotation = rotF;

    auto &mr = ctx.world.Add<MeshRenderer>(e);
    mr.mesh = mesh;
    mr.shader = basicShader;
    mr.color = color;
    m_entities.push_back(e);
    ctx.world.Add<TerrainObject>(e);
  };

  std::vector<XMFLOAT2> clusterPositions;
  int createdClusters = 0;
  const int maxAttempts = targetClusters * 30;
  for (int attempt = 0;
       attempt < maxAttempts && createdClusters < targetClusters; ++attempt) {
    float x = distX(rng);
    float z = distZ(rng);
    if (!materialAllowed(materialAt(x, z))) {
      continue;
    }

    float slopeLimit = biome == 0 ? 0.42f : 0.85f;
    if (slopeAt(x, z) > slopeLimit) {
      continue;
    }

    bool overlaps = false;
    for (const auto &position : clusterPositions) {
      float dx = x - position.x;
      float dz = z - position.y;
      if (dx * dx + dz * dz < 6.25f) {
        overlaps = true;
        break;
      }
    }
    if (overlaps) {
      continue;
    }

    clusterPositions.push_back({x, z});
    ++createdClusters;
    float terrainHeight = GetHeight(x, z);
    float scale = distScale(rng);
    float yaw = distRot(rng);

    if (biome == 0) {
      float trunkHeight = 1.8f * scale;
      createPiece(cylinderMesh, {0.30f, 0.20f, 0.11f, 1.0f},
                  {x, terrainHeight + trunkHeight * 0.5f, z},
                  {0.28f * scale, trunkHeight, 0.28f * scale}, 0.0f, yaw,
                  0.0f);
      const XMFLOAT4 leafColors[] = {
          {0.16f, 0.36f, 0.12f, 1.0f}, {0.22f, 0.44f, 0.15f, 1.0f},
          {0.12f, 0.31f, 0.10f, 1.0f}};
      for (int part = 0; part < 3; ++part) {
        float angle = yaw + static_cast<float>(part) * 2.094f;
        float offset = 0.34f * scale;
        createPiece(
            sphereMesh, leafColors[part],
            {x + std::cos(angle) * offset,
             terrainHeight + trunkHeight + (0.18f + part * 0.10f) * scale,
             z + std::sin(angle) * offset},
            {(1.05f - part * 0.08f) * scale, 0.85f * scale,
             (1.00f - part * 0.05f) * scale},
            0.0f, angle, 0.0f);
      }
    } else if (biome == 1) {
      int rockCount = 2 + static_cast<int>(distScale(rng) > 1.0f);
      for (int part = 0; part < rockCount; ++part) {
        float ox = distOffset(rng) * 0.55f * scale;
        float oz = distOffset(rng) * 0.55f * scale;
        float pieceScale = scale * (0.55f + 0.22f * part);
        float pieceHeight = GetHeight(x + ox, z + oz);
        createPiece(rockMesh,
                    part == 0 ? XMFLOAT4{0.58f, 0.48f, 0.34f, 1.0f}
                              : XMFLOAT4{0.68f, 0.58f, 0.42f, 1.0f},
                    {x + ox, pieceHeight + pieceScale * 0.34f, z + oz},
                    {pieceScale * 1.15f, pieceScale * 0.75f, pieceScale},
                    distOffset(rng) * 0.18f, yaw + part * 0.7f,
                    distOffset(rng) * 0.16f);
      }
    } else if (biome == 2) {
      for (int part = 0; part < 3; ++part) {
        float angle = yaw + static_cast<float>(part) * 2.094f;
        float distance = part == 0 ? 0.0f : 0.38f * scale;
        float pieceHeight = GetHeight(x + std::cos(angle) * distance,
                                      z + std::sin(angle) * distance);
        float pieceScale = scale * (1.0f - part * 0.16f);
        createPiece(
            rockMesh, {0.70f + part * 0.05f, 0.86f + part * 0.03f, 0.96f, 1.0f},
            {x + std::cos(angle) * distance,
             pieceHeight + pieceScale * 0.60f,
             z + std::sin(angle) * distance},
            {pieceScale * 0.55f, pieceScale * 1.45f,
             pieceScale * 0.50f},
            distOffset(rng) * 0.22f, angle, distOffset(rng) * 0.18f);
      }
    } else if (biome == 3) {
      for (int part = 0; part < 3; ++part) {
        float angle = yaw + static_cast<float>(part) * 2.094f;
        float distance = 0.42f * part * scale;
        float px = x + std::cos(angle) * distance;
        float pz = z + std::sin(angle) * distance;
        float pieceScale = scale * (1.0f - part * 0.14f);
        createPiece(
            rockMesh,
            part == 0 ? XMFLOAT4{0.34f, 0.32f, 0.31f, 1.0f}
                      : XMFLOAT4{0.43f, 0.40f, 0.38f, 1.0f},
            {px, GetHeight(px, pz) + pieceScale * 0.34f, pz},
            {pieceScale * 1.20f, pieceScale * 0.82f, pieceScale},
            distOffset(rng) * 0.20f, angle, distOffset(rng) * 0.20f);
      }
    }
  }

  LOG_INFO("WikiTerrain", "Created {} decoration clusters for biome {}",
           createdClusters, biome);
}

} // namespace game::systems
