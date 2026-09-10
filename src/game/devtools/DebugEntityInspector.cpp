#include "DebugEntityInspector.h"

#include "DebugColliderRenderer.h"
#include "DebugEntitySnapshot.h"
#include "../../core/GameContext.h"
#include "../../ecs/World.h"
#include "../components/WikiComponents.h"
#include "imgui.h"

namespace game::debug {
namespace {

void DrawVector3(const char *label, const DirectX::XMFLOAT3 &value) {
  ImGui::Text("%s: %.3f, %.3f, %.3f", label, value.x, value.y, value.z);
}

} // namespace

void DebugEntityInspector::Draw(core::GameContext &ctx,
                                DebugColliderSettings &colliderSettings) {
  ImGui::SetNextItemWidth(180.0f);
  ImGui::InputScalar("Entity ID", ImGuiDataType_U32, &m_entity);
  ImGui::SameLine();
  if (ImGui::Button("ボールを選択")) {
    if (const auto *state =
            ctx.world.GetGlobal<game::components::GolfGameState>()) {
      m_entity = state->ballEntity;
    }
  }
  if (ImGui::Button("ワールドにEntity IDを表示")) {
    colliderSettings.enabled = true;
    colliderSettings.entityIds = true;
  }

  const DebugEntitySnapshot data =
      CaptureDebugEntitySnapshot(ctx.world, m_entity);
  if (!data.alive) {
    ImGui::TextDisabled("Entity #%u は現在生存していません。", m_entity);
    return;
  }
  ImGui::Text("Entity #%u / index=%u / generation=%u", data.entity,
              ecs::GetEntityIndex(data.entity),
              ecs::GetEntityGeneration(data.entity));

  if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (!data.hasTransform) {
      ImGui::TextDisabled("なし");
    } else {
      DrawVector3("位置", data.position);
      ImGui::Text("回転Quaternion: %.3f, %.3f, %.3f, %.3f", data.rotation.x,
                  data.rotation.y, data.rotation.z, data.rotation.w);
      DrawVector3("スケール", data.scale);
    }
  }
  if (ImGui::CollapsingHeader("RigidBody", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (!data.hasRigidBody) {
      ImGui::TextDisabled("なし");
    } else {
      DrawVector3("速度", data.velocity);
      DrawVector3("加速度", data.acceleration);
      DrawVector3("角速度", data.angularVelocity);
      ImGui::Text("質量 %.3f / Drag %.3f / 静的 %s", data.mass, data.drag,
                  data.isStatic ? "true" : "false");
      ImGui::Text("転がり摩擦 %.3f / 反発 %.3f / Spin減衰 %.3f",
                  data.rollingFriction, data.restitution, data.spinDecay);
    }
  }
  if (ImGui::CollapsingHeader("Collider", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (!data.hasCollider) {
      ImGui::TextDisabled("なし");
    } else {
      ImGui::Text("種類: %s / 半径: %.3f", data.colliderType.c_str(),
                  data.colliderRadius);
      DrawVector3("サイズ", data.colliderSize);
      DrawVector3("オフセット", data.colliderOffset);
    }
  }
  if (data.hasHeading && ImGui::CollapsingHeader("Heading")) {
    ImGui::Text("HP: %d / %d / 破壊済み: %s", data.headingHealth,
                data.headingMaximumHealth,
                data.headingDestroyed ? "true" : "false");
    ImGui::TextWrapped("見出し: %s", data.headingText.c_str());
    ImGui::TextWrapped("リンク: %s", data.headingLink.c_str());
  }
  if (data.hasGolfHole && ImGui::CollapsingHeader("GolfHole")) {
    ImGui::Text("目的ホール: %s / 半径: %.3f / 吸引力: %.3f",
                data.targetHole ? "true" : "false", data.holeRadius,
                data.holeGravity);
    ImGui::TextWrapped("リンク: %s", data.holeLink.c_str());
  }
}

} // namespace game::debug
