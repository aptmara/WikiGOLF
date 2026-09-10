#include "DebugBallInspector.h"

#include "DebugBallTeleport.h"
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
  if (!m_result.empty()) {
    ImGui::TextUnformatted(m_result.c_str());
  }
  ImGui::TextDisabled("テレポート後は接地判定を解除し、次の物理更新で再判定します。");
}

} // namespace game::debug
