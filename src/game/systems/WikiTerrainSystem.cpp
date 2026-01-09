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
#include "../components/WikiComponents.h" // 追加
#include "../components/Transform.h"
#include "TerrainGenerator.h" // 追加
#include "WikiClient.h" // 追加

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
                                   const std::string& pageTitle,
                                   const graphics::WikiTextureResult &result,
                                   float fieldWidth, float fieldDepth) {
  Clear(ctx);

  LOG_INFO("WikiTerrain", "Building field {}x{} with {} images, {} headings",
           fieldWidth, fieldDepth, result.images.size(),
           result.headings.size());

  CreateFloor(ctx, result, fieldWidth, fieldDepth, pageTitle);
  CreateWalls(ctx, fieldWidth, fieldDepth);
  // 画像や見出しの障害物は、地形の起伏に埋もれる可能性があるため、
  // 地形の高さを考慮して配置する必要があるが、一旦Y座標調整のみで対応
  CreateImageObstacles(ctx, result, fieldWidth, fieldDepth);
  // CreateHeadingSteps は地形そのもので表現されるべきなので廃止検討
  // だが、今回は残しておく（地形の上に浮くかもしれないが）
}

void WikiTerrainSystem::CreateFloor(core::GameContext &ctx,
                                    const graphics::WikiTextureResult &result,
                                    float width, float depth, const std::string& pageTitle) {
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

  for (const auto& cat : categories) {
      // 歴史・社会 -> 砂漠 (Heavy)
      if (cat.find("歴史") != std::string::npos || cat.find("戦争") != std::string::npos || 
          cat.find("事件") != std::string::npos || cat.find("政治") != std::string::npos ||
          cat.find("古代") != std::string::npos) {
          biome = 1; 
          found = true; break;
      }
      // 科学・技術 -> 氷原 (Slippery)
      if (cat.find("科学") != std::string::npos || cat.find("技術") != std::string::npos ||
          cat.find("数学") != std::string::npos || cat.find("物理") != std::string::npos ||
          cat.find("コンピュータ") != std::string::npos || cat.find("宇宙") != std::string::npos) {
          biome = 2; 
          found = true; break;
      }
      // 自然・地理 -> 岩場 (Bouncy)
      if (cat.find("地理") != std::string::npos || cat.find("地形") != std::string::npos ||
          cat.find("生物") != std::string::npos || cat.find("植物") != std::string::npos ||
          cat.find("動物") != std::string::npos || cat.find("山") != std::string::npos) {
          biome = 3; 
          found = true; break;
      }
  }
  
  if (!found) {
      // フォールバック: ハッシュ
      std::hash<std::string> hasher;
      size_t h = hasher(pageTitle);
      biome = h % 4;
  }

  XMFLOAT4 terrainColor = {1.0f, 1.0f, 1.0f, 1.0f};

  switch(biome) {
  case 0: // 草原 (Normal)
      config.friction = 0.5f; 
      config.restitution = 0.3f;
      terrainColor = {0.4f, 0.8f, 0.4f, 1.0f}; // 緑
      break;
  case 1: // 砂漠 (Heavy)
      config.friction = 2.5f; 
      config.restitution = 0.1f;
      config.heightScale = 2.5f; // 少し高いくらいに
      terrainColor = {0.9f, 0.8f, 0.5f, 1.0f}; // 砂色
      break;
  case 2: // 氷原 (Slippery)
      config.friction = 0.05f; 
      config.restitution = 0.6f;
      config.heightScale = 1.0f; // かなり平坦
      terrainColor = {0.8f, 0.9f, 1.0f, 1.0f}; // 白っぽい水色
      break;
  case 3: // 岩場 (Bouncy/Rough)
      config.friction = 0.6f; 
      config.restitution = 0.8f;
      config.heightScale = 3.0f; // そこそこ険しい
      terrainColor = {0.6f, 0.5f, 0.5f, 1.0f}; // 赤褐色
      break;
  }

  // 記事テキストとリンク情報（WikiTextureResultにはリンク位置はあるがテキストがない）
  // テクスチャ生成時に使ったテキストが必要だが、ここには渡されていない。
  // 簡易的に WikiTextureResult の text (wstring) を string に変換して使うか、
  // あるいはランダムシードとして pageTitle を使う。
  // ここでは pageTitle をシードにする。
  std::string seedText = pageTitle;
  
  // リンク位置情報はWikiTextureResultにあるが、文字列とのペアではない。
  // TerrainGeneratorは文字列をハッシュに使っているが、座標さえわかればいいはず。
  // TerrainGeneratorのインターフェースを少し柔軟にする必要があるが、
  // ここではダミーのリンクデータを作って渡す（座標ベースの生成に変えるのが理想だがPhase 1なので）
  std::vector<std::pair<std::string, std::wstring>> dummyLinks;
  for(const auto& link : result.links) {
      dummyLinks.push_back({link.targetPage, core::ToWString(link.targetPage)});
  }

  // 地形データ生成
  // TerrainDataは大きくコピーコストが高いので、shared_ptrで管理してコンポーネントと共有する
  auto terrainData = std::make_shared<TerrainData>(
      TerrainGenerator::GenerateTerrain(seedText, dummyLinks, config));

  // メッシュリソース作成
  std::string meshName = "GeneratedTerrain_" + seedText;
  auto meshHandle = ctx.resource.CreateDynamicMesh(meshName, terrainData->vertices, terrainData->indices);

  // エンティティ作成
  auto e = ctx.world.CreateEntity();
  auto &t = ctx.world.Add<Transform>(e);
  t.position = {0.0f, 0.0f, 0.0f}; // メッシュ自体がオフセットを持っているので0
  t.scale = {1.0f, 1.0f, 1.0f};

  auto &mr = ctx.world.Add<MeshRenderer>(e);
  mr.mesh = meshHandle;
  // 地形専用シェーダーを使用（グリッド、フォグ、ライティング）
  mr.shader = ctx.resource.LoadShader("Terrain", L"Assets/shaders/TerrainVS.hlsl",
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

  // 地形コライダーを追加（物理システムで使用）
  auto &tc = ctx.world.Add<TerrainCollider>(e);
  tc.data = terrainData;

  m_floorEntity = e;
  m_entities.push_back(e);
  
  LOG_INFO("WikiTerrain", "Generated terrain mesh with {} vertices", terrainData->vertices.size());
}

void WikiTerrainSystem::CreateWalls(core::GameContext &ctx, float width,
                                    float depth) {
  float wallHeight = 4.0f; // 高くする
  float wallThickness = 2.0f; // 厚くする
  float halfW = width * 0.5f;
  float halfD = depth * 0.5f;

  // 4方向の壁
  struct WallDef {
    float x, z, w, d;
  };
  WallDef walls[] = {
      {0.0f, halfD + wallThickness * 0.5f, width + wallThickness*2, wallThickness},  // 奥
      {0.0f, -halfD - wallThickness * 0.5f, width + wallThickness*2, wallThickness}, // 手前
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

  for (const auto &img : result.images) {
    // 画像のワールド座標への変換
    float centerX = img.x + img.width * 0.5f;
    float centerY = img.y + img.height * 0.5f;

    float worldX = (centerX / texW - 0.5f) * fieldWidth;
    float worldZ = (0.5f - centerY / texH) * fieldDepth;
    float worldW = (img.width / texW) * fieldWidth;
    float worldD = (img.height / texH) * fieldDepth;

    float height = 2.0f; // 高くする

    // 本体
    auto e = ctx.world.CreateEntity();
    auto &t = ctx.world.Add<Transform>(e);
    // Y座標はとりあえず浮かないように高めに設定して埋める
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
    // 廃止（地形に統合されるため）
}

} // namespace game::systems