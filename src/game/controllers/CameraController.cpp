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
// ローカルユーティリティ（翻訳単位内に閉じる）
// ============================================================
namespace {

/// @brief スラブ法による AABB 交差判定ヘルパー
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

/// @brief OBB とのレイ交差判定（ボックスのローカル空間へ変換してAABBで判定）
bool IntersectRayOBB(XMVECTOR rayOrigin, XMVECTOR rayDir, float maxDist,
                     XMVECTOR boxPos, XMVECTOR boxSize, XMVECTOR boxRot,
                     float &outDist) {
  XMVECTOR relOrigin = XMVectorSubtract(rayOrigin, boxPos);
  XMVECTOR invRot    = XMQuaternionInverse(boxRot);

  XMVECTOR localOrigin = XMVector3Rotate(relOrigin, invRot);
  XMVECTOR localDir    = XMVector3Rotate(rayDir,    invRot);

  float tMin = 0.0f;
  float tMax = maxDist;

  if (!IntersectRayAABBSlab(XMVectorGetX(localOrigin), XMVectorGetX(localDir),
                             -XMVectorGetX(boxSize), XMVectorGetX(boxSize),
                             tMin, tMax))
    return false;
  if (!IntersectRayAABBSlab(XMVectorGetY(localOrigin), XMVectorGetY(localDir),
                             -XMVectorGetY(boxSize), XMVectorGetY(boxSize),
                             tMin, tMax))
    return false;
  if (!IntersectRayAABBSlab(XMVectorGetZ(localOrigin), XMVectorGetZ(localDir),
                             -XMVectorGetZ(boxSize), XMVectorGetZ(boxSize),
                             tMin, tMax))
    return false;

  if (tMin <= tMax && tMax >= 0.0f) {
    outDist = tMin;
    return true;
  }
  return false;
}

} // namespace

// ============================================================
// 公開インターフェース実装
// ============================================================

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
  m_cameraChaseThreshold = m_cameraDistance;
}

void CameraController::SetTargetDistanceAndHeight(float recommendedDistance,
                                                   float recommendedHeight) {
  m_targetCameraDistance = recommendedDistance;
  m_targetCameraHeight   = recommendedHeight;
}

void CameraController::ResetForTransition(float fieldScale) {
  m_cameraYaw      = 0.0f;
  m_cameraPitch    = 0.5f;
  m_cameraDistance = 15.0f * fieldScale;
  m_shotDirection  = {0.0f, 0.0f, 1.0f};
  m_isCameraChasing = false;
}

void CameraController::OnShotStart(core::GameContext &ctx, float power) {
  // ExecuteShot 直前に呼ぶ: 開始カメラ位置を記録し追尾フラグをリセット
  using namespace game::components;
  auto *camT = ctx.world.Get<Transform>(m_cfg.cameraEntity);
  if (camT) {
    m_shotStartCamPos = camT->position;
  }
  m_isCameraChasing = false;
  m_cameraChaseThreshold = std::max(power * 1.0f, 5.0f);
}

void CameraController::ProcessInput(core::GameContext &ctx,
                                     int mouseX, int mouseY) {
  // 中ボタンドラッグで視点回転（非マップビュー・非ショット中）

  if (ctx.input.GetMouseButton(2)) {
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
  if (wheel != 0.0f && !ctx.input.GetMouseButton(2)) {
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

  if (isExecuting) {
    XMVECTOR ballPos     = XMLoadFloat3(&ballT->position);
    XMVECTOR startCamPos = XMLoadFloat3(&m_shotStartCamPos);

    float distFromStart = XMVectorGetX(
        XMVector3Length(XMVectorSubtract(ballPos, startCamPos)));

    if (!m_isCameraChasing) {
      if (distFromStart < m_cameraChaseThreshold) {
        // フェーズ1: 固定注視（カメラ位置固定・ボールを向く）
        camT->position = m_shotStartCamPos;

        XMVECTOR lookDir = XMVectorSubtract(ballPos, startCamPos);
        lookDir = XMVectorAdd(lookDir, XMVectorSet(0, 2.0f, 0, 0));

        if (XMVectorGetX(XMVector3LengthSq(lookDir)) > 0.001f) {
          lookDir = XMVector3Normalize(lookDir);

          float yaw   = std::atan2(XMVectorGetX(lookDir), XMVectorGetZ(lookDir));
          float pitch = -std::asin(XMVectorGetY(lookDir));

          float lerp = 10.0f * ctx.dt;
          float diff  = yaw - m_cameraYaw;
          while (diff >  XM_PI) diff -= XM_2PI;
          while (diff < -XM_PI) diff += XM_2PI;
          m_cameraYaw   += diff * lerp;
          m_cameraPitch += (pitch - m_cameraPitch) * lerp;
          m_cameraPitch  = std::clamp(m_cameraPitch, -0.1f, 1.4f);

          XMVECTOR q = XMQuaternionRotationRollPitchYaw(m_cameraPitch, m_cameraYaw, 0.0f);
          XMStoreFloat4(&camT->rotation, q);
        }
        return;
      } else {
        m_isCameraChasing = true;
      }
    }

    // フェーズ2: 追尾モード（ボール進行方向にYaw追従）
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
  }

  // === TPSオービット基準計算 ===
  XMVECTOR ballPos = XMLoadFloat3(&ballT->position);
  XMVECTOR camRotQ = XMQuaternionRotationRollPitchYaw(m_cameraPitch, m_cameraYaw, 0.0f);
  XMVECTOR offset  = XMVectorSet(0, 0, -m_cameraDistance, 0);
  offset           = XMVector3Rotate(offset, camRotQ);
  XMVECTOR camPos  = XMVectorAdd(ballPos, offset);

  // 衝突補正
  XMVECTOR adjustedPos;
  bool collided = CheckCameraCollision(ctx, camPos, ballPos, adjustedPos);
  XMStoreFloat3(&camT->position, adjustedPos);

  // 回転設定
  if (collided) {
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

  // === ショット方向をカメラ前方から算出（Idle 時のみ更新） ===
  if (!isExecuting) {
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
  XMStoreFloat3(&m_shotStartCamPos, adjustedPos);
}

// ============================================================
// 内部処理
// ============================================================

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
