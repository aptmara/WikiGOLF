#include "CameraSystem.h"
#include "../../core/Input.h"
#include "../../core/Logger.h"
#include "../../ecs/World.h"
#include "../../game/components/Camera.h"
#include "../../game/components/PhysicsComponents.h"
#include "../../game/components/Transform.h"
#include "../../game/components/WikiComponents.h"
#include <DirectXMath.h>

namespace game::systems {

using namespace DirectX;

// カメラの状態保持用（簡易的にstatic変数を使用）
static float s_yaw = 0.0f;
static float s_pitch = 0.5f; // 少し見下ろし
static float s_distance = 5.0f;

void CameraSystem(core::GameContext &ctx) {
  auto &input = ctx.input;
  auto &world = ctx.world;
  const float dt = ctx.dt;

  // 1. ターゲット（ボール）を見つける
  ecs::Entity targetEntity = ecs::NULL_ENTITY;
  world.Query<components::Transform, components::RigidBody>().Each(
      [&](ecs::Entity e, components::Transform &t, components::RigidBody &rb) {
        if (!rb.isStatic) {
          targetEntity = e;
        }
      });

  if (!world.IsAlive(targetEntity))
    return;

  auto *targetT = world.Get<components::Transform>(targetEntity);

  // 2. マウス入力による回転
  float mouseSensitivity = 0.005f;

  // マウス位置の差分計算
  static int lastMouseX = 0;
  static int lastMouseY = 0;
  static bool firstFrame = true;

  auto mousePos = input.GetMousePosition();
  if (firstFrame) {
    lastMouseX = mousePos.x;
    lastMouseY = mousePos.y;
    firstFrame = false;
  }

  float dx = static_cast<float>(mousePos.x - lastMouseX);
  float dy = static_cast<float>(mousePos.y - lastMouseY);
  lastMouseX = mousePos.x;
  lastMouseY = mousePos.y;

  // 右クリック中のみ回転
  if (input.GetMouseButton(1)) {
    s_yaw += dx * mouseSensitivity;
    s_pitch += dy * mouseSensitivity;

    // Pitch制限
    const float pitchLimit = XM_PIDIV2 - 0.1f;
    if (s_pitch > pitchLimit)
      s_pitch = pitchLimit;
    if (s_pitch < -0.2f)
      s_pitch = -0.2f; // 地面より下には行かない
  }

  // 矢印キーでも回転（補助）
  float keyRotSpeed = 2.0f * dt;
  if (input.GetKey(VK_LEFT))
    s_yaw -= keyRotSpeed;
  if (input.GetKey(VK_RIGHT))
    s_yaw += keyRotSpeed;
  if (input.GetKey(VK_UP))
    s_pitch -= keyRotSpeed;
  if (input.GetKey(VK_DOWN))
    s_pitch += keyRotSpeed;

  // 3. カメラ位置の計算
  XMVECTOR targetPos = XMLoadFloat3(&targetT->position);

  // カメラの回転クォータニオン
  XMVECTOR camRotQ = XMQuaternionRotationRollPitchYaw(s_pitch, s_yaw, 0.0f);

  // オフセット（後ろ方向）
  XMVECTOR offset = XMVectorSet(0, 0, -s_distance, 0);
  offset = XMVector3Rotate(offset, camRotQ);

  // カメラ位置
  XMVECTOR camPos = XMVectorAdd(targetPos, offset);
  // 少し上にあげる（注視点をボールの中心より少し上にする）
  camPos = XMVectorAdd(camPos, XMVectorSet(0, 1.0f, 0, 0));

  // 4. カメラ更新
  world.Query<components::Transform, components::Camera>().Each(
      [&](ecs::Entity, components::Transform &t, components::Camera &c) {
        if (!c.isMainCamera)
          return;

        XMStoreFloat3(&t.position, camPos);
        XMStoreFloat4(&t.rotation, camRotQ);
      });
}

} // namespace game::systems
