/**
 * @file TutorialOverlayControllerUI.cpp
 * @brief チュートリアルの案内パネルへ、現在の段階の内容を渡します。
 */

#include "TutorialOverlayController.h"
#include "../components/WikiComponents.h"
#include "../utils/TutorialGuideRules.h"
#include "../../audio/AudioSystem.h"
#include "../../ecs/World.h"

namespace game::controllers {

void TutorialOverlayController::UpdateUI(core::GameContext& ctx) {
    game::utils::TutorialGuideState state;
    state.step = m_step;
    state.cardIndex = m_terrainCardIndex;
    state.terrainCardCount = m_eventCamTargets.size();
    state.flagCardCount = m_flagEventCamTargets.size();
    const auto& targets = GetActiveEventCameraTargets();
    if (m_terrainCardIndex < targets.size()) {
        state.cardName = targets[m_terrainCardIndex].name;
        state.cardDescription = targets[m_terrainCardIndex].desc;
    }
    if (const auto* golf = ctx.world.GetGlobal<components::GolfGameState>()) {
        state.goalCleared = golf->gameCleared;
    }
    state.wrongCupName = m_wrongCupName;

    const auto content = game::utils::BuildTutorialGuideContent(state);
    m_spotlight = content.spotlight;
    m_panel.SetContent(ctx, content);
}

const std::vector<TutorialOverlayController::EventCameraTarget>&
TutorialOverlayController::GetActiveEventCameraTargets() const {
    static const std::vector<EventCameraTarget> empty;
    if (m_step == TutorialStep::TerrainInfo) return m_eventCamTargets;
    if (m_step == TutorialStep::FlagInfo) return m_flagEventCamTargets;
    return empty;
}

void TutorialOverlayController::TriggerStepClear(core::GameContext& ctx,
                                                 bool achieved) {
    if (m_stepClearPending) return;
    m_stepClearPending = true;
    m_panel.SetSpotlight(ctx, game::utils::TutorialSpotlight::None);
    if (!achieved) {
        // Enterで送ったときは待たせない。
        m_stepClearTimer = hud::TutorialGuidePanel::kStepClearSeconds;
        return;
    }
    m_stepClearTimer = 0.0f;
    m_panel.PlayStepClear(ctx);
    // 打球音と聞き分けられるよう、高めの音程で短く鳴らす。
    if (ctx.audio) ctx.audio->PlaySE(ctx, "se_shot_soft.mp3", 0.45f, 0.6f);
}

} // namespace game::controllers
