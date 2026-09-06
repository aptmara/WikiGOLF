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
#include <DirectXMath.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
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
 * @brief 近隣ホール用サムネイル取得（バックグラウンドスレッド）の結果
 */
struct PendingHoleThumbnail {
    bool found = false;
    std::vector<uint8_t> pixelsBGRA;
    uint32_t width = 0;
    uint32_t height = 0;
};

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
    std::vector<graphics::PendingWikiImage> pendingImages;
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
     * @brief 実行中の経路評価へ中断を要求します。
     */
    void CancelAsyncPathEvaluations();

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
     * @brief チュートリアル専用の固定教材コースを使うか設定します。 山内陽
     */
    void SetTutorialMode(bool enabled);

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

    /**
     * @brief 遅延中の経路評価が完了していれば反映します。 山内陽
     * @param ctx ゲームコンテキスト
     * @return 経路評価結果を反映した場合は true
     */
    bool UpdateAsyncPathEvaluation(core::GameContext& ctx);

    /// @brief 構築の進捗 (0.0 - 1.0)
    float GetBuildProgress() const { return m_buildProgress; }

    /**
     * @brief 事前ロードデータをセットする（キャッシュとして使用）
     * @param links   事前取得済みリンク一覧
     * @param extract 事前取得済み記事テキスト
     */
    void SetPreloadedData(std::vector<game::WikiLink> links, std::string extract);

    /**
     * @brief 目的記事の代表サムネイルをGPUテクスチャ化して保持します。
     *        以降のCreateHole呼び出し（ページ遷移のたびに再生成される目的ホール）で
     *        使い回され、追加のAPI通信は発生しません。
     * @param ctx ゲームコンテキスト（D3D11デバイスアクセス用）
     * @param pixelsBGRA デコード済み32bpp BGRAピクセル列
     * @param width 画像幅
     * @param height 画像高さ
     */
    void SetTargetThumbnail(core::GameContext& ctx,
                            const std::vector<uint8_t>& pixelsBGRA,
                            uint32_t width, uint32_t height);

    /**
     * @brief ボール付近のホールに対し、記事サムネイル看板を遅延ロードします。
     *        毎フレーム呼び出す想定。範囲内の未取得ホールのみ非同期フェッチを開始し
     *        （同時実行数は上限あり）、完了済みのものを看板として反映、
     *        既存の全看板をカメラの方向へビルボード回転させます。
     * @param ctx ゲームコンテキスト
     * @param ballPos ボールのワールド座標（距離判定の基準）
     * @param cameraEntity ビルボード回転の基準にするカメラエンティティ
     */
    void UpdateNearbyHoleSignboards(core::GameContext& ctx,
                                    const DirectX::XMFLOAT3& ballPos,
                                    ecs::Entity cameraEntity);

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

    /// @brief デコード済みBGRAピクセル列からD3D11 SRVを作成する（失敗時nullptr）
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> CreateSRVFromPixels(
        core::GameContext& ctx, const std::vector<uint8_t>& pixelsBGRA,
        uint32_t width, uint32_t height);

    /// @brief 記事サムネイル看板（ビルボード）を旗ポールの真上に1枚生成する。
    ///        額縁の色・演出強度はhopsToTarget（GetHoleColorと同じ配色）で決まる。
    ecs::Entity CreateHoleSignboardEntity(
        core::GameContext& ctx, float x, float z, float terrainH,
        bool isTargetHole, int hopsToTarget, ID3D11ShaderResourceView* srv,
        float aspect);

    // ---- 借用ポインタ（非所有） ----
    graphics::WikiTextureGenerator*    m_textureGenerator = nullptr;
    game::systems::WikiTerrainSystem*  m_terrainSystem    = nullptr;
    graphics::SkyboxTextureGenerator*  m_skyboxGenerator  = nullptr;
    game::systems::WikiShortestPath*   m_shortestPath     = nullptr;
    bool                               m_tutorialMode     = false;

    // ---- 所有リソース ----
    std::unique_ptr<graphics::WikiTextureResult> m_wikiTexture;

    // ---- 目的記事サムネイル（ゲーム開始時に1回だけ生成し使い回す） ----
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_targetThumbnailSRV;
    float m_targetThumbnailAspect = 1.0f; // width / height
    bool m_hasTargetThumbnail = false;

    /**
     * @brief 記事タイトルごとのサムネイル取得状況（ゲームセッション全体で使い回すキャッシュ）。
     */
    struct HoleThumbnailState {
        enum class Status { Fetching, Ready, Failed };
        Status status = Status::Fetching;
        std::future<PendingHoleThumbnail> future;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
        float aspect = 1.0f;
    };
    std::unordered_map<std::string, HoleThumbnailState> m_holeThumbnailCache;
    int m_activeThumbnailFetches = 0;
    static constexpr int kMaxConcurrentThumbnailFetches = 3;
    static constexpr float kThumbnailLoadRadius = 45.0f;
    // 距離判定・フェッチ管理・キャッシュ走査は毎フレームではなくこの間隔でのみ実行する
    // （ビルボード回転自体は毎フレーム行うので見た目の滑らかさは変わらない）
    float m_signboardScanTimer = 0.0f;
    static constexpr float kSignboardScanInterval = 0.5f;

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
     * @brief ビルドステップ名をログ用に返します。 山内陽
     */
    static const char* BuildStepName(BuildStep step);

    /**
     * @brief ステップ変更時に前ステップ完了と次ステップ開始を記録します。 山内陽
     */
    void LogBuildStepTransition();

    /**
     * @brief 長時間継続しているステップの進捗を間隔を空けて記録します。 山内陽
     */
    void LogLongRunningBuildStep();

    /**
     * @brief 経路評価タスクを非同期で開始します。 山内陽
     * @param targetPageId 目標ページIDです。山内陽
     * @param maxDepth 探索する最大リンク深度です。山内陽
     */
    void StartAsyncPathEvaluation(int targetPageId, int maxDepth);

    /**
     * @brief 完了済みの経路評価タスクを候補と既存ホールへ反映します。 山内陽
     */
    bool TryConsumePathEvaluation(core::GameContext& ctx, bool updateWorld);

    /**
     * @brief 経路評価タスクから届いた部分結果を未消費分だけ反映します。山内陽
     */
    size_t ConsumePartialPathEvaluation(core::GameContext& ctx,
                                        bool updateWorld);

    /**
     * @brief 既に生成済みのホール表示へ経路評価結果を反映します。 山内陽
     */
    void ApplyPathEvaluationToWorld(
        core::GameContext& ctx,
        const std::vector<HolePlacementCandidate>& evaluatedCandidates);

    /**
     * @brief 古い経路評価タスクをロード処理から切り離します。 山内陽
     */
    void RetireActivePathEvaluationTask();

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
     * @brief 評価結果を同じリンク候補へ反映します。 山内陽
     */
    void ApplyPathEvaluationResults(
        const std::vector<HolePlacementCandidate>& evaluatedCandidates);

    /**
     * @brief 現在の経路評価候補から最短ホップ数を取得します。 山内陽
     */
    int FindMinResolvedHopsToTarget() const;

    /**
     * @brief 経路評価結果を元に Par を再計算します。 山内陽
     */
    void RefreshParFromPathEvaluation(core::GameContext& ctx,
                                      bool allowFallback);

    /**
     * @brief 候補のリンク距離を評価します。 山内陽
     */
    void EvaluateCandidatePath(core::GameContext& ctx,
                               HolePlacementCandidate& candidate);

    /**
     * @brief マップ候補がプレイ可能ホールとして選ばれたかを判定します。 山内陽
     */
    bool IsPlayableCandidate(const HolePlacementCandidate& candidate) const;

    /**
     * @brief 既存選抜候補から十分離れているかを判定します。 山内陽
     */
    bool IsFarEnoughFromSelected(
        const std::vector<HolePlacementCandidate>& selected,
        const HolePlacementCandidate& candidate,
        float minDistance) const;

    BuildStep m_buildStep = BuildStep::None;
    BuildStep m_loggedBuildStep = BuildStep::None;
    PageDataAsyncResult m_buildData;
    PageLoadResult m_buildResult;
    uint64_t m_buildLoadId = 0;
    inline static std::atomic<uint64_t> s_nextBuildLoadId{1};
    std::chrono::steady_clock::time_point m_buildStartedAt =
        std::chrono::steady_clock::time_point::min();
    std::chrono::steady_clock::time_point m_stepStartedAt =
        std::chrono::steady_clock::time_point::min();
    std::chrono::steady_clock::time_point m_lastLongStepLogAt =
        std::chrono::steady_clock::time_point::min();
    std::chrono::steady_clock::time_point m_lastCreateHolesProgressLogAt =
        std::chrono::steady_clock::time_point::min();

    ecs::Entity m_buildBall = UINT32_MAX;
    ecs::Entity m_buildCamera = UINT32_MAX;
    ecs::Entity m_buildSkybox = UINT32_MAX;
    game::controllers::MinimapController* m_buildMinimap = nullptr;

    std::vector<std::pair<std::string, std::wstring>> m_buildValidLinks;
    std::vector<std::pair<std::wstring, std::string>> m_buildLinkPairs;
    std::vector<HolePlacementCandidate> m_buildHoleCandidates;
    std::vector<HolePlacementCandidate> m_buildPathCandidates;
    std::vector<HolePlacementCandidate> m_buildMapHoleCandidates;
    std::vector<graphics::LinkRegion> m_buildGameplayLinks;
    std::unordered_map<std::string, int> m_pathHopCache;
    std::future<std::vector<HolePlacementCandidate>> m_pathEvaluationTask;
    std::vector<std::future<std::vector<HolePlacementCandidate>>> m_retiredPathEvaluationTasks;
    std::shared_ptr<std::atomic<bool>> m_pathEvaluationCancel;
    std::shared_ptr<std::atomic<size_t>> m_pathEvaluationProgress;
    std::shared_ptr<std::atomic<size_t>> m_pathEvaluationTotal;
    std::shared_ptr<std::mutex> m_pathEvaluationPartialMutex;
    std::shared_ptr<std::vector<HolePlacementCandidate>> m_pathEvaluationPartialResults;
    size_t m_pathEvaluationConsumedResults = 0;
    bool m_pathEvaluationStarted = false;

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
