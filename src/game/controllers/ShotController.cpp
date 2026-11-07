/**
 * @file ShotController.cpp
 * @brief ShotController の実装
*/

#include "ShotController.h"
#include "../../core/Input.h"
#include "../../core/Logger.h"
#include "../../ecs/World.h"
#include "../../audio/AudioSystem.h"
#include "../components/PhysicsComponents.h"
#include "../components/WikiComponents.h"
#include "../components/Transform.h"
#include "../systems/AchievementEvent.h"
#include "../systems/AchievementEventBus.h"
#include "../systems/GrassFieldRenderer.h"
#include "../systems/GrassImpulseRules.h"
#include "../systems/TimeOfDaySystem.h"
#include "../utils/CarryDistanceTable.h"
#include "../utils/JudgeFeedback.h"
#include "../utils/ShotGaugeRules.h"
#include "../utils/UIConstants.h"
#include "WikiGolfHUD.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

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

// 左ボタンは視点回転ドラッグとしても使うため、押下からこの距離(px)以上
// マウスが動いた場合はドラッグ扱いとし、パワーチャージ開始のクリックとは
// みなさない（CameraController::ProcessInputの回転条件と対になる）。
constexpr int kAimClickMoveThreshold = 6;

} // namespace

ShotController::ShotEvent ShotController::ProcessShot(core::GameContext& ctx, bool canAim, WikiGolfHUD* hud, ClubController* clubCtrl) {
    ShotEvent eventOut;
    auto* state = ctx.world.GetGlobal<game::components::GolfGameState>();
    auto* shot = ctx.world.GetGlobal<game::components::ShotState>();
    if (!state || !shot) return eventOut;

    const float dt = ctx.dt;

    bool debugClick = false;
#ifdef WIKIGOLF_DEBUG_TOOLS
    debugClick = UpdateDebugAutoShot(ctx, canAim);
#endif

    switch (shot->phase) {
    case game::components::ShotState::Phase::Idle: {
        if (!canAim) {
            shot->aimClickTracking = false;
            break;
        }

        if (debugClick) {
            shot->aimClickTracking = false;
            shot->phase = game::components::ShotState::Phase::PowerCharging;
            shot->powerGaugePos = 0.0f;
            shot->powerGaugeDir = 1.0f;
            if (ctx.audio) {
                ctx.audio->PlaySE(ctx, "se_shot_charge.mp3", 0.7f);
            }
            break;
        }

        if (ctx.input.GetMouseButtonDown(0)) {
            const auto pos = ctx.input.GetMousePosition();
            shot->aimClickTracking = true;
            shot->aimClickPressX = pos.x;
            shot->aimClickPressY = pos.y;
        }

        // 押下から動かさずに離した場合だけ「クリック」として扱う。
        // しきい値以上動いた場合はCameraController側の視点回転ドラッグ
        // として消費されており、ここではチャージを開始しない。
        if (shot->aimClickTracking && ctx.input.GetMouseButtonUp(0)) {
            shot->aimClickTracking = false;
            const auto pos = ctx.input.GetMousePosition();
            const int dx = pos.x - shot->aimClickPressX;
            const int dy = pos.y - shot->aimClickPressY;
            if (dx * dx + dy * dy <= kAimClickMoveThreshold * kAimClickMoveThreshold) {
                // パワーチャージ開始
                shot->phase = game::components::ShotState::Phase::PowerCharging;
                shot->powerGaugePos = 0.0f;
                shot->powerGaugeDir = 1.0f;

                if (ctx.audio) {
                    ctx.audio->PlaySE(ctx, "se_shot_charge.mp3", 0.7f);
                }
            }
        }
        break;
    }

    case game::components::ShotState::Phase::PowerCharging: {
        // パワー決定
        if (ctx.input.GetMouseButtonDown(0) || debugClick) {
            shot->confirmedPower = shot->powerGaugePos;
            shot->phase = game::components::ShotState::Phase::ImpactTiming;
            shot->impactGaugePos = 0.0f;
            shot->impactGaugeDir = 1.0f;
            // インパクトの「パーフェクト」位置を確定パワーの位置に合わせる。
            // 端に寄りすぎると判定帯が画面外にはみ出して事実上パーフェクトが
            // 出せなくなるため、わずかに余白を残してクランプする。
            shot->impactPerfectCenter =
                std::clamp(shot->confirmedPower, 0.05f, 0.95f);

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
        // スイング中(最下点でボール発射待ち)は入力もゲージ進行も受け付けない
        if (shot->swingCommitted) {
            break;
        }

        // インパクト決定
        if (ctx.input.GetMouseButtonDown(0) || debugClick) {
            shot->confirmedImpact = shot->impactGaugePos;
            shot->swingCommitted = true;

            // 判定ロジック（パーフェクト中心はパワー確定位置に連動する）
            shot->judgement = game::utils::EvaluateImpactJudgement(
                shot->confirmedImpact, shot->impactPerfectCenter);

            if (ctx.achievementEvents) {
                game::systems::AchievementEvent shotJudged;
                shotJudged.type = game::systems::AchievementEventType::ShotJudged;
                shotJudged.judgement = shot->judgement;
                ctx.achievementEvents->Publish(shotJudged);
            }

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

#ifdef WIKIGOLF_DEBUG_TOOLS
bool ShotController::UpdateDebugAutoShot(core::GameContext& ctx, bool canAim) {
    if (!m_debugAutoShotRead) {
        m_debugAutoShotRead = true;
        char* value = nullptr;
        size_t length = 0;
        if (_dupenv_s(&value, &length, "WIKIGOLF_DEBUG_AUTO_SHOT") == 0 && value) {
            m_debugAutoShotWait = static_cast<float>(std::atof(value));
            std::free(value);
        }
    }
    if (!(m_debugAutoShotWait > 0.0f)) {
        return false;
    }
    const auto* shot = ctx.world.GetGlobal<game::components::ShotState>();
    if (!shot) {
        return false;
    }

    const int phase = static_cast<int>(shot->phase);
    if (phase != m_debugAutoShotPhase) {
        m_debugAutoShotPhase = phase;
        m_debugAutoShotTimer = 0.0f;
    }
    m_debugAutoShotTimer += ctx.dt;

    using Phase = game::components::ShotState::Phase;
    constexpr float kTargetPower = 0.7f;
    constexpr float kImpactWindow = 0.04f;
    constexpr float kImpactGiveUpSeconds = 3.0f;
    switch (shot->phase) {
    case Phase::Idle:
        if (!canAim) {
            m_debugAutoShotTimer = 0.0f;
            return false;
        }
        return m_debugAutoShotTimer >= m_debugAutoShotWait;
    case Phase::PowerCharging:
        return shot->powerGaugePos >= kTargetPower;
    case Phase::ImpactTiming:
        if (shot->swingCommitted) {
            return false;
        }
        // フレームが重くて判定帯を飛び越えても止まらないよう、一定時間で打つ
        if (std::abs(shot->impactGaugePos - shot->impactPerfectCenter) <= kImpactWindow ||
            m_debugAutoShotTimer >= kImpactGiveUpSeconds) {
            LOG_INFO("Debug", "Debug auto shot: power={:.2f}", shot->confirmedPower);
            return true;
        }
        return false;
    default:
        return false;
    }
}
#endif

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

    const DirectX::XMFLOAT3 finalDir = game::utils::ApplyImpactSideError(
        shotDir, shot->judgement, shot->confirmedImpact,
        shot->impactPerfectCenter);

    auto* rb = ctx.world.Get<game::components::RigidBody>(ballEntity);
    if (rb) {
        rb->velocity = {finalDir.x * vxz, vy, finalDir.z * vxz};
        rb->angularVelocity = {0.0f, 0.0f, 0.0f}; // simplified
    }
    state->isBallGrounded = false;
    state->currentBallSpeed = power;

    if (const auto* ballTransform = ctx.world.Get<game::components::Transform>(ballEntity)) {
        const float length = std::sqrt(finalDir.x * finalDir.x + finalDir.z * finalDir.z);
        game::components::GrassImpulse impulse;
        impulse.kind = game::components::GrassImpulseKind::Swing;
        impulse.position = ballTransform->position;
        if (length > 1.0e-4f) {
            impulse.direction = {finalDir.x / length, finalDir.z / length};
        }
        impulse.strength = game::systems::GrassSwingStrength(power);
        game::systems::AddGrassImpulse(ctx, impulse);
    }

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
