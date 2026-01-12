#pragma once
/**
 * @file WikiTerrainSystem.h
 * @brief Wikipedia記事情報に基づいた地形（フィールド）生成システム
 */

#include "../../graphics/WikiTextureGenerator.h"
#include <memory>
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

  /// @brief フィールドを再構築する
  /// @param ctx ゲームコンテキスト
  /// @param pageTitle 記事タイトル（シードとして使用）
  /// @param textureResult テクスチャ生成結果（画像・見出し座標入り）
  /// @param fieldWidth フィールドのワールド幅
  /// @param fieldDepth フィールドのワールド奥行き
  void BuildField(core::GameContext &ctx, const std::string& pageTitle,
                  const graphics::WikiTextureResult &textureResult,
                  float fieldWidth, float fieldDepth);

  /// @brief 現在のフィールドエンティティ群を取得
  const std::vector<ecs::Entity> &GetEntities() const { return m_entities; }

  /// @brief フィールドを全削除
  void Clear(core::GameContext &ctx);

  /// @brief 床のエンティティID取得（カメラターゲット用など）
  ecs::Entity GetFloorEntity() const { return m_floorEntity; }

private:
  std::vector<ecs::Entity> m_entities;
  ecs::Entity m_floorEntity = 0xFFFFFFFF; // 無効値

  /// @brief 床作成
  void CreateFloor(core::GameContext &ctx,
                   const graphics::WikiTextureResult &result, float width,
                   float depth, const std::string& pageTitle);

  /// @brief 壁作成
  void CreateWalls(core::GameContext &ctx, float width, float depth);

  /// @brief 画像障害物作成
  void CreateImageObstacles(core::GameContext &ctx,
                            const graphics::WikiTextureResult &result,
                            float fieldWidth, float fieldDepth);

  /// @brief 見出し段差作成
  void CreateHeadingSteps(core::GameContext &ctx,
                          const graphics::WikiTextureResult &result,
                          float fieldWidth, float fieldDepth);
};

} // namespace game::systems
