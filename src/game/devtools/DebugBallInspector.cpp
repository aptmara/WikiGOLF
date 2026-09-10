#include "DebugBallInspector.h"

#include "DebugBallTeleport.h"
#include "DebugBallImpulse.h"
#include "../../core/GameContext.h"
#include "imgui.h"

namespace game::debug {
namespace {

const char *ResultText(DebugBallTeleportResult result) {
  switch (result) {
  case DebugBallTeleportResult::Success:
    return "ボールを移動しました。";
  case DebugBallTeleportResult::MissingGameState:
    return "GolfGameStateがないため移動できません。";
  case DebugBallTeleportResult::MissingBallComponents:
    return "ボールのTransform/RigidBodyがないため移動できません。";
  }
  return "移動できませんでした。";
}

} // namespace

void DebugBallInspector::Draw(core::GameContext &ctx) {
  if (!m_hasTarget) {
    m_hasTarget = CaptureDebugBallPosition(ctx.world, m_target);
  }
  ImGui::SeparatorText("ボール位置");
  if (ImGui::Button("現在位置を読み込む")) {
    m_hasTarget = CaptureDebugBallPosition(ctx.world, m_target);
    if (!m_hasTarget) {
      m_result = "有効なボール位置を取得できません。";
    }
  }
  ImGui::BeginDisabled(!m_hasTarget);
  ImGui::DragFloat3("移動先 XYZ", &m_target.x, 0.1f, 0.0f, 0.0f, "%.3f");
  ImGui::Checkbox("移動時に速度・加速度・角速度をゼロにする",
                  &m_resetMotion);
  if (ImGui::Button("この位置へ移動")) {
    const auto result = TeleportDebugBall(ctx.world, m_target, m_resetMotion);
    m_result = ResultText(result);
  }
  ImGui::EndDisabled();
  ImGui::TextDisabled("テレポート後は接地判定を解除し、次の物理更新で再判定します。");

  ImGui::SeparatorText("物理パラメーター");
  if (!m_hasPhysics) {
    m_hasPhysics = CaptureDebugBallPhysics(ctx.world, m_physics);
  }
  if (ImGui::Button("物理値を再読込")) {
    m_hasPhysics = CaptureDebugBallPhysics(ctx.world, m_physics);
    m_result = m_hasPhysics ? "現在の物理値を読み込みました。"
                            : "有効なRigidBodyを取得できません。";
  }
  ImGui::BeginDisabled(!m_hasPhysics);
  ImGui::DragFloat("質量", &m_physics.mass, 0.01f);
  ImGui::DragFloat("空気抵抗", &m_physics.drag, 0.001f);
  ImGui::DragFloat("転がり摩擦", &m_physics.rollingFriction, 0.01f);
  ImGui::DragFloat("反発", &m_physics.restitution, 0.01f);
  ImGui::DragFloat("スピン減衰", &m_physics.spinDecay, 0.01f);
  if (ImGui::Button("物理値を適用")) {
    const bool applied = ApplyDebugBallPhysics(ctx.world, m_physics);
    m_result = applied ? "物理値を適用しました。"
                       : "有効なRigidBodyへ適用できませんでした。";
  }
  ImGui::EndDisabled();
  ImGui::TextDisabled("入力値はデバッグ検証用にクランプしません。");

  ImGui::SeparatorText("Impulse");
  ImGui::DragFloat3("力積 XYZ", &m_impulse.x, 0.1f);
  if (ImGui::Button("Impulseを適用")) {
    const auto impulseResult = ApplyDebugBallImpulse(ctx.world, m_impulse);
    if (impulseResult == DebugBallImpulseResult::Success) {
      m_result = "Impulseを適用しました。";
    } else if (impulseResult == DebugBallImpulseResult::ZeroMass) {
      m_result = "質量が0のためImpulseを適用できません。";
    } else {
      m_result = "有効なボールへImpulseを適用できません。";
    }
  }
  ImGui::TextDisabled("速度変化 = Impulse / 質量");
  if (!m_result.empty()) {
    ImGui::TextUnformatted(m_result.c_str());
  }
}

} // namespace game::debug
