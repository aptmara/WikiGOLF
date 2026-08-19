#pragma once
/**
 * @file WikiTerrainSystem.h
 * @brief Wikipedia記事情報に基づいた地形（フィールド）生成システム
 */

#include "../../graphics/WikiTextureGenerator.h"
#include "../../resources/ResourceManager.h"
#include "../systems/TerrainGenerator.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <future>
#include <memory>
#include <string>
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

  /// @brief チュートリアル専用の固定教材地形を使うか設定します。 山内陽
  void SetTutorialMode(bool enabled) { m_tutorialMode = enabled; }

  /// @brief フィールドを再構築する（同期版 / 後方互換用）
  void BuildField(core::GameContext &ctx, const std::string &pageTitle,
                  const graphics::WikiTextureResult &textureResult,
                  float fieldWidth, float fieldDepth,
                  const std::vector<std::string> &pageCategories = {});

  // ------------------------------------------------------------------
  // インクリメンタルビルド API
  // TerrainGeneratorをstd::asyncで非同期化し、メッシュを1タイル/フレームで構築する。
  // ------------------------------------------------------------------

  /// @brief インクリメンタルビルドを開始する
  /// @note テクスチャ結果はコピーして保持する（呼び出し元がムーブしても安全）
  void BeginBuildField(const std::string &pageTitle,
                       const graphics::WikiTextureResult &textureResult,
                       float fieldWidth, float fieldDepth,
                       const std::vector<std::string> &pageCategories);

  /// @brief 構築を1ステップ進める（毎フレーム1回呼ぶ）
  /// @return 完了したら true
  bool StepBuildField(core::GameContext &ctx);

  /// @brief 構築進捗 (0.0-1.0)
  float GetBuildProgress() const { return m_buildProgress; }

  /// @brief 現在のフィールドエンティティ群を取得
  const std::vector<ecs::Entity> &GetEntities() const { return m_entities; }

  /// @brief フィールドを全削除
  void Clear(core::GameContext &ctx);

  /// @brief 床のエンティティID取得（カメラターゲット用など）
  ecs::Entity GetFloorEntity() const { return m_floorEntity; }

  /// @brief 地形データを取得（物理パラメータ参照用）
  std::shared_ptr<TerrainData> GetTerrainData() const { return m_terrainData; }

  /// @brief 指定座標の地形高さを取得
  float GetHeight(float x, float z) const;

  /// @brief ボール近傍の芝を倒し、離れた芝を元の姿勢へ戻す
  void UpdateSurfaceResponse(core::GameContext &ctx, ecs::Entity ballEntity,
                             float dt);

private:
  struct GrassPatch {
    ecs::Entity entity = 0xFFFFFFFF;
    DirectX::XMFLOAT3 position = {0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT2 interactionPoint = {100000.0f, 100000.0f};
    float interactionYaw = 0.0f;
    float response = 0.0f;
    float halfExtent = 1.0f;
  };

  std::vector<ecs::Entity> m_entities;
  std::vector<GrassPatch> m_grassPatches;
  ecs::Entity m_floorEntity = 0xFFFFFFFF;
  std::shared_ptr<TerrainData> m_terrainData;
  bool m_tutorialMode = false;

  /// @brief 床作成（同期版 / BuildFieldから呼ぶ）
  void CreateFloor(core::GameContext &ctx,
                   const graphics::WikiTextureResult &result, float width,
                   float depth, const std::string &pageTitle,
                   const std::vector<std::string> &pageCategories);

  void CreateWalls(core::GameContext &ctx, float width, float depth);
  void CreateImageObstacles(core::GameContext &ctx,
                            const graphics::WikiTextureResult &result,
                            float fieldWidth, float fieldDepth);
  void CreateHeadingSteps(core::GameContext &ctx,
                          const graphics::WikiTextureResult &result,
                          float fieldWidth, float fieldDepth);
  /// @brief バイオーム別装飾オブジェクト作成
  void CreateDecorations(core::GameContext &ctx, float fieldWidth,
                         float fieldDepth, int biome);
  void CreateSurfaceGrass(core::GameContext &ctx, float fieldWidth,
                          float fieldDepth);

  int m_biome = 0; ///< 現在のバイオーム

  // ------------------------------------------------------------------
  // インクリメンタルビルド用状態
  // ------------------------------------------------------------------
  enum class BuildPhase {
    Idle,
    TerrainGenAsync,   ///< TerrainGenerator 非同期待ち
    CreatePhysics,     ///< 物理エンティティ作成
    CreateTileMesh,    ///< ビジュアルメッシュ（1タイル/ステップ）
    CreateTileOverlay, ///< オーバーレイ（1タイル/ステップ）
    CreateWalls,       ///< 壁生成
    CreateDecorations, ///< 装飾生成
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
    float   maxHeight = 0.0f; ///< タイル内の頂点最大Y（ミニマップオーバーレイの平面高さ算出用）
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
