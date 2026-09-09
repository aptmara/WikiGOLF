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
#include "hud/LiePanel.h"
#include "hud/ShotGaugePanel.h"
#include "hud/GameplayControlsPanel.h"
#include "hud/AimDistancePanel.h"
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

    /**
     * @brief 着弾点プレビュー(トップビュー)トグルボタンの見た目を更新します。
     * @param hovered マウスがボタン上にあるか
     * @param active トップビュー(マップビュー)が現在有効か
     * @param enabled ボタンを操作可能な状態か(ショット中などは無効化)
*/
    void UpdateLandingPreviewButton(core::GameContext& ctx, bool hovered,
                                    bool active, bool enabled);

private:
    hud::CourseInfoPanel m_courseInfoPanel;
    hud::ClubSelectionPanel m_clubSelectionPanel;
    hud::GameplayControlsPanel m_gameplayControlsPanel;
    hud::LiePanel m_liePanel;
    hud::MinimapDecorationPanel m_minimapDecorationPanel;
    hud::ShotGaugePanel m_shotGaugePanel;
    hud::WindPanel m_windPanel;
    hud::AimDistancePanel m_aimDistancePanel;

    float m_elapsedTime = 0.0f;
};

} // namespace game::controllers
