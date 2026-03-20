#pragma once
/**
 * @file WikiPageLoader.h
 * @brief Wikipedia記事の取得・テクスチャ生成・地形構築・ホール配置を担うローダー
 *
 * 入力: 記事名、GameContext、WikiShortestPath（SDOW）
 * 出力: ECSエンティティ群（フィールド・ホール・スカイボックス）の更新、
 *       GolfGameState のフィールドサイズ・風・Par 設定
 */

#include "../../core/GameContext.h"
#include "../../ecs/Entity.h"
#include "../../graphics/SkyboxTextureGenerator.h"
#include "../../graphics/WikiTextureGenerator.h"
#include "../systems/WikiClient.h"
#include "../systems/WikiShortestPath.h"
#include "../systems/WikiTerrainSystem.h"
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace game::systems {
class WikiShortestPath;
} // namespace game::systems

namespace game::controllers {
class MinimapController;
} // namespace game::controllers

namespace game::scenes {

/**
 * @brief ページロードの結果
 */
struct PageLoadResult {
    float fieldWidth  = 80.0f;
    float fieldDepth  = 120.0f;
    int   calculatedPar = -1;
    bool  success     = false;
};

/**
 * @brief 非同期取得する記事データ
 */
struct PageDataAsyncResult {
    std::string pageName;
    std::string articleText;
    std::vector<game::WikiLink> allLinks;
    bool hasData = false;
};

/**
 * @brief ページロード用コントローラ
 * WikiGolfScene の LoadPage / CreateHole / CreateLinksFromTexture を移管する。
 */
class WikiPageLoader {
public:
    WikiPageLoader() = default;
    ~WikiPageLoader() = default;

    /**
     * @brief 内部システムの参照を設定する（所有権は WikiGolfScene 側が持つ）
     * @param textureGen  テクスチャジェネレータ（非 null 必須）
     * @param terrainSys  地形システム（非 null 必須）
     * @param skyboxGen   スカイボックスジェネレータ（非 null 必須）
     * @param shortestPath SDOW 最短パスシステム（null 許容）
     */
    void SetSystems(graphics::WikiTextureGenerator* textureGen,
                    game::systems::WikiTerrainSystem* terrainSys,
                    graphics::SkyboxTextureGenerator* skyboxGen,
                    game::systems::WikiShortestPath* shortestPath);

    /**
     * @brief 記事の地形・ホールを構築する
     * @param ctx       ゲームコンテキスト
     * @param pageName  ロードする記事名
     * @param ballEntity ボールエンティティ（ボール再配置に使用）
     * @param cameraEntity カメラエンティティ（farZ 更新に使用）
     * @param skyboxEntity スカイボックスエンティティ（テーマ更新に使用）
     * @param minimapController ミニマップ（中心同期に使用。null 許容）
     * @return ロード結果（フィールドサイズ・Par 等）
     */
    PageLoadResult LoadPage(core::GameContext& ctx,
                            const std::string& pageName,
                            ecs::Entity ballEntity,
                            ecs::Entity cameraEntity,
                            ecs::Entity skyboxEntity,
                            game::controllers::MinimapController* minimapController);

    /**
     * @brief 記事データとリンクを非同期で取得する（通信・DB処理）
     * @param pageName ロードする記事名
     * @return 取得したデータ
     */
    PageDataAsyncResult FetchPageDataAsync(const std::string& pageName);

    /**
     * @brief 取得したデータをもとに、メインスレッドでGPUリソースとエンティティを生成する
     */
    PageLoadResult BuildPageSync(core::GameContext& ctx,
                                 PageDataAsyncResult asyncData,
                                 ecs::Entity ballEntity,
                                 ecs::Entity cameraEntity,
                                 ecs::Entity skyboxEntity,
                                 game::controllers::MinimapController* minimapController);

    /**
     * @brief 事前ロードデータをセットする（キャッシュとして使用）
     * @param links   事前取得済みリンク一覧
     * @param extract 事前取得済み記事テキスト
     */
    void SetPreloadedData(std::vector<game::WikiLink> links, std::string extract);

    /// @brief ホールを一個作成する（CreateLinksFromTexture/LoadPage 内部から呼ぶ）
    void CreateHole(core::GameContext& ctx, float x, float z,
                    const std::string& linkTarget, bool isTargetHole,
                    int hopsToTarget = -1);

    /// @brief 最後に生成したテクスチャ結果を取得（外部参照用）
    const graphics::WikiTextureResult* GetWikiTexture() const {
        return m_wikiTexture.get();
    }

    /// @brief フィールドサイズ取得
    float GetFieldWidth() const { return m_fieldWidth; }
    float GetFieldDepth() const { return m_fieldDepth; }

private:
    /// @brief テクスチャのリンク領域からホールを一括配置する
    void CreateLinksFromTexture(core::GameContext& ctx);

    // ---- 借用ポインタ（非所有） ----
    graphics::WikiTextureGenerator*    m_textureGenerator = nullptr;
    game::systems::WikiTerrainSystem*  m_terrainSystem    = nullptr;
    graphics::SkyboxTextureGenerator*  m_skyboxGenerator  = nullptr;
    game::systems::WikiShortestPath*   m_shortestPath     = nullptr;

    // ---- 所有リソース ----
    std::unique_ptr<graphics::WikiTextureResult> m_wikiTexture;

    // ---- フィールドサイズ（LoadPage 後に確定） ----
    float m_fieldWidth = 80.0f;
    float m_fieldDepth = 120.0f;

    // ---- 事前ロードキャッシュ ----
    bool                          m_hasPreloadedData = false;
    std::vector<game::WikiLink>   m_preloadedLinks;
    std::string                   m_preloadedExtract;
};

} // namespace game::scenes
