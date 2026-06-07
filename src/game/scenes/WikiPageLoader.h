#pragma once
/**
 * @file WikiPageLoader.h
 * @brief Wikipedia記事のロード・フィールド生成・ホール配置を行うクラス
 */

#include "../../core/GameContext.h"
#include "../../ecs/Entity.h"
#include "../../graphics/SkyboxTextureGenerator.h"
#include "../../graphics/WikiTextureGenerator.h"
#include "../systems/WikiClient.h"
#include "../systems/WikiShortestPath.h"
#include "../systems/WikiTerrainSystem.h"
#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace game::systems {
class WikiShortestPath;
} // namespace game::systems

namespace game::components {
struct GolfGameState;
} // namespace game::components

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
    std::vector<std::string> pageCategories;
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
     * @brief インクリメンタルな構築を開始する
     */
    void BeginBuildPage(core::GameContext& ctx,
                        PageDataAsyncResult asyncData,
                        ecs::Entity ballEntity,
                        ecs::Entity cameraEntity,
                        ecs::Entity skyboxEntity,
                        game::controllers::MinimapController* minimapController);

    /**
     * @brief 構築を 1 ステップ進める
     * @return 完了したら true
     */
    bool StepBuildPage(core::GameContext& ctx);

    /**
     * @brief 1フレーム内の時間予算内で構築を進めます。 山内陽
     * @param ctx ゲームコンテキスト
     * @param budget メインスレッド構築に使える最大時間
     * @return 完了したら true
     */
    bool StepBuildPageWithinFrameBudget(core::GameContext& ctx,
                                        std::chrono::milliseconds budget);

    /// @brief 構築の進捗 (0.0 - 1.0)
    float GetBuildProgress() const { return m_buildProgress; }

    /**
     * @brief 事前ロードデータをセットする（キャッシュとして使用）
     * @param links   事前取得済みリンク一覧
     * @param extract 事前取得済み記事テキスト
     */
    void SetPreloadedData(std::vector<game::WikiLink> links, std::string extract);

    /**
     * @brief ホールを生成します。
     */
    void CreateHole(core::GameContext& ctx, float x, float z,
                    const std::string& linkTarget, bool isTargetHole,
                    int hopsToTarget = -1, bool addMapIcon = true);

    /**
     * @brief 最後に生成したテクスチャ結果を取得します。
     */
    const graphics::WikiTextureResult* GetWikiTexture() const {
        return m_wikiTexture.get();
    }

    /**
     * @brief フィールドの幅を取得します。
     */
    float GetFieldWidth() const { return m_fieldWidth; }
    
    /**
     * @brief フィールドの奥行きを取得します。
     */
    float GetFieldDepth() const { return m_fieldDepth; }

private:
    /**
     * @brief 生成済みのページ関連エンティティを破棄します。 山内陽
     */
    void ClearGeneratedPageObjects(core::GameContext& ctx,
                                   game::controllers::MinimapController* minimapController);

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

    // 構築状態
    enum class BuildStep {
        None,
        ClearOldHoles,
        PrepareLinks,
        BeginTexture,
        GenerateTextureTiles,
        ApplySkybox,
        BeginTerrain,     ///< 地形生成処理の開始
        BuildTerrainStep, ///< 地形生成処理のインクリメンタル更新
        RepositionBall,
        EvaluateHoles,    ///< 全リンク領域のホール候補化
        EvaluateHolePaths, ///< 一部候補のリンク距離評価
        CreateMapIcons,   ///< マップビュー用の軽量リンク表示
        CreateHoles,
        SetupWind,
        Finish
    };

    /**
     * @brief テクスチャリンクから作ったホール配置候補です。 山内陽
     */
    struct HolePlacementCandidate {
        float x = 0.0f;
        float z = 0.0f;
        std::string linkTarget;
        bool isTarget = false;
        int hopsToTarget = -1;
        size_t originalIndex = 0;
        bool isPlayable = false;
    };

    /**
     * @brief リンク領域からホール候補を作ります。 山内陽
     */
    HolePlacementCandidate BuildHolePlacementCandidate(
        const graphics::LinkRegion& link,
        size_t originalIndex) const;

    /**
     * @brief マップビューに残す軽量リンク候補を選びます。 山内陽
     */
    std::vector<HolePlacementCandidate> SelectMapHoleIconCandidates(
        const std::vector<HolePlacementCandidate>& candidates) const;

    /**
     * @brief リンク距離を評価する代表候補を安い条件だけで絞ります。 山内陽
     */
    std::vector<HolePlacementCandidate> SelectPathEvaluationCandidates(
        const std::vector<HolePlacementCandidate>& candidates) const;

    /**
     * @brief 評価結果を同じリンク候補へ反映します。 山内陽
     */
    void ApplyPathEvaluationResults(
        const std::vector<HolePlacementCandidate>& evaluatedCandidates);

    /**
     * @brief 候補のリンク距離をページ内キャッシュ付きで評価します。 山内陽
     */
    void EvaluateCandidatePath(core::GameContext& ctx,
                               HolePlacementCandidate& candidate);

    /**
     * @brief マップ候補がプレイ可能ホールとして選ばれたかを判定します。 山内陽
     */
    bool IsPlayableCandidate(const HolePlacementCandidate& candidate) const;

    /**
     * @brief リンク距離を評価した代表ホールをマップ候補にも必ず含めます。 山内陽
     */
    void EnsurePathCandidatesHaveMapIcons();

    /**
     * @brief 既存選抜候補から十分離れているかを判定します。 山内陽
     */
    bool IsFarEnoughFromSelected(
        const std::vector<HolePlacementCandidate>& selected,
        const HolePlacementCandidate& candidate,
        float minDistance) const;

    BuildStep m_buildStep = BuildStep::None;
    PageDataAsyncResult m_buildData;
    PageLoadResult m_buildResult;

    ecs::Entity m_buildBall = UINT32_MAX;
    ecs::Entity m_buildCamera = UINT32_MAX;
    ecs::Entity m_buildSkybox = UINT32_MAX;
    game::controllers::MinimapController* m_buildMinimap = nullptr;

    std::vector<std::pair<std::string, std::wstring>> m_buildValidLinks;
    std::vector<std::pair<std::wstring, std::string>> m_buildLinkPairs;
    std::vector<HolePlacementCandidate> m_buildHoleCandidates;
    std::vector<HolePlacementCandidate> m_buildPathCandidates;
    std::vector<HolePlacementCandidate> m_buildMapHoleCandidates;
    std::unordered_map<std::string, int> m_pathHopCache;

    size_t m_nextHoleIndex = 0;
    size_t m_nextMapIconIndex = 0;
    size_t m_nextPathIndex = 0;
    graphics::WikiTextureGenerationState m_textureState;

    float m_buildFieldWidth = 80.0f;
    float m_buildFieldDepth = 120.0f;
    float m_buildTexScale = 1.0f;
    float m_buildProgress = 0.0f;
    uint32_t m_buildTexWidth = 0;
    uint32_t m_buildTexHeight = 0;
    std::chrono::steady_clock::time_point m_buildDeadline =
        std::chrono::steady_clock::time_point::max();
};

} // namespace game::scenes
