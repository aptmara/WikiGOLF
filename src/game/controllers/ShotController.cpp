#include "ShotController.h"
#include "../../core/Input.h"
#include "../../ecs/World.h"
#include "../../audio/AudioSystem.h"
#include "../components/PhysicsComponents.h"
#include "../components/WikiComponents.h"
#include "../components/Transform.h"
#include "../systems/TimeOfDaySystem.h"
#include "../utils/CarryDistanceTable.h"
#include "../utils/JudgeFeedback.h"
#include "../utils/ShotGaugeRules.h"
#include "../utils/UIConstants.h"
#include "WikiGolfHUD.h"
#include <cmath>

namespace game::controllers {

namespace {

void AdvanceGauge(float& value, float& direction, float speed, float dt) {
    value += direction * speed * dt;
    if (value >= 1.0f) {
        value = 1.0f;
        direction = -1.0f;
    } else if (value <= 0.0f) {
        value = 0.0f;
        direction = 1.0f;
    }
}

} // namespace

ShotController::ShotEvent ShotController::ProcessShot(core::GameContext& ctx, bool canAim, WikiGolfHUD* hud, ClubController* clubCtrl) {
    ShotEvent eventOut;
    auto* state = ctx.world.GetGlobal<game::components::GolfGameState>();
    auto* shot = ctx.world.GetGlobal<game::components::ShotState>();
    if (!state || !shot) return eventOut;

    const float dt = ctx.dt;

    switch (shot->phase) {
    case game::components::ShotState::Phase::Idle: {
        if (canAim && ctx.input.GetMouseButtonDown(0)) {
            // パワーチャージ開始
            shot->phase = game::components::ShotState::Phase::PowerCharging;
            shot->powerGaugePos = 0.0f;
            shot->powerGaugeDir = 1.0f;
            
            if (ctx.audio) {
                ctx.audio->PlaySE(ctx, "se_shot_charge.mp3", 0.7f);
            }
        }
        break;
    }
    
    case game::components::ShotState::Phase::PowerCharging: {
        // パワー決定
        if (ctx.input.GetMouseButtonDown(0)) {
            shot->confirmedPower = shot->powerGaugePos;
            shot->phase = game::components::ShotState::Phase::ImpactTiming;
            shot->impactGaugePos = 0.0f;
            shot->impactGaugeDir = 1.0f;
            
            if (hud) {
                hud->SetImpactZonesVisible(ctx, true);
            }

            if (ctx.audio) {
                ctx.audio->PlaySE(ctx, "se_shot_charge.mp3", 0.5f);
            }
            break;
        }
        
        // 右クリックキャンセル
        if (ctx.input.GetMouseButtonDown(1)) {
            shot->Reset();
            if (hud) hud->ResetShotUI(ctx);
            if (ctx.audio) ctx.audio->PlaySE(ctx, "se_cancel.mp3", 0.6f);
            break;
        }

        // パワーゲージ往復
        AdvanceGauge(shot->powerGaugePos, shot->powerGaugeDir,
                     shot->powerGaugeSpeed, dt);
        break;
    }
    
    case game::components::ShotState::Phase::ImpactTiming: {
        // インパクト決定
        if (ctx.input.GetMouseButtonDown(0)) {
            shot->confirmedImpact = shot->impactGaugePos;

            // 判定ロジック
            shot->judgement =
                game::utils::EvaluateImpactJudgement(shot->confirmedImpact);
            if (shot->judgement == game::components::ShotJudgement::Special) {
                if (hud) hud->UpdateJudge(ctx, L"", game::ui::kColorSpecial);
            } else if (shot->judgement == game::components::ShotJudgement::Great) {
                if (hud) hud->UpdateJudge(ctx, L"", game::ui::kColorSuccess);
            } else if (shot->judgement == game::components::ShotJudgement::Nice) {
                if (hud) hud->UpdateJudge(ctx, L"", game::ui::kColorAccent);
            } else {
                if (hud) hud->UpdateJudge(ctx, L"", game::ui::kColorError);
            }

            // 判定音の再生
            auto feedback = game::utils::BuildJudgeFeedback(shot->judgement);
            if (ctx.audio && !feedback.soundPath.empty()) {
                ctx.audio->PlaySE(ctx, feedback.soundPath, feedback.soundVolume);
            }
            
            state->lastShotPosition = ctx.world.Get<game::components::Transform>(state->ballEntity)->position;
            eventOut.shotFired = true;
            break;
        }
        
        // キャンセル
        if (ctx.input.GetMouseButtonDown(1)) {
            shot->Reset();
            if (hud) hud->ResetShotUI(ctx);
            if (ctx.audio) ctx.audio->PlaySE(ctx, "se_cancel.mp3", 0.6f);
            break;
        }

        // インパクトゲージ往復
        AdvanceGauge(shot->impactGaugePos, shot->impactGaugeDir,
                     shot->impactGaugeSpeed, dt);
        break;
    }
    
    default:
        break;
    }
    
    return eventOut;
}

void ShotController::ExecuteShot(core::GameContext& ctx,
                                 ecs::Entity ballEntity,
                                 const DirectX::XMFLOAT3& shotDir,
                                 const ClubController::Club& club,
                                 game::systems::TimeOfDaySystem* timeOfDay,
                                 WikiGolfHUD* hud) {
    auto* state = ctx.world.GetGlobal<game::components::GolfGameState>();
    auto* shot = ctx.world.GetGlobal<game::components::ShotState>();
    if (!state || !shot) return;

    state->shotCount++;

    auto feedback = game::utils::BuildJudgeFeedback(shot->judgement);
    float curveAmount = 0.0f;

    if (shot->judgement == game::components::ShotJudgement::Nice) {
        curveAmount = 0.1f;
    } else if (shot->judgement == game::components::ShotJudgement::Miss) {
        curveAmount = 0.3f;
    }

    // 「基本飛距離からの変位」でショットの強さを決定する。
    // ゲージ比率と判定結果を距離の倍率として基準飛距離に掛け、
    // 目標飛距離をクラブごとのキャリー距離テーブルで初速へ逆引きする。
    const float distanceRatio = game::utils::ClampGaugeValue(
        shot->confirmedPower > 0.0f ? shot->confirmedPower : shot->powerGaugePos);
    const float judgementMultiplier =
        game::utils::GetJudgementDistanceMultiplier(shot->judgement);
    const float targetDistance =
        club.baseCarryDistance * distanceRatio * judgementMultiplier;
    const float power =
        game::utils::LookupSpeedForDistance(club.carryTable, targetDistance);

    float angleRad = DirectX::XMConvertToRadians(club.launchAngle);
    float vy = power * std::sin(angleRad);
    float vxz = power * std::cos(angleRad);

    DirectX::XMFLOAT3 finalDir = shotDir;
    if (curveAmount != 0.0f) {
        // 簡易的なカーブ処理
        finalDir.x += curveAmount;
    }

    auto* rb = ctx.world.Get<game::components::RigidBody>(ballEntity);
    if (rb) {
        rb->velocity = {finalDir.x * vxz, vy, finalDir.z * vxz};
        rb->angularVelocity = {0.0f, 0.0f, 0.0f}; // simplified
    }
    state->isBallGrounded = false;
    state->currentBallSpeed = power;

    shot->phase = game::components::ShotState::Phase::Executing;

    if (ctx.audio) {
        if (power > 40.0f) {
            ctx.audio->PlaySE(ctx, "se_shot_hard.mp3", 0.9f);
        } else if (power < 15.0f) {
            ctx.audio->PlaySE(ctx, "se_shot_soft.mp3", 0.7f);
        } else {
            ctx.audio->PlaySE(ctx, "se_shot.mp3", 0.8f);
        }
    }

    if (timeOfDay) {
        timeOfDay->OnShot(0.5f);
    }
    // ゲージの可視性（インパクト確定後の保持表示含む）は WikiGolfHUD 側で一元管理する。
}

} // namespace game::controllers
