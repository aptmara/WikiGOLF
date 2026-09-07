/**
 * @file WikiGolfSceneHudEffects.cpp
 * @brief HUDとショット演出の更新処理を実装します。
*/

#include "WikiGolfScene.h"
#include "../../core/GameContext.h"
#include "../../core/Profiler.h"
#include "../../ecs/World.h"
#include "../components/Transform.h"
#include "../components/WikiComponents.h"
#include "../controllers/CameraController.h"
#include "../controllers/ClubController.h"
#include "../controllers/WikiGolfHUD.h"
#include "../systems/GameJuiceSystem.h"
#include "../systems/WikiTerrainSystem.h"
#include <cmath>
#include <vector>

namespace game::scenes {

using namespace game::components;

void WikiGolfScene::UpdateHudAndEffects(
    core::GameContext &ctx, GolfGameState &state, ShotState &shot, float dt) {
    // HUD 更新
    if (m_hud) {
        PROFILE_SCOPE("WikiGolf.HUD");
        float currentPower = shot.powerGaugePos;
        if (shot.phase == game::components::ShotState::Phase::ImpactTiming || shot.confirmedPower > 0.0f) {
            currentPower = shot.confirmedPower;
        }
        float currentImpact = 0.5f;
        if (shot.phase == game::components::ShotState::Phase::ImpactTiming) {
            currentImpact = shot.impactGaugePos;
        }
        const bool isShotPhase = (shot.phase != game::components::ShotState::Phase::Idle &&
                                  shot.phase != game::components::ShotState::Phase::ShowResult &&
                                  shot.phase != game::components::ShotState::Phase::RestoringCamera);
        m_hudUpdateTimer += dt;
        const bool shouldRefreshHud = isShotPhase || m_hudUpdateTimer >= 0.1f;

        if (shouldRefreshHud) {
            // 間引き中に経過した実時間を渡す。ここで単フレームの dt を渡すと、
            // 間引き期間(約0.1秒)ぶん進んだはずのアイドルアニメーション用の
            // 時計(m_elapsedTime)が単フレーム分しか進まず、クラブの矢印や
            // 選択行の揺れ等がほぼ静止して見える不具合になっていた。
          float hudDt = m_hudUpdateTimer;
          if (isShotPhase) {
              hudDt = dt;
          }
            m_hudUpdateTimer = 0.0f;

            // クラブ情報リストを構築して渡す
            std::vector<game::controllers::ClubUIData> clubDataList;
            int clubIdx = 0;
            if (m_clubController) {
                const auto& clubs = m_clubController->GetAllClubs();
                for (const auto& c : clubs) {
                    clubDataList.push_back({c.name, c.iconTexture, c.shortName, c.categoryEN, c.maxPower, c.baseCarryDistance});
                }
                clubIdx = m_clubController->GetCurrentClubIndex();
            }

            // ターゲット距離と高低差を計算
            float distanceToTarget = 0.0f;
            float heightDiff = 0.0f;
          if (!state.holes.empty()) {
                auto targetHoleEntity = state.holes[0];
                auto* holeT = ctx.world.Get<game::components::Transform>(targetHoleEntity);
                auto* ballT = ctx.world.Get<game::components::Transform>(m_ballEntity);
                if (holeT && ballT) {
                    float dx = holeT->position.x - ballT->position.x;
                    float dz = holeT->position.z - ballT->position.z;
                    distanceToTarget = std::sqrt(dx * dx + dz * dz);
                    heightDiff = holeT->position.y - ballT->position.y;
              }
          }

          float cameraYaw = 0.0f;
          if (m_cameraController) {
              cameraYaw = m_cameraController->GetYaw();
          }
          m_hud->Update(ctx, hudDt, state,
                          shot.phase, currentImpact,
                          currentPower, shot.confirmedPower,
                          shot.confirmedImpact,
                          state.windSpeed, state.windDirection,
                          cameraYaw,
                          clubDataList, clubIdx,
                          distanceToTarget, heightDiff);
        }

        // HUDへのパワーゲージ更新
        if (shot.phase == game::components::ShotState::Phase::PowerCharging ||
            shot.phase == game::components::ShotState::Phase::ImpactTiming) {
            m_hud->UpdatePowerGauge(ctx, currentPower, currentImpact, 0.0f, 1.0f);
        }

        // 通常時 <-> ショット時 UI 切り替え
        m_hud->SetShotPhaseUIVisible(ctx, isShotPhase);
    }

    if (m_gameJuice) {
        PROFILE_SCOPE("WikiGolf.GameJuice");
        m_gameJuice->Update(ctx, m_cameraEntity, m_ballEntity);
    }
    if (m_terrainSystem) {
        PROFILE_SCOPE("WikiGolf.SurfaceResponse");
        m_terrainSystem->UpdateSurfaceResponse(ctx, m_ballEntity, dt);
    }
    {
      PROFILE_SCOPE("WikiGolf.ProceduralFlags");
      UpdateProceduralFlagEffects(ctx, dt);
    }
}

} // namespace game::scenes
