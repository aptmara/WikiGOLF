#include "DebugFreeCamera.h"

#include "../../core/GameContext.h"
#include "../../ecs/World.h"
#include "../components/Camera.h"
#include "../components/Transform.h"
#include "imgui.h"
#include <cmath>

namespace game::debug {
namespace {

void CaptureAngles(const DirectX::XMFLOAT4 &rotation, float &yaw,
                   float &pitch) {
  const auto forward = DirectX::XMVector3Rotate(
      DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f),
      DirectX::XMLoadFloat4(&rotation));
  DirectX::XMFLOAT3 direction;
  DirectX::XMStoreFloat3(&direction, forward);
  yaw = std::atan2(direction.x, direction.z);
  pitch = std::asin((std::clamp)(-direction.y, -1.0f, 1.0f));
}

} // namespace

void DebugFreeCamera::DrawControls() {
  ImGui::Checkbox("フリーカメラを有効化 [F2]", &m_enabled);
  ImGui::DragFloat("移動速度", &m_moveSpeed, 0.5f, 0.1f, 200.0f);
  ImGui::DragFloat("高速倍率", &m_fastMultiplier, 0.1f, 1.0f, 20.0f);
  ImGui::DragFloat("マウス感度", &m_mouseSensitivity, 0.0001f, 0.0001f,
                   0.02f, "%.4f");
  ImGui::Text("WASD: 移動 / Q,E: 上下 / Shift: 高速");
  ImGui::Text("右ドラッグ: 視点回転");
  ImGui::TextDisabled("ゲームカメラ更新後、描画直前だけ姿勢を上書きします。");
}

void DebugFreeCamera::Apply(core::GameContext &ctx, float realDeltaSeconds) {
  if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
    m_enabled = !m_enabled;
  }
  if (!m_enabled) {
    m_cameraEntity = ecs::NULL_ENTITY;
    return;
  }

  ecs::Entity cameraEntity = ecs::NULL_ENTITY;
  ctx.world.Query<game::components::Camera, game::components::Transform>().Each(
      [&](ecs::Entity entity, game::components::Camera &camera,
          game::components::Transform &) {
        if (cameraEntity == ecs::NULL_ENTITY || camera.isMainCamera) {
          cameraEntity = entity;
        }
      });
  auto *transform = ctx.world.Get<game::components::Transform>(cameraEntity);
  if (!transform) {
    m_cameraEntity = ecs::NULL_ENTITY;
    return;
  }
  if (cameraEntity != m_cameraEntity) {
    m_cameraEntity = cameraEntity;
    m_pose.position = transform->position;
    CaptureAngles(transform->rotation, m_pose.yaw, m_pose.pitch);
  }

  const ImGuiIO &io = ImGui::GetIO();
  DebugFreeCameraInput input;
  if (!io.WantCaptureKeyboard) {
    input.moveForward = (ImGui::IsKeyDown(ImGuiKey_W) ? 1.0f : 0.0f) -
                        (ImGui::IsKeyDown(ImGuiKey_S) ? 1.0f : 0.0f);
    input.moveRight = (ImGui::IsKeyDown(ImGuiKey_D) ? 1.0f : 0.0f) -
                      (ImGui::IsKeyDown(ImGuiKey_A) ? 1.0f : 0.0f);
    input.moveUp = (ImGui::IsKeyDown(ImGuiKey_E) ? 1.0f : 0.0f) -
                   (ImGui::IsKeyDown(ImGuiKey_Q) ? 1.0f : 0.0f);
    input.speedMultiplier = ImGui::IsKeyDown(ImGuiKey_LeftShift)
                                ? m_fastMultiplier
                                : 1.0f;
  }
  if (!io.WantCaptureMouse && ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
    input.yawDelta = io.MouseDelta.x * m_mouseSensitivity;
    input.pitchDelta = io.MouseDelta.y * m_mouseSensitivity;
  }
  m_pose = StepDebugFreeCamera(m_pose, input, realDeltaSeconds, m_moveSpeed);
  transform->position = m_pose.position;
  DirectX::XMStoreFloat4(
      &transform->rotation,
      DirectX::XMQuaternionRotationRollPitchYaw(m_pose.pitch, m_pose.yaw,
                                                0.0f));
}

} // namespace game::debug
