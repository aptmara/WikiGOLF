/**
 * @file CameraController.cpp
 * @brief TPSオービットカメラ・マップ俯瞰カメラの制御実装
 *
 * 入力: GameContext（入力・カメラ/ボールTransform）、WikiTerrainSystem
 * 変更: カメラTransform/FOV、Yaw/Pitch/Distance、衝突補正
 * 出力: GetShotDirection() で正規化済みショット方向を提供
 *
 * 回帰リスク:
 *   - UpdateCamera の全ロジックをここへ移動
 *   - m_cameraYaw/Pitch/Distance は本クラスが唯一の管理者
 *   - WikiGolfScene 側の同名メンバは削除し、GetYaw()/GetPitch()/GetDistance() 参照に切り替える
*/

#include "CameraController.h"
#include "../../core/Input.h"
#include "../../core/Logger.h"
#include "../../ecs/World.h"
#include "../components/Camera.h"
#include "../components/PhysicsComponents.h"
#include "../components/Transform.h"
#include "../components/WikiComponents.h"
#include "../systems/GameJuiceSystem.h"
#include "../systems/WikiTerrainSystem.h"
#include <algorithm>
#include <cmath>

#undef min
#undef max

using namespace DirectX;

namespace game::controllers {

// ============================================================
// ローカルユーティリティ
// ============================================================
namespace {

/** @brief スラブ法による AABB 交差判定ヘルパー*/
bool IntersectRayAABBSlab(float start, float dir, float minVal, float maxVal,
                           float &tmin, float &tmax) {
  if (std::abs(dir) < 1e-6f) {
    return (start >= minVal && start <= maxVal);
  }
  float invDir = 1.0f / dir;
  float t1 = (minVal - start) * invDir;
  float t2 = (maxVal - start) * invDir;
  if (t1 > t2)
    std::swap(t1, t2);
  tmin = std::max(tmin, t1);
  tmax = std::min(tmax, t2);
  return tmin <= tmax;
}

/** @brief OBB とのレイ交差判定（ボックスのローカル空間へ変換してAABBで判定）*/
bool IntersectRayOBB(XMVECTOR rayOrigin, XMVECTOR rayDir, float maxDist,
                     XMVECTOR boxPos, XMVECTOR boxSize, XMVECTOR boxRot,
                     float &outDist) {
  XMVECTOR relOrigin = XMVectorSubtract(rayOrigin, boxPos);
  XMVECTOR invRot    = XMQuaternionInverse(boxRot);

  XMVECTOR localOrigin = XMVector3Rotate(relOrigin, invRot);
  XMVECTOR localDir    = XMVector3Rotate(rayDir,    invRot);

  float tMin = 0.0f;
  float tMax = maxDist;
  const XMVECTOR halfSize = XMVectorScale(boxSize, 0.5f);

  if (!IntersectRayAABBSlab(XMVectorGetX(localOrigin), XMVectorGetX(localDir),
                             -XMVectorGetX(halfSize), XMVectorGetX(halfSize),
                             tMin, tMax))
    return false;
  if (!IntersectRayAABBSlab(XMVectorGetY(localOrigin), XMVectorGetY(localDir),
                             -XMVectorGetY(halfSize), XMVectorGetY(halfSize),
                             tMin, tMax))
    return false;
  if (!IntersectRayAABBSlab(XMVectorGetZ(localOrigin), XMVectorGetZ(localDir),
                             -XMVectorGetZ(halfSize), XMVectorGetZ(halfSize),
                             tMin, tMax))
    return false;

  if (tMin <= tMax && tMax >= 0.0f) {
    outDist = tMin;
    return true;
  }
  return false;
}

// --- ショット直後のロボット三人称(見上げ)カメラ ---
constexpr float kShotCamEaseSeconds = 1.0f;   // 三人称視点へ寄るまでの秒数
constexpr float kShotCamBackRatio = 2.2f;     // ロボット背後への距離(身長比)
constexpr float kShotCamUpRatio = 1.1f;       // 高さ(身長比)
constexpr float kShotCamSideRatio = 0.35f;    // ボール側への横ずらし(身長比)
constexpr float kShotCamLookUpMaxDistance = 55.0f; // これ以上離れたら追尾へ
constexpr float kShotCamChaseFallSpeed = 1.0f;     // 落下速度がこれを超えたら追尾へ
constexpr float kOrbitBlendSeconds = 0.9f;    // 三人称→オービットの補間秒数
constexpr float kOrbitMinPitchAfterShot = 0.35f;

// --- カップイン演出カメラ ---
constexpr float kCelebrationEaseSeconds = 1.2f;
constexpr float kCelebrationDistanceRatio = 3.4f;
constexpr float kCelebrationHeightRatio = 0.9f;
constexpr float kCelebrationFocusHeightRatio = 0.55f;

float SmoothStep01(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

/** @brief fromからtoを向くカメラ回転(クォータニオン)*/
XMVECTOR LookRotation(FXMVECTOR from, FXMVECTOR to) {
  const XMMATRIX view = XMMatrixLookAtLH(from, to, XMVectorSet(0, 1, 0, 0));
  XMVECTOR det;
  return XMQuaternionRotationMatrix(XMMatrixInverse(&det, view));
}

} // namespace

// 公開インターフェース実装

void CameraController::Initialize(Config cfg) {
  m_cfg = cfg;
  m_prevMouseX = 0;
  m_prevMouseY = 0;

  LOG_INFO("CameraController", "Initialized: cameraEntity={}, ballEntity={}",
           cfg.cameraEntity, cfg.ballEntity);

  m_cameraDistance = 15.0f * cfg.fieldScale;
  m_targetCameraDistance = m_cameraDistance;
  m_targetCameraHeight   = 20.0f;
  m_cameraYaw   = 0.0f;
  m_cameraPitch = 0.5f;
  m_shotDirection = {0.0f, 0.0f, 1.0f};
  m_isCameraChasing = false;
}

void CameraController::SetTargetDistanceAndHeight(float recommendedDistance,
                                                   float recommendedHeight) {
  m_targetCameraDistance = recommendedDistance;
  m_targetCameraHeight   = recommendedHeight;
}

void CameraController::AimYawTowards(const DirectX::XMFLOAT3 &fromPos,
                                     const DirectX::XMFLOAT3 &toPos) {
  const float dx = toPos.x - fromPos.x;
  const float dz = toPos.z - fromPos.z;
  if (dx * dx + dz * dz < 0.0001f) {
    return; // 距離ゼロに近い場合は向きを変えない
  }
  m_cameraYaw = std::atan2(dx, dz);
}

void CameraController::ResetForTransition(float fieldScale) {
  m_cameraYaw      = 0.0f;
  m_cameraPitch    = 0.5f;
  m_cameraDistance = 15.0f * fieldScale;
  m_shotDirection  = {0.0f, 0.0f, 1.0f};
  m_isCameraChasing = false;
  m_wasShotCamera = false;
  m_orbitBlend = 1.0f;
}

void CameraController::SetGolferAnchor(const DirectX::XMFLOAT3 &position,
                                       float height) {
  m_golferAnchorPos = position;
  m_golferAnchorHeight = std::max(height, 0.1f);
  m_hasGolferAnchor = true;
}

void CameraController::OnShotStart(core::GameContext &ctx) {
  using namespace game::components;
  auto *camT = ctx.world.Get<Transform>(m_cfg.cameraEntity);
  if (camT) {
    m_shotStartCamPos = camT->position;
  }
  m_isCameraChasing = false;
  m_wasShotCamera = false;
  m_orbitBlend = 1.0f;
  m_shotCamTimer = 0.0f;
  m_shotTpsCamPos = m_shotStartCamPos;

  if (!m_hasGolferAnchor) {
    return; // 基準が無い場合はその場から見上げる
  }

  // ロボットの背後・やや上、ボール側へ少し寄せた位置（ショット方向基準）
  const float h = m_golferAnchorHeight;
  const XMVECTOR fwd = XMVector3Normalize(
      XMVectorSet(m_shotDirection.x, 0.0f, m_shotDirection.z, 0.0f));
  const XMVECTOR right =
      XMVectorSet(XMVectorGetZ(fwd), 0.0f, -XMVectorGetX(fwd), 0.0f);
  const XMVECTOR golfer = XMLoadFloat3(&m_golferAnchorPos);

  XMVECTOR tps =
      XMVectorSubtract(golfer, XMVectorScale(fwd, kShotCamBackRatio * h));
  tps = XMVectorAdd(tps, XMVectorSet(0.0f, kShotCamUpRatio * h, 0.0f, 0.0f));
  tps = XMVectorAdd(tps, XMVectorScale(right, kShotCamSideRatio * h));
  const XMVECTOR focus =
      XMVectorAdd(golfer, XMVectorSet(0.0f, 0.6f * h, 0.0f, 0.0f));

  XMVECTOR adjusted;
  CheckCameraCollision(ctx, tps, focus, adjusted);
  XMStoreFloat3(&m_shotTpsCamPos, adjusted);
}

void CameraController::BeginOrbitBlend(const DirectX::XMFLOAT3 &from) {
  m_orbitBlend = 0.0f;
  m_orbitBlendFrom = from;
  // 見上げ(負ピッチ)のままオービットするとボールの下に回り込むため戻す
  m_cameraPitch = std::max(m_cameraPitch, kOrbitMinPitchAfterShot);
}

void CameraController::BeginCelebrationView(core::GameContext &ctx,
                                            const DirectX::XMFLOAT3 &holePos,
                                            const DirectX::XMFLOAT3 &golferSpot,
                                            float golferHeight) {
  using namespace game::components;
  auto *camT = ctx.world.Get<Transform>(m_cfg.cameraEntity);
  if (!camT) {
    return;
  }

  const float h = std::max(golferHeight, 0.1f);
  m_celebrationFromPos = camT->position;
  m_celebrationTimer = 0.0f;
  m_isCameraChasing = false;
  m_wasShotCamera = false;
  m_orbitBlend = 1.0f;

  // ポールとロボットの中間を注視点にする
  const XMVECTOR focus = XMVectorAdd(
      XMVectorScale(
          XMVectorAdd(XMLoadFloat3(&holePos), XMLoadFloat3(&golferSpot)), 0.5f),
      XMVectorSet(0.0f, kCelebrationFocusHeightRatio * h, 0.0f, 0.0f));
  XMStoreFloat3(&m_celebrationFocus, focus);

  // 今のカメラがある側から正面に収める
  XMVECTOR dir = XMVectorSubtract(XMLoadFloat3(&camT->position), focus);
  dir = XMVectorSetY(dir, 0.0f);
  if (XMVectorGetX(XMVector3LengthSq(dir)) < 1e-4f) {
    dir = XMVectorSet(-m_shotDirection.x, 0.0f, -m_shotDirection.z, 0.0f);
  }
  dir = XMVector3Normalize(dir);

  XMVECTOR to =
      XMVectorAdd(focus, XMVectorScale(dir, kCelebrationDistanceRatio * h));
  to = XMVectorAdd(to, XMVectorSet(0.0f, kCelebrationHeightRatio * h, 0.0f, 0.0f));
  XMVECTOR adjusted;
  CheckCameraCollision(ctx, to, focus, adjusted);
  XMStoreFloat3(&m_celebrationToPos, adjusted);
}

void CameraController::UpdateCelebrationView(core::GameContext &ctx) {
  using namespace game::components;
  auto *camT = ctx.world.Get<Transform>(m_cfg.cameraEntity);
  if (!camT) {
    return;
  }

  m_celebrationTimer += ctx.dt;
  const float e = SmoothStep01(m_celebrationTimer / kCelebrationEaseSeconds);
  const XMVECTOR pos = XMVectorLerp(XMLoadFloat3(&m_celebrationFromPos),
                                    XMLoadFloat3(&m_celebrationToPos), e);
  const XMVECTOR focus = XMLoadFloat3(&m_celebrationFocus);
  XMStoreFloat3(&camT->position, pos);
  if (XMVectorGetX(XMVector3LengthSq(XMVectorSubtract(focus, pos))) > 1e-4f) {
    XMStoreFloat4(&camT->rotation, LookRotation(pos, focus));
  }
}

void CameraController::ProcessInput(core::GameContext &ctx,
                                     int mouseX, int mouseY) {
  // 視点回転はアイドル中の左ボタンドラッグに統一（マップビューのパン操作と
  // 揃える）。右ボタンドラッグも従来どおり併用可能。中ボタンはエイムピン
  // 設置専用のため、視点回転には使わない。
  // なお左ボタンは「動かさずに押して離す」とショットチャージ開始のクリックに
  // なるため（ShotController::ProcessShot参照）、実際の回転はマウスが動いた
  // フレームのみ発生し、静止クリックではここでの回転は起きない。
  auto *shotState = ctx.world.GetGlobal<components::ShotState>();
  bool isIdle = (!shotState || shotState->phase == components::ShotState::Phase::Idle);
  bool canRotate = isIdle && (ctx.input.GetMouseButton(0) || ctx.input.GetMouseButton(1));

  if (canRotate) {
    int deltaX = mouseX - m_prevMouseX;
    int deltaY = mouseY - m_prevMouseY;

    if (deltaX != 0 || deltaY != 0) {
      float sensitivity = 0.005f;
      if (ctx.input.GetKey(VK_SHIFT)) {
        sensitivity *= 0.33f; // 精密モード
      }
      m_cameraYaw   += deltaX * sensitivity;
      m_cameraPitch += deltaY * sensitivity;
      m_cameraPitch  = std::clamp(m_cameraPitch, -1.5f, 1.5f);
    }
  }

  // ホイールでズーム
  float wheel = ctx.input.GetMouseScrollDelta();
  if (wheel != 0.0f && !canRotate) {
    const float fieldScale = m_cfg.fieldScale;
    m_cameraDistance -= wheel * 2.0f * fieldScale;
    m_cameraDistance  = std::clamp(m_cameraDistance,
                                   1.2f * fieldScale, 35.0f * fieldScale);
  }

  m_prevMouseX = mouseX;
  m_prevMouseY = mouseY;
}

void CameraController::Update(core::GameContext &ctx) {
  using namespace game::components;

  if (!ctx.world.IsAlive(m_cfg.ballEntity) ||
      !ctx.world.IsAlive(m_cfg.cameraEntity)) {
    return;
  }

  auto *ballT = ctx.world.Get<Transform>(m_cfg.ballEntity);
  auto *camT  = ctx.world.Get<Transform>(m_cfg.cameraEntity);
  if (!ballT || !camT)
    return;

  auto *shotState = ctx.world.GetGlobal<ShotState>();
  const bool isExecuting =
      (shotState && shotState->phase == ShotState::Phase::Executing);
  // インパクト確定(スイング開始)からショット中までをショット用カメラとする
  const bool isShotCamera =
      isExecuting || (shotState &&
                      shotState->phase == ShotState::Phase::ImpactTiming &&
                      shotState->swingCommitted);

  if (isShotCamera) {
    if (!m_isCameraChasing) {
      // 約1秒でロボット背後の三人称視点へ寄り、そこから見上げてボールを追う
      m_shotCamTimer += ctx.dt;
      const float ease = SmoothStep01(m_shotCamTimer / kShotCamEaseSeconds);
      const XMVECTOR camPos = XMVectorLerp(XMLoadFloat3(&m_shotStartCamPos),
                                           XMLoadFloat3(&m_shotTpsCamPos), ease);
      XMStoreFloat3(&camT->position, camPos);

      const XMVECTOR ballPos = XMLoadFloat3(&ballT->position);
      XMVECTOR lookDir = XMVectorSubtract(
          XMVectorAdd(ballPos, XMVectorSet(0, 0.5f, 0, 0)), camPos);
      if (XMVectorGetX(XMVector3LengthSq(lookDir)) > 0.001f) {
        lookDir = XMVector3Normalize(lookDir);
        const float yaw = std::atan2(XMVectorGetX(lookDir), XMVectorGetZ(lookDir));
        const float pitch = -std::asin(XMVectorGetY(lookDir));

        const float lerp = std::min(1.0f, 10.0f * ctx.dt);
        float diff = yaw - m_cameraYaw;
        while (diff >  XM_PI) diff -= XM_2PI;
        while (diff < -XM_PI) diff += XM_2PI;
        m_cameraYaw   += diff * lerp;
        m_cameraPitch += (pitch - m_cameraPitch) * lerp;
        m_cameraPitch  = std::clamp(m_cameraPitch, -1.35f, 1.4f); // 見上げを許可

        XMStoreFloat4(&camT->rotation, XMQuaternionRotationRollPitchYaw(
                                           m_cameraPitch, m_cameraYaw, 0.0f));
      }

      // 寄り切った後、ボールが落下を始めるか遠ざかったら追尾へ移る
      bool shouldChase = false;
      if (isExecuting && m_shotCamTimer >= kShotCamEaseSeconds) {
        const float dx = ballT->position.x - XMVectorGetX(camPos);
        const float dz = ballT->position.z - XMVectorGetZ(camPos);
        const auto *ballRB = ctx.world.Get<RigidBody>(m_cfg.ballEntity);
        shouldChase = std::sqrt(dx * dx + dz * dz) > kShotCamLookUpMaxDistance ||
                      (ballRB && ballRB->velocity.y < -kShotCamChaseFallSpeed);
      }
      if (!shouldChase) {
        m_wasShotCamera = true;
        return;
      }
      m_isCameraChasing = true;
      BeginOrbitBlend(camT->position);
    }

    // 追尾モードフェーズ（ボール進行方向にヨー回転を追従）
    if (m_isCameraChasing) {
      auto *ballRB = ctx.world.Get<RigidBody>(m_cfg.ballEntity);
      if (ballRB) {
        float vx = ballRB->velocity.x;
        float vz = ballRB->velocity.z;
        float speedHoriz = std::sqrt(vx * vx + vz * vz);

        if (speedHoriz > 1.0f) {
          float targetYaw = std::atan2(vx, vz);
          float diff = targetYaw - m_cameraYaw;
          while (diff >  XM_PI) diff -= XM_2PI;
          while (diff < -XM_PI) diff += XM_2PI;
          m_cameraYaw += diff * 2.0f * ctx.dt;
        }
      }
      float targetPitch = 0.5f;
      m_cameraPitch += (targetPitch - m_cameraPitch) * 2.0f * ctx.dt;
    }
  } else if (m_wasShotCamera && !m_isCameraChasing) {
    // 追尾に入る前にボールが止まった(パット等)場合も三人称視点から滑らかに戻す
    BeginOrbitBlend(camT->position);
  }
  m_wasShotCamera = isShotCamera;

  // TPSオービットの基準位置を計算
  XMVECTOR ballPos = XMLoadFloat3(&ballT->position);
  XMVECTOR camRotQ = XMQuaternionRotationRollPitchYaw(m_cameraPitch, m_cameraYaw, 0.0f);
  XMVECTOR offset  = XMVectorSet(0, 0, -m_cameraDistance, 0);
  offset           = XMVector3Rotate(offset, camRotQ);
  XMVECTOR camPos  = XMVectorAdd(ballPos, offset);

  // 衝突補正
  XMVECTOR adjustedPos;
  bool collided = CheckCameraCollision(ctx, camPos, ballPos, adjustedPos);

  if (m_orbitBlend < 1.0f) {
    // 三人称(見上げ)視点の位置からオービット位置へ補間し、ボールを注視し続ける
    m_orbitBlend = std::min(1.0f, m_orbitBlend + ctx.dt / kOrbitBlendSeconds);
    const XMVECTOR blended = XMVectorLerp(XMLoadFloat3(&m_orbitBlendFrom),
                                          adjustedPos, SmoothStep01(m_orbitBlend));
    XMStoreFloat3(&camT->position, blended);
    const XMVECTOR focus = XMVectorAdd(ballPos, XMVectorSet(0, 0.5f, 0, 0));
    if (XMVectorGetX(XMVector3LengthSq(XMVectorSubtract(focus, blended))) > 0.001f) {
      XMStoreFloat4(&camT->rotation, LookRotation(blended, focus));
    } else {
      XMStoreFloat4(&camT->rotation, camRotQ);
    }
  } else {
    XMStoreFloat3(&camT->position, adjustedPos);
  }

  // 回転設定
  if (m_orbitBlend < 1.0f) {
    // 補間中は上で注視回転を設定済み
  } else if (collided) {
    XMVECTOR focusPoint = XMVectorAdd(ballPos, XMVectorSet(0, 0.5f, 0, 0));
    XMVECTOR lookDir    = XMVectorSubtract(focusPoint, adjustedPos);
    if (XMVectorGetX(XMVector3LengthSq(lookDir)) > 0.001f) {
      lookDir       = XMVector3Normalize(lookDir);
      XMMATRIX view = XMMatrixLookAtLH(adjustedPos, focusPoint, XMVectorSet(0, 1, 0, 0));
      XMVECTOR det;
      XMMATRIX camWorld = XMMatrixInverse(&det, view);
      XMStoreFloat4(&camT->rotation, XMQuaternionRotationMatrix(camWorld));
    } else {
      XMStoreFloat4(&camT->rotation, camRotQ);
    }
  } else {
    XMStoreFloat4(&camT->rotation, camRotQ);
  }

  // アイドル時のみカメラ前方からショット方向を算出
  // （スイング中にカメラが三人称へ回っても打球方向が変わらないようにする）
  if (!isShotCamera) {
    XMVECTOR forward = XMVectorSet(0, 0, 1, 0);
    forward          = XMVector3Rotate(forward, camRotQ);

    XMFLOAT3 fwd;
    XMStoreFloat3(&fwd, forward);
    fwd.y = 0.0f;
    XMVECTOR flatForward = XMLoadFloat3(&fwd);
    flatForward          = XMVector3Normalize(flatForward);
    XMStoreFloat3(&m_shotDirection, flatForward);
  }
}

void CameraController::RestoreAfterFade(core::GameContext &ctx) {
  using namespace game::components;

  if (!ctx.world.IsAlive(m_cfg.ballEntity) ||
      !ctx.world.IsAlive(m_cfg.cameraEntity))
    return;

  auto *ballT = ctx.world.Get<Transform>(m_cfg.ballEntity);
  auto *camT  = ctx.world.Get<Transform>(m_cfg.cameraEntity);
  if (!ballT || !camT)
    return;

  XMVECTOR ballPos = XMLoadFloat3(&ballT->position);
  XMVECTOR camRotQ = XMQuaternionRotationRollPitchYaw(m_cameraPitch, m_cameraYaw, 0.0f);
  XMVECTOR offset  = XMVectorSet(0, 0, -m_cameraDistance, 0);
  offset           = XMVector3Rotate(offset, camRotQ);
  XMVECTOR camPos  = XMVectorAdd(ballPos, offset);

  XMVECTOR adjustedPos;
  CheckCameraCollision(ctx, camPos, ballPos, adjustedPos);

  XMStoreFloat3(&camT->position, adjustedPos);
  XMStoreFloat4(&camT->rotation, camRotQ);

  m_isCameraChasing = false;
  m_wasShotCamera = false;
  m_orbitBlend = 1.0f;
  XMStoreFloat3(&m_shotStartCamPos, adjustedPos);
}

// 内部処理

bool CameraController::CheckCameraCollision(core::GameContext &ctx,
                                             const XMVECTOR &targetPos,
                                             const XMVECTOR &lookAtPos,
                                             XMVECTOR &outPos) {
  using namespace game::components;

  outPos = targetPos;
  bool collided = false;

  XMVECTOR rayVec = XMVectorSubtract(targetPos, lookAtPos);
  float dist = XMVectorGetX(XMVector3Length(rayVec));
  if (dist < 0.01f)
    return false;

  XMVECTOR rayDir = XMVectorScale(rayVec, 1.0f / dist);

  float closestHit = dist;
  bool  hitWall    = false;

  ctx.world.Query<Transform, RigidBody, Collider>().Each(
      [&](ecs::Entity e, Transform &t, RigidBody &rb, Collider &c) {
        if (!rb.isStatic || c.type != ColliderType::Box)
          return;
        // カメラ・ボール・床エンティティは除外
        if (e == m_cfg.ballEntity || e == m_cfg.cameraEntity ||
            e == m_cfg.floorEntity)
          return;
        // ステージ外周壁はプレイエリアを囲うだけの透明な境界であり、
        // カメラの視線を遮ってはいけないため衝突判定から除外する
        if (ctx.world.Get<Wall>(e))
          return;

        XMVECTOR boxPos  = XMLoadFloat3(&t.position);
        XMVECTOR boxSize = XMLoadFloat3(&c.size);
        boxSize          = XMVectorMultiply(boxSize, XMLoadFloat3(&t.scale));
        XMVECTOR boxRot  = XMLoadFloat4(&t.rotation);

        float hitDist = 0.0f;
        if (IntersectRayOBB(lookAtPos, rayDir, closestHit,
                            boxPos, boxSize, boxRot, hitDist)) {
          if (hitDist > 0.1f) {
            closestHit = hitDist;
            hitWall    = true;
          }
        }
      });

  if (hitWall) {
    float adjustedDist = std::max(0.5f, closestHit - 0.5f);
    outPos   = XMVectorAdd(lookAtPos, XMVectorScale(rayDir, adjustedDist));
    collided = true;
  }

  // 地形高さ制限
  if (m_cfg.terrain) {
    float camX = XMVectorGetX(outPos);
    float camZ = XMVectorGetZ(outPos);
    float terrainH  = m_cfg.terrain->GetHeight(camX, camZ);
    float currentY  = XMVectorGetY(outPos);
    float minHeight = terrainH + 0.5f;
    if (currentY < minHeight) {
      outPos   = XMVectorSetY(outPos, minHeight);
      collided = true;
    }
  } else {
    if (XMVectorGetY(outPos) < 0.5f) {
      outPos   = XMVectorSetY(outPos, 0.5f);
      collided = true;
    }
  }

  return collided;
}

} // namespace game::controllers
