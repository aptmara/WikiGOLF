#pragma once
/**
 * @file TutorialOverlayController.h
 * @brief WikiGolf チュートリアルの進行管理とオーバーレイUIを制御するコントローラー
 */

#include "../../core/GameContext.h"
#include "../../ecs/Entity.h"
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

    void Initialize(core::GameContext& ctx);
    void Update(core::GameContext& ctx, 
                CameraController* cameraCtrl,
                ClubController* clubCtrl,
                ShotController* shotCtrl,
                MinimapController* minimapCtrl);
    void Shutdown(core::GameContext& ctx);

    bool IsDone() const { return m_step == TutorialStep::Done; }

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
    ecs::Entity m_overlayBgEntity = UINT32_MAX;
    ecs::Entity m_overlayTextEntity = UINT32_MAX;
    ecs::Entity m_skipTextEntity = UINT32_MAX;

    // State Tracking
    float m_initialCameraYaw = 0.0f;
    int m_initialClubIndex = 0;
    bool m_terrainEventStarted = false;
    size_t m_terrainCardIndex = 0;
    float m_terrainCardTimer = 0.0f;
    std::vector<TerrainCard> m_terrainCards;
};

} // namespace game::controllers
