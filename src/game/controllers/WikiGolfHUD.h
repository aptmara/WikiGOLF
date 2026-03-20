#pragma once
/**
 * @file WikiGolfHUD.h
 * @brief HUD (Head-Up Display) Controller for WikiGolf
 */

#include "../../core/GameContext.h"
#include "../../ecs/Entity.h"
#include "../components/WikiComponents.h"
#include <DirectXMath.h>

namespace game::controllers {

class WikiGolfHUD {
public:
    struct UIEntities {
        ecs::Entity browserCurrentPageEntity = UINT32_MAX;
        ecs::Entity browserTargetEntity = UINT32_MAX;
        ecs::Entity browserShotInfoEntity = UINT32_MAX;
        ecs::Entity browserHistoryEntity = UINT32_MAX;
        ecs::Entity browserTabIconEntity = UINT32_MAX;

        ecs::Entity windCardLabelEntity = UINT32_MAX;
        ecs::Entity windCardValueEntity = UINT32_MAX;
        ecs::Entity windCardUnitEntity = UINT32_MAX;

        ecs::Entity shotPanelPowerLabelEntity = UINT32_MAX;
        ecs::Entity shotPanelPowerValueEntity = UINT32_MAX;
        ecs::Entity shotPanelAccuracyLabelEntity = UINT32_MAX;
        ecs::Entity shotPanelAccuracyValueEntity = UINT32_MAX;
        ecs::Entity shotPanelClubLabelEntity = UINT32_MAX;

        ecs::Entity headerEntity = UINT32_MAX;
        ecs::Entity shotCountEntity = UINT32_MAX;
        ecs::Entity infoEntity = UINT32_MAX;
        ecs::Entity windEntity = UINT32_MAX;
        ecs::Entity windArrowEntity = UINT32_MAX;
        
        ecs::Entity gaugeBarEntity = UINT32_MAX;
        ecs::Entity gaugeFillEntity = UINT32_MAX;
        ecs::Entity gaugeMarkerEntity = UINT32_MAX;
        
        ecs::Entity pathEntity = UINT32_MAX;
        ecs::Entity judgeEntity = UINT32_MAX;
    };

    void Initialize(core::GameContext& ctx);

    void Update(core::GameContext& ctx, float dt, const game::components::GolfGameState& state, 
                float currentPower, float confirmedPower, 
                float windSpeed, const DirectX::XMFLOAT2& windDir, float cameraYaw,
                const std::string& clubName);
    
    void UpdatePowerGauge(core::GameContext& ctx, float power, float minPower, float maxPower);
    void UpdateJudge(core::GameContext& ctx, const std::wstring& text, const DirectX::XMFLOAT4& color);
    
private:
    void UpdateGuideUI(core::GameContext& ctx, const game::components::GolfGameState& state);
    void UpdateWindUI(core::GameContext& ctx, float windSpeed, const DirectX::XMFLOAT2& windDir, float cameraYaw);
    
    UIEntities m_ui;
};

} // namespace game::controllers
