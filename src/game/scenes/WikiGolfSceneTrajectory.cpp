/**
 * @file WikiGolfSceneTrajectory.cpp
 * @brief ショット予測表示（予測線・着地点・向き）と傾斜可視化の更新処理を実装します。
*/

#include "WikiGolfScene.h"
#include "../../core/GameContext.h"
#include "../../core/Profiler.h"
#include "../../ecs/World.h"
#include "../components/PhysicsComponents.h"
#include "../components/Transform.h"
#include "../components/WikiComponents.h"
#include "../controllers/ClubController.h"
#include "../controllers/shotguide/ShotGuideController.h"
#include "../systems/WikiTerrainSystem.h"
#include "../utils/CarryDistanceTable.h"
#include "../utils/GameplayPhysicsConstants.h"
#include "../utils/ShotGaugeRules.h"
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>

namespace game::scenes {

using namespace DirectX;
using namespace game::components;

namespace {

/** @brief エイムピンが無い待機中に、予測線で見せるパワー（フルスイングの届く先）*/
constexpr float kIdlePreviewPower = 1.0f;
/**
 * @brief 「止まりそうな範囲」に含めるインパクトの出来。
 * @details Nice までのぶれ（左右と、短くなる距離）を範囲にする。Miss は
 *          大きく外れる扱いなので範囲には入れない。
*/
constexpr auto kZoneJudgement = game::components::ShotJudgement::Nice;

} // namespace

void WikiGolfScene::UpdateTrajectoryAndGuide(
    core::GameContext &ctx, GolfGameState &state, ShotState &shot, float dt,
    bool tutorialInputLocked, bool isMapView) {
    // ショット予測表示。シーンの役目は「どこから・どの向きへ・どの強さで」を
    // 伝えるところまでで、道すじの計算と描画は ShotGuideController が受け持つ。
    // 全体マップは実カメラの俯瞰なので、同じ表示がそのまま上から見える。
    if (m_shotGuide) {
        PROFILE_SCOPE("WikiGolf.Trajectory");
        using Phase = game::components::ShotState::Phase;
        game::guide::ShotGuideInput input;

        auto *ballT = ctx.world.Get<Transform>(m_ballEntity);
        auto *ballRB = ctx.world.Get<RigidBody>(m_ballEntity);
        bool ballAtRest = true;
        if (ballRB) {
            const float speedSq = ballRB->velocity.x * ballRB->velocity.x +
                                  ballRB->velocity.y * ballRB->velocity.y +
                                  ballRB->velocity.z * ballRB->velocity.z;
            ballAtRest = speedSq <= 0.1f * 0.1f;
        }
        const bool aiming = shot.phase == Phase::Idle ||
                            shot.phase == Phase::PowerCharging ||
                            shot.phase == Phase::ImpactTiming;
        input.visible = ballT && m_clubController && state.canShoot &&
                        !tutorialInputLocked && aiming && ballAtRest;

        if (input.visible) {
            const auto &club = m_clubController->GetCurrentClub();

            // 見せるパワー: ゲージ中はその値、待機中はエイムピンへ届く強さ、
            // ピンが無ければフルスイング。
            float powerRatio = kIdlePreviewPower;
            if (shot.phase == Phase::PowerCharging) {
                powerRatio = shot.powerGaugePos;
            } else if (shot.phase == Phase::ImpactTiming) {
                powerRatio = shot.confirmedPower;
            } else if (const auto *pin = ctx.world.GetGlobal<AimPinState>();
                       pin && pin->active && pin->reachable &&
                       pin->requiredPowerRatio > 0.01f) {
                powerRatio = pin->requiredPowerRatio;
            }
            powerRatio = std::clamp(powerRatio, 0.0f, 1.0f);

            auto &request = input.request;
            request.start = ballT->position;
            request.direction = m_cameraController
                                    ? m_cameraController->GetShotDirection()
                                    : XMFLOAT3{0.0f, 0.0f, 1.0f};
            request.launchAngleDeg = club.launchAngle;
            const float targetDistance = club.baseCarryDistance * powerRatio;
            request.speed = game::utils::LookupSpeedForDistance(club.carryTable,
                                                                targetDistance);

            if (ballRB) {
                request.ball.drag = ballRB->drag;
                request.ball.restitution = ballRB->restitution;
                request.ball.mass = ballRB->mass;
            }
            if (const auto *collider = ctx.world.Get<Collider>(m_ballEntity)) {
                request.ball.ballRadius = collider->radius;
            }
            request.ball.rollingFrictionScale = state.rollingFrictionScale;
            if (!std::isfinite(request.ball.rollingFrictionScale) ||
                request.ball.rollingFrictionScale < 0.05f) {
                request.ball.rollingFrictionScale = 1.0f;
            }
            request.wind.windSpeed = state.windSpeed;
            request.wind.windDirection = state.windDirection;

            input.terrain = m_terrainSystem.get();
            input.energy = powerRatio;
            input.charging = shot.phase != Phase::Idle;
            // ぶれの大きさは、実際のショットに使う判定の規則からそのまま取る
            input.spread.side =
                game::utils::GetJudgementSideOffset(kZoneJudgement);
            input.spread.shortBy =
                1.0f -
                game::utils::GetJudgementDistanceMultiplier(kZoneJudgement);
            // 壁や障害物は、狙い始めるたびに集め直す（ページごとに変わる）
            if (!m_shotGuide->IsVisible()) {
                m_shotGuideObstacles.Collect(ctx.world);
            }
            input.obstacles = &m_shotGuideObstacles;
            input.obstacleRevision = m_shotGuideObstacles.Revision();
            // クラブが替わったら、線を手前から描き直す
            input.contextKey = static_cast<int>(club.launchAngle * 10.0f) * 4096 +
                               static_cast<int>(club.maxPower * 10.0f);
        }
        m_shotGuide->Update(ctx, input);
    }

    // 傾斜可視化（パター保持中、待機中〜ボールが止まるまでグリーンの高低差オーバーレイを表示）
    {
      using Phase = game::components::ShotState::Phase;
      const bool isPutter = m_clubController &&
          m_clubController->GetCurrentClub().categoryEN == "Putter";
      // Idle/PowerCharging/ImpactTiming: ショット前の待機〜パワー・インパクトゲージ操作中
      // Executing: ショット後、ボールが転がって停止するまでの間
      const bool showSlope = isPutter && !tutorialInputLocked && !isMapView &&
                             (shot.phase == Phase::Idle ||
                              shot.phase == Phase::PowerCharging ||
                              shot.phase == Phase::ImpactTiming ||
                              shot.phase == Phase::Executing);

      DirectX::XMFLOAT3 ballPos{0.0f, 0.0f, 0.0f};
      if (auto* ballT3 = ctx.world.Get<game::components::Transform>(m_ballEntity)) {
        ballPos = ballT3->position;
      }
      m_slopeVisualization.Update(ctx, dt, showSlope, ballPos, m_terrainSystem.get());
    }
}

} // namespace game::scenes
