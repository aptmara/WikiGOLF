#pragma once
/**
 * @file WikiGolfHUD.h
 * @brief ゲーム画面のHUD表示要素を管理するクラス
*/

#include "../../core/GameContext.h"
#include "../components/WikiComponents.h"
#include "../../graphics/TextStyle.h"
#include "hud/CourseInfoPanel.h"
#include "hud/ClubSelectionPanel.h"
#include "hud/WindPanel.h"
#include "hud/MinimapDecorationPanel.h"
#include "hud/ShotGaugePanel.h"
#include <DirectXMath.h>
#include <vector>
#include <string>

namespace game::controllers {

class WikiGolfHUD {
public:
    /**
     * @brief HUD用の初期化処理を行います。
*/
    void Initialize(core::GameContext& ctx);

    /**
     * @brief HUDが生成したすべてのEntityを破棄し、内部状態を初期化します。
     * @param ctx ゲーム全体の共有コンテキストです。
*/
    void Shutdown(core::GameContext& ctx);

    /**
     * @brief 毎フレーム更新
     * @param clubs クラブ表示情報のリスト（表示順）
     * @param currentClubIndex 現在選択中のインデックス
     * @param impactCenter インパクトゲージの「パーフェクト」中心位置 (0.0〜1.0)
     * @param aimPin 設置中のエイムピン状態（nullptrまたはactive=falseなら非表示）
*/
    void Update(core::GameContext& ctx, float dt,
                const game::components::GolfGameState& state,
                game::components::ShotState::Phase shotPhase,
                float currentImpact,
                float currentPower, float confirmedPower,
                float confirmedImpact, float impactCenter,
                float windSpeed, const DirectX::XMFLOAT2& windDir, float cameraYaw,
                const std::vector<ClubUIData>& clubs,
                int currentClubIndex,
                float distanceToTarget, float heightDiff,
                const game::components::AimPinState* aimPin);

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

    /** @brief 通常時 (Idle) <-> ショット時の UI 切り替え*/
    void SetShotPhaseUIVisible(core::GameContext& ctx, bool shotPhase);

    /**
     * @brief HUD全体の表示/非表示を切り替える（ロード中は非表示にするため）
     * @param visible true=表示, false=非表示
*/
    void SetVisible(core::GameContext& ctx, bool visible);

    /** @brief チュートリアル中に、その時点で関係する通常HUDだけを表示します。*/
    void SetTutorialPresentation(core::GameContext& ctx, bool enabled,
                                 bool showCourseInfo,
                                 bool showClubSelection,
                                 bool showMinimapDecoration);

    /** @brief 通常HUDの現在のフェード透明度を返します。*/
    float GetNormalHudOpacity() const {
        return m_isVisible ? m_normalHudOpacity : 0.0f;
    }

private:
    void UpdateNormalHudTransition(
        core::GameContext& ctx, float dt,
        game::components::ShotState::Phase shotPhase);
    void ApplyNormalHudOpacity(core::GameContext& ctx);

    hud::CourseInfoPanel m_courseInfoPanel;
    hud::ClubSelectionPanel m_clubSelectionPanel;
    hud::MinimapDecorationPanel m_minimapDecorationPanel;
    hud::ShotGaugePanel m_shotGaugePanel;
    hud::WindPanel m_windPanel;

    float m_elapsedTime = 0.0f;
    float m_normalHudOpacity = 1.0f;
    bool m_shotSequenceActive = false;
    bool m_isVisible = true;
    bool m_tutorialPresentation = false;
    bool m_tutorialShowCourseInfo = false;
    bool m_tutorialShowClubSelection = false;
    bool m_tutorialShowMinimapDecoration = false;
};

} // namespace game::controllers
