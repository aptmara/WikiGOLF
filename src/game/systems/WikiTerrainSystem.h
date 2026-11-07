#pragma once
/**
 * @file WikiTerrainSystem.h
 * @brief Wikipedia記事情報に基づいた地形（フィールド）生成システム
*/

#include "../../graphics/WikiTextureGenerator.h"
#include "../../resources/ResourceManager.h"
#include "../systems/TerrainBackdrop.h"
#include "../systems/TerrainGenerator.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>
#include <deque>
#include <future>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace core {
struct GameContext;
}

namespace ecs {
using Entity = unsigned int;
}

namespace game::systems {

/**
 * @brief Wiki地形システム
 *
 * 記事のレイアウト情報（WikiTextureResult）を受け取り、
 * 物理挙動を持つ3Dオブジェクト群（床、壁、障害物、段差）を生成する。
*/
class WikiTerrainSystem {
public:
  WikiTerrainSystem() = default;
  ~WikiTerrainSystem() = default;

  /** @brief チュートリアル専用の固定教材地形を使うか設定します。*/
  void SetTutorialMode(bool enabled) { m_tutorialMode = enabled; }

  /** @brief フィールドを再構築する（同期版 / 後方互換用）*/
  void BuildField(core::GameContext &ctx, const std::string &pageTitle,
                  const graphics::WikiTextureResult &textureResult,
                  float fieldWidth, float fieldDepth,
                  const std::vector<std::string> &pageCategories = {});

  // ------------------------------------------------------------------
  // インクリメンタルビルド API
  // TerrainGeneratorをstd::asyncで非同期化し、メッシュを1タイル/フレームで構築する。
  // ------------------------------------------------------------------

  /**
   * @brief インクリメンタルビルドを開始する
   * @note テクスチャ結果はコピーして保持する（呼び出し元がムーブしても安全）
*/
  void BeginBuildField(const std::string &pageTitle,
                       const graphics::WikiTextureResult &textureResult,
                       float fieldWidth, float fieldDepth,
                       const std::vector<std::string> &pageCategories);

  /**
   * @brief 構築を1ステップ進める（毎フレーム1回呼ぶ）
   * @return 完了したら true
*/
  bool StepBuildField(core::GameContext &ctx);

  /** @brief 構築進捗 (0.0-1.0)*/
  float GetBuildProgress() const { return m_buildProgress; }

  /** @brief 現在のフィールドエンティティ群を取得*/
  const std::vector<ecs::Entity> &GetEntities() const { return m_entities; }

  /** @brief フィールドを全削除*/
  void Clear(core::GameContext &ctx);

  /** @brief 床のエンティティID取得（カメラターゲット用など）*/
  ecs::Entity GetFloorEntity() const { return m_floorEntity; }

  /** @brief 地形データを取得（物理パラメータ参照用）*/
  std::shared_ptr<TerrainData> GetTerrainData() const { return m_terrainData; }

  /** @brief 記事のタイルを上から順に取得*/
  const std::vector<graphics::WikiTextureResult::Tile> &GetArticleTiles() const {
    return m_buildTiles;
  }

  /** @brief 指定座標の地形高さを取得*/
  float GetHeight(float x, float z) const;

  /**
   * @brief コース外の延長地形（山並み）も含めた、見た目上の地面の高さを取得します。
   * @details カメラが地形にめり込まないようにする判定用。コース内は GetHeight と同じ。
   */
  float GetSceneryHeight(float x, float z) const;

  /** @brief ボールの位置・速度・接地を、芝の踏み跡の入力（GrassTrampleInput）へ渡す*/
  void UpdateSurfaceResponse(core::GameContext &ctx, ecs::Entity ballEntity);

  /** @brief 空に浮かぶ地球儀をゆっくり自転・上下させる。*/
  void UpdateSkyGlobes(core::GameContext &ctx, float dt);

private:
  std::vector<ecs::Entity> m_entities;
  ecs::Entity m_floorEntity = 0xFFFFFFFF;
  std::shared_ptr<TerrainData> m_terrainData;
  bool m_tutorialMode = false;

  /** @brief 床作成（同期版 / BuildFieldから呼ぶ）*/
  void CreateFloor(core::GameContext &ctx,
                   const graphics::WikiTextureResult &result, float width,
                   float depth, const std::string &pageTitle,
                   const std::vector<std::string> &pageCategories);

  /**
   * @brief 外周壁エンティティを生成します。
   * @param ctx ゲームコンテキスト
   * @param width フィールド幅
   * @param depth フィールド奥行き
   */
  void CreateWalls(core::GameContext &ctx, float width, float depth);

  /**
   * @brief コース外側に見た目専用の山並みを配置します。
   * @param ctx ゲームコンテキスト
   * @param pageTitle 記事タイトル（山並みの形の種）
   */
  void CreateBackdrop(core::GameContext &ctx, const std::string &pageTitle);

  /**
   * @brief 山並みの種を確定し、地形の高さ取得で山並みを使うようにします。
   * @param pageTitle 記事タイトル（山並みの形の種）
   * @return BuildTerrainBackdrop に渡す種
   */
  uint32_t PrepareBackdrop(const std::string &pageTitle);

  /**
   * @brief 生成済みの山並みチャンクからメッシュとエンティティを作ります。
   * @param ctx ゲームコンテキスト
   * @param chunks BuildTerrainBackdrop の結果
   */
  void CreateBackdropMeshes(core::GameContext &ctx,
                            const std::vector<TerrainBackdropChunk> &chunks);

  /**
   * @brief 空に パズル地球儀をランダムに浮かべます（見た目専用）。
   * @param ctx ゲームコンテキスト
   * @param pageTitle 記事タイトル（配置の種）
   */
  void CreateSkyGlobes(core::GameContext &ctx, const std::string &pageTitle);

  struct SkyGlobe {
    ecs::Entity entity = 0;
    float scale = 1.0f;
    int lod = 0;
    DirectX::XMFLOAT3 basePosition = {0.0f, 0.0f, 0.0f};
    float tilt = 0.0f;
    float spinSpeed = 0.0f;
    float bobPhase = 0.0f;
    float bobHeight = 0.0f;
  };
  std::vector<SkyGlobe> m_skyGlobes; /**< 空に浮かぶ地球儀*/
  float m_skyGlobeTime = 0.0f;

  /**
   * @brief 画像障害物エンティティを配置します。
   * @param ctx ゲームコンテキスト
   * @param result テクスチャ生成結果
   * @param fieldWidth フィールド幅
   * @param fieldDepth フィールド奥行き
   */
  void CreateImageObstacles(core::GameContext &ctx,
                            const graphics::WikiTextureResult &result,
                            float fieldWidth, float fieldDepth);

  /**
   * @brief 見出し段差エンティティを配置します。
   * @param ctx ゲームコンテキスト
   * @param result テクスチャ生成結果
   * @param fieldWidth フィールド幅
   * @param fieldDepth フィールド奥行き
   */
  void CreateHeadingSteps(core::GameContext &ctx,
                          const graphics::WikiTextureResult &result,
                          float fieldWidth, float fieldDepth);

  /** @brief バイオーム別装飾オブジェクトを作成します。 */
  void CreateDecorations(core::GameContext &ctx, float fieldWidth,
                         float fieldDepth, int biome);

  /** @brief 地形から芝の元データ（GrassFieldSource）を作る */
  void CreateSurfaceGrass(core::GameContext &ctx);
  void ClearSurfaceGrass(core::GameContext &ctx);

  int m_biome = 0; /**< 現在のバイオーム*/
  uint32_t m_backdropSeed = 0;  /**< 延長地形の形の種*/
  bool m_hasBackdrop = false;   /**< 延長地形を生成済みか*/

  // ------------------------------------------------------------------
  // インクリメンタルビルド用状態
  // ------------------------------------------------------------------
  enum class BuildPhase {
    Idle,
    TerrainGenAsync,   /**< TerrainGenerator 非同期待ち*/
    CreatePhysics,     /**< 物理エンティティ作成*/
    LoadTerrainAlbedo, /**< 地形アルベド配列をロード*/
    LoadTerrainNormal, /**< 地形法線配列をロード*/
    LoadTerrainShader, /**< 地形シェーダーをロード*/
    LoadBasicShader,   /**< オーバーレイ用シェーダーをロード*/
    CreateTileMesh,    /**< ビジュアルメッシュ（1タイル/ステップ）*/
    CreateTileOverlay, /**< オーバーレイ（1タイル/ステップ）*/
    CreateWalls,       /**< 壁生成*/
    CreateBackdrop,    /**< 山並みの非同期生成を待ち、メッシュと空の地球儀を作成*/
    CreateDecorations, /**< 装飾生成*/
    CreateSurfaceGrass,/**< 芝の元データ生成*/
    Done
  };

  BuildPhase m_buildPhase   = BuildPhase::Idle;
  float      m_buildProgress = 0.0f;

  // BeginBuildFieldで保存した入力データ
  std::string              m_buildPageTitle;
  float                    m_buildFieldWidth      = 0.0f;
  float                    m_buildFieldDepth      = 0.0f;
  std::vector<std::string> m_buildPageCategories;

  // テクスチャ結果コピー（BeginBuildFieldが呼ばれた時点のコピー）
  std::vector<graphics::WikiTextureResult::Tile> m_buildTiles;
  std::vector<graphics::LinkRegion>              m_buildLinks;
  uint32_t m_buildTexWidth  = 0;
  uint32_t m_buildTexHeight = 0;

  // TerrainGenerator 非同期タスク
  std::future<TerrainData> m_terrainFuture;

  // 山並み生成の非同期タスク（地形データの完成後に開始）
  std::future<std::vector<TerrainBackdropChunk>> m_backdropFuture;

  // タイルごとのインデックス
  size_t m_buildTileIndex = 0;

  // ビジュアルメッシュキャッシュ（CreateTileMeshとCreateTileOverlayで共用）
  struct TileMeshCache {
    std::vector<graphics::Vertex> vertices;
    std::vector<uint32_t>         indices;
    int     resX     = 0;
    int     tileResZ = 0;
    float   vStart   = 0.0f;
    float   vEnd     = 0.0f;
    float   maxHeight = 0.0f; /**< タイル内の頂点最大Y（ミニマップオーバーレイの平面高さ算出用）*/
  };
  std::vector<TileMeshCache> m_tileMeshCaches;

  // シェーダー/テクスチャハンドルキャッシュ（BuildPhaseをまたいで保持）
  resources::ShaderHandle   m_buildTerrainShader;
  resources::ShaderHandle   m_buildBasicShader;
  // ComPtrで保持：ResourceManagerが返す型と一致させる
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_buildAlbedoSRV;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_buildNormalSRV;
  int   m_buildResX    = 64;
  int   m_buildResZ    = 64;
};

} // namespace game::systems
