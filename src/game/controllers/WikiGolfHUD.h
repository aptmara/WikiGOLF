#pragma once
/**
 * @file WikiGolfHUD.h
 * @brief ゲーム画面のHUD表示要素を管理するクラス
 */

#include "../../core/GameContext.h"
#include "../../ecs/Entity.h"
#include "../components/WikiComponents.h"
#include <DirectXMath.h>
#include <vector>
#include <string>

namespace game::controllers {

// クラブUIに必要な情報
struct ClubUIData {
    std::string name;
    std::string iconTexture;
    std::string shortName;
    std::string categoryEN;
};

class WikiGolfHUD {
public:
    // -------------------------------------------------------
    // UIエンティティ管理
    // -------------------------------------------------------
    struct UIEntities {
        // 左上: ブラウザ風コース情報
        ecs::Entity browserBgEntity         = UINT32_MAX; // 背景パネル
        ecs::Entity browserTabIconEntity    = UINT32_MAX; // バッジ
        ecs::Entity browserCurrentPageEntity= UINT32_MAX; // 現在地表示
        ecs::Entity browserTargetEntity     = UINT32_MAX; // 目的地表示
        ecs::Entity browserShotInfoEntity   = UINT32_MAX; // スコア情報
        ecs::Entity browserHistoryEntity    = UINT32_MAX; // 経路履歴
        
        ecs::Entity clubHeaderEntity        = UINT32_MAX; // ヘッダー

        // 互換用エイリアス
        ecs::Entity browserTabIconEntityAlias = UINT32_MAX;
        ecs::Entity headerEntity            = UINT32_MAX;
        ecs::Entity shotCountEntity         = UINT32_MAX;
        ecs::Entity infoEntity              = UINT32_MAX;
        ecs::Entity pathEntity              = UINT32_MAX;

        // 上中央: 風情報カード
        ecs::Entity windEntity              = UINT32_MAX;
        ecs::Entity windArrowEntity         = UINT32_MAX;
        ecs::Entity windCardLabelEntity     = UINT32_MAX; // 風ラベル
        ecs::Entity windCardValueEntity     = UINT32_MAX; // 風速値
        ecs::Entity windCardUnitEntity      = UINT32_MAX; // 風向と単位

        // 左側: クラブ選択リスト
        std::vector<ecs::Entity> clubBgEntities;   
        std::vector<ecs::Entity> clubIconEntities; 
        std::vector<ecs::Entity> clubNameEntities; 
        std::vector<ecs::Entity> clubSubNameEntities; 
        std::vector<ecs::Entity> clubArrowEntities;

        // 右下: ショットボタン
        ecs::Entity shotButtonBgEntity      = UINT32_MAX; 
        ecs::Entity shotButtonTextEntity    = UINT32_MAX; 

        // 左下: 操作ヘルプ
        ecs::Entity controlHintEntity       = UINT32_MAX;

        // 下部中央: 情報パネル
        ecs::Entity distPanelBgEntity       = UINT32_MAX; 
        ecs::Entity distLabelEntity         = UINT32_MAX;
        ecs::Entity distValueEntity         = UINT32_MAX;
        ecs::Entity heightLabelEntity       = UINT32_MAX;
        ecs::Entity heightValueEntity       = UINT32_MAX;
        
        ecs::Entity clubInfoPanelBgEntity   = UINT32_MAX; // クラブパネル
        ecs::Entity clubInfoLabelEntity     = UINT32_MAX;
        ecs::Entity clubInfoNameEntity      = UINT32_MAX;
        ecs::Entity clubInfoIconEntity      = UINT32_MAX;
        ecs::Entity clubInfoShortNameEntity = UINT32_MAX;
        
        ecs::Entity liePanelBgEntity        = UINT32_MAX; // ライ情報パネル
        ecs::Entity lieLabelEntity          = UINT32_MAX;
        ecs::Entity lieValueEntity          = UINT32_MAX;
        ecs::Entity lieCondLabelEntity      = UINT32_MAX;
        ecs::Entity lieCondValueEntity      = UINT32_MAX;

        // ショット時のみ表示
        ecs::Entity shotPanelPowerLabelEntity   = UINT32_MAX;
        ecs::Entity shotPanelPowerValueEntity   = UINT32_MAX;
        ecs::Entity shotPanelAccuracyLabelEntity= UINT32_MAX;
        ecs::Entity shotPanelAccuracyValueEntity= UINT32_MAX;
        ecs::Entity shotPanelClubLabelEntity    = UINT32_MAX;
        ecs::Entity gaugeBarEntity          = UINT32_MAX;
        ecs::Entity gaugeFillEntity         = UINT32_MAX;
        ecs::Entity gaugeMarkerEntity       = UINT32_MAX;
        ecs::Entity judgeEntity             = UINT32_MAX;
    };

    /**
     * @brief HUD用の初期化処理を行います。
     */
    void Initialize(core::GameContext& ctx);

    /**
     * @brief 毎フレーム更新
     * @param clubNames クラブ名リスト (表示順)
     * @param currentClubIndex 現在選択中のインデックス
     */
    void Update(core::GameContext& ctx, float dt,
                const game::components::GolfGameState& state,
                float currentPower, float confirmedPower,
                float windSpeed, const DirectX::XMFLOAT2& windDir, float cameraYaw,
                const std::vector<ClubUIData>& clubs,
                int currentClubIndex,
                float distanceToTarget, float heightDiff);

    /**
     * @brief パワーゲージの値を更新します。
     */
    void UpdatePowerGauge(core::GameContext& ctx, float fillValue, float markerValue, float minPower, float maxPower);

    /**
     * @brief 打球判定結果を表示します。
     */
    void UpdateJudge(core::GameContext& ctx, const std::wstring& text, const DirectX::XMFLOAT4& color);

    /**
     * @brief ショット関連のUI表示をリセットします。
     */
    void ResetShotUI(core::GameContext& ctx);

    /**
     * @brief ゲージUIの表示・非表示を切り替えます。
     */
    void SetGaugeVisible(core::GameContext& ctx, bool visible);

    /**
     * @brief インパクトゾーンの表示・非表示を切り替えます。
     */
    void SetImpactZonesVisible(core::GameContext& ctx, bool visible);

    /// @brief 通常時 (Idle) <-> ショット時の UI 切り替え
    void SetShotPhaseUIVisible(core::GameContext& ctx, bool shotPhase);

    /// @brief HUD全体の表示/非表示を切り替える（ロード中は非表示にするため）
    /// @param visible true=表示, false=非表示
    void SetVisible(core::GameContext& ctx, bool visible);

private:
    void InitializeCourseInfoPanel(core::GameContext& ctx);
    void InitializeWindCard(core::GameContext& ctx);
    void InitializeClubSelectList(core::GameContext& ctx);
    void InitializeShotButton(core::GameContext& ctx);
    void InitializeControlHint(core::GameContext& ctx);
    void InitializeShotGaugePanel(core::GameContext& ctx);
    void InitializeJudgeText(core::GameContext& ctx);
    void InitializeMinimapUI(core::GameContext& ctx);

    void UpdateCourseInfoPanel(core::GameContext& ctx, const game::components::GolfGameState& state);
    void UpdateWindUI(core::GameContext& ctx, float windSpeed, const DirectX::XMFLOAT2& windDir, float cameraYaw);
    void UpdateClubSelectList(core::GameContext& ctx, const std::vector<ClubUIData>& clubs, int currentClubIndex);
    void UpdateBottomInfoPanels(core::GameContext& ctx, float distanceToTarget, float heightDiff, 
                                game::components::TerrainMaterial lie, const ClubUIData& currentClub);
    void UpdateGuideUI(core::GameContext& ctx, const game::components::GolfGameState& state);
    void SetEntityAlpha(core::GameContext& ctx, ecs::Entity e, float alpha);

    UIEntities m_ui;

    // クラブリスト構築済みフラグ (クラブ数が変わった場合に再構築)
    int m_builtClubCount = 0;
};

} // namespace game::controllers
