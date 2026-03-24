#include "ShotController.h"
#include "../../core/Input.h"
#include "../../ecs/World.h"
#include "../../audio/AudioSystem.h"
#include "../components/PhysicsComponents.h"
#include "../components/WikiComponents.h"
#include "../components/Transform.h"
#include "../systems/TimeOfDaySystem.h"
#include "../utils/JudgeFeedback.h"
#include "WikiGolfHUD.h"
#include <cmath>

namespace game::controllers {

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
                ctx.audio->PlaySE(ctx, "se_charge.mp3");
            }
        }
        break;
    }
    
    case game::components::ShotState::Phase::PowerCharging: {
        // パワーゲージ往復
        shot->powerGaugePos += shot->powerGaugeDir * shot->powerGaugeSpeed * dt;
        if (shot->powerGaugePos >= 1.0f) {
            shot->powerGaugePos = 1.0f;
            shot->powerGaugeDir = -1.0f;
        } else if (shot->powerGaugePos <= 0.0f) {
            shot->powerGaugePos = 0.0f;
            shot->powerGaugeDir = 1.0f;
        }
        
        // パワー決定
        if (ctx.input.GetMouseButtonDown(0)) {
            shot->confirmedPower = shot->powerGaugePos;
            shot->phase = game::components::ShotState::Phase::ImpactTiming;
            shot->impactGaugePos = 0.0f;
            shot->impactGaugeDir = 1.0f;
            
            if (ctx.audio) {
                ctx.audio->PlaySE(ctx, "se_shot_charge.mp3");
            }
        }
        
        // 右クリックキャンセル
        if (ctx.input.GetMouseButtonDown(1)) {
            shot->phase = game::components::ShotState::Phase::Idle;
            shot->powerGaugePos = 0.0f;
        }
        break;
    }
    
    case game::components::ShotState::Phase::ImpactTiming: {
        // インパクトゲージ往復
        shot->impactGaugePos += shot->impactGaugeDir * shot->impactGaugeSpeed * dt;
        if (shot->impactGaugePos >= 1.0f) {
            shot->impactGaugePos = 1.0f;
            shot->impactGaugeDir = -1.0f;
        } else if (shot->impactGaugePos <= 0.0f) {
            shot->impactGaugePos = 0.0f;
            shot->impactGaugeDir = 1.0f;
        }
        
        // インパクト決定
        if (ctx.input.GetMouseButtonDown(0)) {
            shot->confirmedImpact = shot->impactGaugePos;
            
            // 判定ロジック
            float diff = std::abs(shot->confirmedImpact - 0.5f);
            if (diff < 0.02f) {
                shot->judgement = game::components::ShotJudgement::Special;
                if (hud) hud->UpdateJudge(ctx, L"SPECIAL", {1.0f, 0.8f, 0.2f, 1.0f});
            } else if (diff < 0.05f) {
                shot->judgement = game::components::ShotJudgement::Great;
                if (hud) hud->UpdateJudge(ctx, L"Great", {0.8f, 0.2f, 0.2f, 1.0f});
            } else if (diff < 0.15f) {
                shot->judgement = game::components::ShotJudgement::Nice;
                if (hud) hud->UpdateJudge(ctx, L"Nice", {0.2f, 0.8f, 0.2f, 1.0f});
            } else {
                shot->judgement = game::components::ShotJudgement::Miss;
                if (hud) hud->UpdateJudge(ctx, L"Miss", {0.2f, 0.2f, 0.8f, 1.0f});
            }
            
            state->lastShotPosition = ctx.world.Get<game::components::Transform>(state->ballEntity)->position;
            eventOut.shotFired = true;
        }
        
        // キャンセル
        if (ctx.input.GetMouseButtonDown(1)) {
            shot->phase = game::components::ShotState::Phase::Idle;
        }
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
                                 float clubPower,
                                 float clubAngle,
                                 game::systems::TimeOfDaySystem* timeOfDay,
                                 WikiGolfHUD* hud) {
    auto* state = ctx.world.GetGlobal<game::components::GolfGameState>();
    auto* shot = ctx.world.GetGlobal<game::components::ShotState>();
    if (!state || !shot) return;

    state->shotCount++;

    auto feedback = game::utils::BuildJudgeFeedback(shot->judgement);
    float powerMultiplier = 1.0f;
    float curveAmount = 0.0f;

    if (shot->judgement == game::components::ShotJudgement::Special) {
        powerMultiplier = 1.05f;
    } else if (shot->judgement == game::components::ShotJudgement::Nice) {
        powerMultiplier = 0.8f;
        curveAmount = 0.1f;
    } else if (shot->judgement == game::components::ShotJudgement::Miss) {
        powerMultiplier = 0.5f;
        curveAmount = 0.3f;
    }

    float power = clubPower * shot->powerGaugePos * powerMultiplier;

    float angleRad = DirectX::XMConvertToRadians(clubAngle);
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

    shot->phase = game::components::ShotState::Phase::Executing;

    if (ctx.audio) {
        if (power > 30.0f) {
            ctx.audio->PlaySE(ctx, "se_shot_hard.mp3");
        } else {
            ctx.audio->PlaySE(ctx, "se_shot_soft.mp3");
        }
    }

    if (timeOfDay) {
        timeOfDay->OnShot(0.5f);
    }

    if (hud) {
        hud->UpdateJudge(ctx, L"Shot!", {1.0f, 1.0f, 1.0f, 1.0f});
    }
}

} // namespace game::controllers
