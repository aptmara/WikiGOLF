#pragma once
/**
 * @file TutorialOverlayController.h
 * @brief WikiGolf チュートリアルの進行管理とオーバーレイUIを制御するコントローラー
 */

#include "../../core/GameContext.h"
#include "../../ecs/Entity.h"
#include <DirectXMath.h>
#include <string>
#include <vector>

namespace game::controllers {

class CameraController;
class ClubController;
class ShotController;
class MinimapController;

enum class TutorialStep {
    Camera,
    Club,
    Power,
    Impact,
    TerrainEvent,
    CupIn,
    Done
};

struct TerrainCard {
    std::wstring name;
    std::wstring desc;
    float timer = 0.0f;
    uint32_t bgEntity = UINT32_MAX;
    uint32_t textEntity = UINT32_MAX;
};

class TutorialOverlayController {
public:
    TutorialOverlayController() = default;
    ~TutorialOverlayController() = default;

    // -------------------------------------------------------
    // イベントカメラターゲット（STEP 5 の地形フォーカス用）
    // -------------------------------------------------------
    struct EventCameraTarget {
        DirectX::XMFLOAT3 camPos;   ///< イベントカメラ位置
        DirectX::XMFLOAT3 focusPos; ///< 注視点（地形エリア中心）
        std::wstring name;          ///< 地形名（UI表示用）
        std::wstring desc;          ///< 地形説明（UI表示用）
    };

    void Initialize(core::GameContext& ctx);
    void Update(core::GameContext& ctx, 
                CameraController* cameraCtrl,
                ClubController* clubCtrl,
                ShotController* shotCtrl,
                MinimapController* minimapCtrl);
    void Shutdown(core::GameContext& ctx);

    bool IsDone() const { return m_step == TutorialStep::Done; }

    // -------------------------------------------------------
    // イベントカメラ設定
    // -------------------------------------------------------
    /// @brief STEP 5 のイベントカメラターゲットと使用カメラEntityを設定する。
    /// @details 本体共通チュートリアルでは、地形注視点を安全に抽出できるまで未設定の旧動作を使う。
    void SetEventCameraTargets(ecs::Entity cameraEntity,
                               std::vector<EventCameraTarget> targets);

    /// @brief STEP 5（TerrainEvent）の間は入力をロックする。
    bool IsInputLocked() const { return m_inputLocked; }

    /// @brief イベントカメラを毎フレーム更新する。
    void UpdateEventCamera(core::GameContext& ctx);

private:
    void NextStep(core::GameContext& ctx);
    void UpdateUI(core::GameContext& ctx);
    void CheckCompletion(core::GameContext& ctx, 
                         CameraController* cameraCtrl,
                         ClubController* clubCtrl,
                         ShotController* shotCtrl,
                         MinimapController* minimapCtrl);

    TutorialStep m_step = TutorialStep::Camera;
    
    // UI Entities
    ecs::Entity m_overlayBgEntity   = UINT32_MAX;
    ecs::Entity m_overlayTextEntity = UINT32_MAX;
    ecs::Entity m_skipTextEntity    = UINT32_MAX;

    // State Tracking
    float m_initialCameraYaw  = 0.0f;
    int   m_initialClubIndex  = 0;
    bool  m_terrainEventStarted = false;
    size_t m_terrainCardIndex = 0;
    float  m_terrainCardTimer = 0.0f;
    std::vector<TerrainCard> m_terrainCards;

    // --- イベントカメラ ---
    std::vector<EventCameraTarget> m_eventCamTargets; ///< 設定済みならイベントカメラモード
    ecs::Entity m_cameraEntity       = UINT32_MAX;   ///< 操作対象カメラEntity
    DirectX::XMFLOAT3 m_eventCamFromPos = {};        ///< ラープ開始位置
    float m_eventCamLerpTimer        = 0.0f;         ///< 0→1 でラープ完了
    float m_eventCamDisplayTimer     = 0.0f;         ///< 表示残り秒数
    float m_cupInWaitTimer           = 0.0f;         ///< カップイン後の待機時間
    bool  m_inputLocked              = false;        ///< STEP 5 中のみ true
};

} // namespace game::controllers
