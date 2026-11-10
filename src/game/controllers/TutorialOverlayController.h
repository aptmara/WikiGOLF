#pragma once
/**
 * @file TutorialOverlayController.h
 * @brief WikiGolf チュートリアルの進行管理とオーバーレイUIを制御するコントローラー
*/

#include "../../core/GameContext.h"
#include "../../ecs/Entity.h"
#include "MinimapController.h"
#include "TutorialStep.h"
#include "hud/TutorialGuidePanel.h"
#include <DirectXMath.h>
#include <string>
#include <vector>

namespace game::controllers {

class CameraController;
class ClubController;
class ShotController;

struct TutorialInputPolicy {
    bool camera = false;
    bool aimPin = false;
    bool club = false;
    bool shot = false;
    bool map = false;
};

struct TutorialPresentationPolicy {
    bool courseInfo = false;
    bool club = false;
    bool minimap = false;
};

class TutorialOverlayController {
public:
    TutorialOverlayController() = default;
    ~TutorialOverlayController() = default;

    // -------------------------------------------------------
    // イベントカメラターゲット（地形・旗フォーカス用）
    // -------------------------------------------------------
    struct EventCameraTarget {
        DirectX::XMFLOAT3 camPos;   ///< イベントカメラ位置
        DirectX::XMFLOAT3 focusPos; ///< 注視点（説明対象の中心）
        std::wstring name;          ///< 説明対象名（UI表示用）
        std::wstring desc;          ///< 説明文（UI表示用）
    };

    void Initialize(core::GameContext& ctx);
    void Update(core::GameContext& ctx,
                CameraController* cameraCtrl,
                ClubController* clubCtrl,
                ShotController* shotCtrl,
                MinimapController* minimapCtrl,
                ecs::Entity skyboxEntity);
    void Shutdown(core::GameContext& ctx);

    bool IsDone() const { return m_step == TutorialStep::Done; }
    void SetVisible(core::GameContext& ctx, bool visible);
    TutorialStep GetStep() const { return m_step; }
    /** @brief 案内パネルの上で押したマウス操作を、このフレームはパネルが使ったか*/
    bool IsPointerOnGuide() const { return m_panel.IsCapturingPointer(); }
    TutorialInputPolicy GetInputPolicy() const;
    TutorialPresentationPolicy GetPresentationPolicy() const;
    game::controllers::MinimapController::InputPermissions GetMapInputPermissions() const;
    bool CanAcceptCupIn(const std::string& linkTarget, bool isTarget) const;
    void NotifyLinkCupIn(core::GameContext& ctx);
    /** @brief 違う旗に入って最初の場所へ戻したことを、理由とともに案内へ出す。*/
    void NotifyWrongCupIn(core::GameContext& ctx, const std::wstring& name);

    // -------------------------------------------------------
    // イベントカメラ設定
    // -------------------------------------------------------
    /**
     * @brief STEP 5/6 のイベントカメラターゲットと使用カメラEntityを設定する。
     * @details 本体共通チュートリアルでは、地形注視点を安全に抽出できるまで未設定の旧動作を使う。
*/
    void SetEventCameraTargets(ecs::Entity cameraEntity,
                               std::vector<EventCameraTarget> terrainTargets,
                               std::vector<EventCameraTarget> flagTargets = {});

    /** @brief イベントカメラ説明中は入力をロックする。*/
    bool IsInputLocked() const;

    /** @brief イベントカメラを毎フレーム更新する。*/
    void UpdateEventCamera(core::GameContext& ctx);

private:
    void NextStep(core::GameContext& ctx);
    void UpdateUI(core::GameContext& ctx);
    void CheckCompletion(core::GameContext& ctx,
                         CameraController* cameraCtrl,
                         ClubController* clubCtrl,
                         ShotController* shotCtrl,
                         MinimapController* minimapCtrl);
    /** @brief 現在のイベントカメラ説明で使うターゲット一覧を返します。*/
    const std::vector<EventCameraTarget>& GetActiveEventCameraTargets() const;
    /**
     * @brief 少し待ってから次の段階へ進める
     * @param achieved 操作をやり遂げたか。Enterで送ったときはfalseにし、合図を出さない
    */
    void TriggerStepClear(core::GameContext& ctx, bool achieved = true);
    /** @brief 枠で示すHUD部品を、いまの操作状況に合わせて切り替える*/
    void UpdateSpotlight(core::GameContext& ctx,
                         MinimapController* minimapCtrl);

    TutorialStep m_step = TutorialStep::Intro;

    hud::TutorialGuidePanel m_panel;
    game::utils::TutorialSpotlight m_spotlight =
        game::utils::TutorialSpotlight::None;

    // State Tracking
    float m_initialCameraYaw  = 0.0f;
    float m_initialCameraDistance = 0.0f;
    int   m_initialClubIndex  = 0;
    bool  m_terrainEventStarted = false;
    size_t m_terrainCardIndex = 0;

    // --- イベントカメラ ---
    std::vector<EventCameraTarget> m_eventCamTargets;     ///< 地形説明用イベントカメラターゲット
    std::vector<EventCameraTarget> m_flagEventCamTargets; ///< 旗説明用イベントカメラターゲット
    ecs::Entity m_cameraEntity       = UINT32_MAX;   ///< 操作対象カメラEntity
    DirectX::XMFLOAT3 m_eventCamFromPos = {};        ///< ラープ開始位置
    float m_eventCamLerpTimer        = 0.0f;         ///< 0→1 でラープ完了
    float m_eventCamDisplayTimer     = 0.0f;         ///< 表示残り秒数
    float m_cupInWaitTimer           = 0.0f;         ///< カップイン後の待機時間
    bool  m_practiceShotLaunched     = false;        ///< 初めての1打が飛び出したか
    std::wstring m_wrongCupName;                     ///< 直前に入った違う旗の名前
    bool  m_inputLocked              = false;        ///< STEP 5 中のみ true
    bool  m_visible                  = true;
    MinimapController::InputActivity m_mapActivityAtStep{};

    // --- ステップクリア演出 ---
    float       m_stepClearTimer   = 0.0f;       ///< 合図を出してからの経過時間
    bool        m_stepClearPending = false;      ///< NextStep 直前の合図の待機中
};

} // namespace game::controllers
