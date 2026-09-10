#include "DebugCheckpointInspector.h"

#include "../../core/GameContext.h"
#include "imgui.h"

namespace game::debug {

void DebugCheckpointInspector::Draw(core::GameContext &ctx) {
  ImGui::Text("ボール・GolfGameState・ShotStateをメモリへ1件保存します。");
  if (ImGui::Button("現在状態を保存")) {
    m_result = m_checkpoint.Save(ctx.world)
                   ? "状態を保存しました。"
                   : "必要なゲーム状態がないため保存できません。";
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(!m_checkpoint.HasSnapshot());
  if (ImGui::Button("保存状態を復元")) {
    m_result = m_checkpoint.Restore(ctx.world)
                   ? "保存状態を復元しました。"
                   : "保存時と同じボールがないため復元しませんでした。";
  }
  ImGui::EndDisabled();
  ImGui::Text("保存済み: %s", m_checkpoint.HasSnapshot() ? "true" : "false");
  if (!m_result.empty()) {
    ImGui::TextUnformatted(m_result.c_str());
  }
  ImGui::TextDisabled("シーン移動後の別Entityには復元しません。");
}

} // namespace game::debug
