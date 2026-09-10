#include "DebugCupInInspector.h"

#include "DebugColliderRenderer.h"
#include "DebugCupInStatus.h"
#include "../../core/GameContext.h"
#include "imgui.h"
#include <algorithm>

namespace game::debug {
namespace {

void DrawResult(const char *label, bool passed) {
  const ImVec4 color = passed ? ImVec4{0.25f, 0.9f, 0.35f, 1.0f}
                              : ImVec4{1.0f, 0.3f, 0.3f, 1.0f};
  ImGui::TextColored(color, "%s: %s", label, passed ? "true" : "false");
}

float SafeRatio(float value, float limit) {
  if (limit <= 0.0f) {
    return 1.0f;
  }
  return (std::clamp)(value / limit, 0.0f, 1.0f);
}

} // namespace

void DrawCupInInspector(core::GameContext &ctx,
                        DebugColliderSettings &colliderSettings) {
  if (ImGui::Checkbox("判定範囲をワールド表示",
                      &colliderSettings.cupInGuide) &&
      colliderSettings.cupInGuide) {
    colliderSettings.enabled = true;
    colliderSettings.holes = true;
  }

  const DebugCupInStatus status = CaptureCupInStatus(ctx.world);
  if (!status.available) {
    ImGui::TextDisabled("判定可能なボールまたはホールがありません。");
    return;
  }

  ImGui::Text("最寄りホール: #%u %s", status.holeEntity,
              status.linkTarget.c_str());
  DrawResult("最終ホールイン判定", status.readyForCupIn);
  ImGui::SameLine();
  DrawResult("ホールインワン", status.holeInOne);

  if (ImGui::CollapsingHeader("1. 水平位置条件",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawResult("判定内", status.withinHorizontalRange);
    ImGui::Text("水平距離: %.3f", status.horizontalDistance);
    ImGui::Text("判定半径: %.3f（ホール半径の90%%）",
                status.captureRadius);
    const float ratio = SafeRatio(status.horizontalDistance,
                                  status.captureRadius);
    ImGui::ProgressBar(ratio, {-1.0f, 0.0f},
                       status.withinHorizontalRange ? "範囲内" : "範囲外");
  }

  if (ImGui::CollapsingHeader("2. 高さ条件",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawResult("判定内", status.withinVerticalRange);
    ImGui::Text("ボールY - ホールY: %.3f", status.verticalOffset);
    ImGui::TextDisabled("条件: -1.000 < 相対Y < 0.000");
  }

  if (ImGui::CollapsingHeader("3. 速度条件",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawResult("十分に低速", status.slowEnough);
    ImGui::Text("速度: %.3f", status.speed);
    ImGui::TextDisabled("条件: 速度 < 0.100");
    ImGui::ProgressBar(SafeRatio(status.speed, 0.1f), {-1.0f, 0.0f},
                       status.slowEnough ? "低速" : "速すぎる");
  }

  if (ImGui::CollapsingHeader("4. ホール種別と打数",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawResult("目的ホール", status.targetHole);
    ImGui::Text("ホールインワン判定: ホールイン && 目的ホール && 打数 == 1");
    ImGui::TextDisabled("現在の打数は「ゲーム状態」タブで確認できます。");
  }
}

} // namespace game::debug
