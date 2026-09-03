#pragma once
/**
 * @file WikiGolfHUD.h
 * @brief ゲーム画面のHUD表示要素を管理するクラス
 */

#include "../../core/GameContext.h"
#include "../../ecs/Entity.h"
#include "../components/WikiComponents.h"
#include "../../graphics/TextStyle.h"
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
    float maxPower = 0.0f;
    float baseCarryDistance = 0.0f; ///< 基準キャリー飛距離(ヤード)
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
        ecs::Entity browserCurrentLabelEntity = UINT32_MAX; // "CURRENT" ラベル
        ecs::Entity browserCurrentPageEntity= UINT32_MAX; // 現在地表示（値）
        ecs::Entity browserTargetLabelEntity  = UINT32_MAX; // "TARGET" ラベル
        ecs::Entity browserTargetEntity     = UINT32_MAX; // 目的地表示（値）
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
        ecs::Entity windCardValueEntity     = UINT32_MAX; // 風速値 (数値ウェーブ化により非表示のまま保持)
        ecs::Entity windCardUnitEntity      = UINT32_MAX; // 風向と単位
        std::vector<ecs::Entity> windValueWaveChars; // 風速値を1文字ずつ表示するウェーブ演出用

        // 左側: クラブ選択リスト
        std::vector<ecs::Entity> clubBgEntities;   
        std::vector<ecs::Entity> clubIconEntities; 
        std::vector<ecs::Entity> clubNameEntities; 
        std::vector<ecs::Entity> clubSubNameEntities;
        std::vector<ecs::Entity> clubArrowEntities;
        ecs::Entity clubScrollUpEntity      = UINT32_MAX; // 上にも切替可能ヒント(▲)
        ecs::Entity clubScrollDownEntity    = UINT32_MAX; // 下にも切替可能ヒント(▼)

        // クラブ選択パネル横: 着弾点プレビュー(トップビュー)トグルボタン
        ecs::Entity landingPreviewBtnBgEntity   = UINT32_MAX;
        ecs::Entity landingPreviewBtnTextEntity = UINT32_MAX;

        // 右下: ショットボタン
        ecs::Entity shotButtonBgEntity      = UINT32_MAX; 
        ecs::Entity shotButtonTextEntity    = UINT32_MAX; 

        // 左下: 操作ヘルプ
        ecs::Entity controlHintEntity       = UINT32_MAX;

        /// @brief MinimapController管理外のHUD側ミニマップ装飾です。
        /// @author 山内陽
        std::vector<ecs::Entity> minimapDecorationEntities;

        // 下部中央: 情報パネル
        // 残り距離(目的地が多数あり単一距離表示に意味が薄い)と選択中クラブ
        // (左のクラブ選択リストと重複)は削除し、ライ(地形)情報だけを残す。
        ecs::Entity liePanelBgEntity        = UINT32_MAX; // ライ情報パネル
        ecs::Entity lieLabelEntity          = UINT32_MAX;
        ecs::Entity lieValueEntity          = UINT32_MAX;
        ecs::Entity lieCondValueEntity      = UINT32_MAX;

        // ショット時のみ表示
        ecs::Entity shotPanelBgEntity          = UINT32_MAX;
        ecs::Entity shotPanelStepEntity        = UINT32_MAX;
        ecs::Entity shotPanelTitleEntity       = UINT32_MAX;
        ecs::Entity shotPanelHintEntity        = UINT32_MAX;
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
                game::components::ShotState::Phase shotPhase,
                float currentImpact,
                float currentPower, float confirmedPower,
                float confirmedImpact,
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

    /// @brief 着弾点プレビュー(トップビュー)トグルボタンの見た目を更新します。
    /// @param hovered マウスがボタン上にあるか
    /// @param active トップビュー(マップビュー)が現在有効か
    /// @param enabled ボタンを操作可能な状態か(ショット中などは無効化)
    void UpdateLandingPreviewButton(core::GameContext& ctx, bool hovered,
                                    bool active, bool enabled);

private:
    void InitializeCourseInfoPanel(core::GameContext& ctx);
    void InitializeWindCard(core::GameContext& ctx);
    void InitializeClubSelectList(core::GameContext& ctx);
    void InitializeLandingPreviewButton(core::GameContext& ctx);
    void InitializeShotButton(core::GameContext& ctx);
    void InitializeControlHint(core::GameContext& ctx);
    void InitializeShotGaugePanel(core::GameContext& ctx);
    void InitializeJudgeText(core::GameContext& ctx);
    void InitializeMinimapUI(core::GameContext& ctx);

    void UpdateCourseInfoPanel(core::GameContext& ctx, const game::components::GolfGameState& state);
    void UpdateWindUI(core::GameContext& ctx, float windSpeed, const DirectX::XMFLOAT2& windDir, float cameraYaw);
    void UpdateClubSelectList(core::GameContext& ctx, const std::vector<ClubUIData>& clubs, int currentClubIndex);
    void UpdateBottomInfoPanels(core::GameContext& ctx, game::components::TerrainMaterial lie);
    void UpdateGuideUI(core::GameContext& ctx, const game::components::GolfGameState& state);
    void UpdateShotPhasePanel(core::GameContext& ctx,
                              game::components::ShotState::Phase shotPhase,
                              float currentPower,
                              float confirmedPower,
                              float currentImpact,
                              float confirmedImpact,
                              const ClubUIData& currentClub);
    void SetEntityAlpha(core::GameContext& ctx, ecs::Entity e, float alpha);

    /// @brief 数値文字列を1文字ずつ独立したエンティティに割り当て、右から
    ///        位相をずらして上下に揺らす「ウェーブ」演出で描画する。
    void UpdateWaveNumberText(core::GameContext& ctx,
                              std::vector<ecs::Entity>& chars,
                              const std::wstring& text,
                              float baseX, float baseY,
                              const graphics::TextStyle& baseStyle,
                              int layer);

    UIEntities m_ui;

    // クラブリスト構築済みフラグ (クラブ数が変わった場合に再構築)
    int m_builtClubCount = 0;

    // UpdateClubSelectList が計算した「前後3行の窓」に今その行が入って
    // いるかどうか。SetShotPhaseUIVisible がショット終了時に即座に正しい
    // 行だけを復元できるよう、ここに結果をキャッシュしておく（そうしない
    // と次に UpdateClubSelectList が呼ばれる=最大0.1秒後まで、ショット中
    // 非表示にした行が復元されず、選択中の1行だけが残って見える）。
    std::vector<bool> m_clubRowWindowVisible;

    float m_elapsedTime = 0.0f;
    float m_phaseTransition = 1.0f;
    game::components::ShotState::Phase m_previousShotPhase =
        game::components::ShotState::Phase::Idle;

    // インパクト確定後、判定色つきで保持表示 → フェードアウトするまでの残り時間（秒）
    // kGaugeHoldDuration + kGaugeFadeDuration からカウントダウンする。
    float m_gaugeDismissRemaining = 0.0f;
};

} // namespace game::controllers
