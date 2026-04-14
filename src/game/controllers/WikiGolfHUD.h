#pragma once
/**
 * @file WikiGolfHUD.h
 * @brief HUD (Head-Up Display) Controller for WikiGolf
 *
 * 通常時画面で必要な以下のUI要素を管理する:
 *   - 現在地/目的地情報パネル (左上)
 *   - クラブ選択リスト (左側・常時表示)
 *   - 風情報カード (上中央)
 *   - ショットボタン (右下)
 *   - 操作ヘルプバー (左下)
 *   - 着地点マーカー (3D空間)
 * ショット時 (ShotPhase) にはゲージUIを追加表示する。
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
        // === 左上: ブラウザ風コース情報 ===
        ecs::Entity browserBgEntity         = UINT32_MAX; // 背景パネル
        ecs::Entity browserTabIconEntity    = UINT32_MAX; // "WEB" バッジ
        ecs::Entity browserCurrentPageEntity= UINT32_MAX; // "名川忍ゴルフコース"
        ecs::Entity browserTargetEntity     = UINT32_MAX; // "-> ナタリー (ニュースサイト)"
        ecs::Entity browserShotInfoEntity   = UINT32_MAX; // "Shots: 0 / Par 2"
        ecs::Entity browserHistoryEntity    = UINT32_MAX;
        
        ecs::Entity clubHeaderEntity        = UINT32_MAX; // "CLUB" ヘッダー

        // 互換用エイリアス
        ecs::Entity browserTabIconEntityAlias = UINT32_MAX;
        ecs::Entity headerEntity            = UINT32_MAX;
        ecs::Entity shotCountEntity         = UINT32_MAX;
        ecs::Entity infoEntity              = UINT32_MAX;
        ecs::Entity pathEntity              = UINT32_MAX;

        // === 上中央: 風情報カード ===
        ecs::Entity windEntity              = UINT32_MAX;
        ecs::Entity windArrowEntity         = UINT32_MAX;
        ecs::Entity windCardLabelEntity     = UINT32_MAX; // "WIND"
        ecs::Entity windCardValueEntity     = UINT32_MAX; // "3.1"
        ecs::Entity windCardUnitEntity      = UINT32_MAX; // "↗ m/s"

        // === 左側: クラブ選択リスト ===
        // クラブ行ごとに背景+テキストエンティティを持つ
        std::vector<ecs::Entity> clubBgEntities;   // 背景 (UIText with bgColor)
        std::vector<ecs::Entity> clubIconEntities; // アイコン
        std::vector<ecs::Entity> clubNameEntities; // クラブ名テキスト
        std::vector<ecs::Entity> clubSubNameEntities; // 英語種別テキスト (例: "1W Driver")
        std::vector<ecs::Entity> clubArrowEntities;// 選択中の「▶」

        // === 右下: ショットボタン ===
        ecs::Entity shotButtonBgEntity      = UINT32_MAX; // ボタン背景
        ecs::Entity shotButtonTextEntity    = UINT32_MAX; // "SHOT" テキスト

        // === 左下: 操作ヘルプ ===
        ecs::Entity controlHintEntity       = UINT32_MAX;

        // === 下部中央: 情報パネル (⑧距離, ⑨クラブ, ⑪ライ) ===
        // ⑧ 距離パネル
        ecs::Entity distPanelBgEntity       = UINT32_MAX;
        ecs::Entity distLabelEntity         = UINT32_MAX;
        ecs::Entity distValueEntity         = UINT32_MAX;
        ecs::Entity heightLabelEntity       = UINT32_MAX;
        ecs::Entity heightValueEntity       = UINT32_MAX;
        
        // ⑨ 選択中クラブパネル
        ecs::Entity clubInfoPanelBgEntity   = UINT32_MAX;
        ecs::Entity clubInfoLabelEntity     = UINT32_MAX;
        ecs::Entity clubInfoNameEntity      = UINT32_MAX;
        ecs::Entity clubInfoIconEntity      = UINT32_MAX;
        ecs::Entity clubInfoShortNameEntity = UINT32_MAX;
        
        // ⑪ ライ情報パネル
        ecs::Entity liePanelBgEntity        = UINT32_MAX;
        ecs::Entity lieLabelEntity          = UINT32_MAX;
        ecs::Entity lieValueEntity          = UINT32_MAX;
        ecs::Entity lieCondLabelEntity      = UINT32_MAX;
        ecs::Entity lieCondValueEntity      = UINT32_MAX;

        // === ショット時のみ表示 ===
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

    void UpdatePowerGauge(core::GameContext& ctx, float fillValue, float markerValue, float minPower, float maxPower);
    void UpdateJudge(core::GameContext& ctx, const std::wstring& text, const DirectX::XMFLOAT4& color);
    void ResetShotUI(core::GameContext& ctx);
    void SetGaugeVisible(core::GameContext& ctx, bool visible);
    void SetImpactZonesVisible(core::GameContext& ctx, bool visible);

    /// @brief 通常時 (Idle) <-> ショット時の UI 切り替え
    void SetShotPhaseUIVisible(core::GameContext& ctx, bool shotPhase);

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
