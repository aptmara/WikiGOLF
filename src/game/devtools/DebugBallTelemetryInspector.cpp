#include "DebugBallTelemetryInspector.h"

#include "../../core/GameContext.h"
#include "imgui.h"
#include <algorithm>
#include <cstdio>

namespace game::debug {
namespace {

void Plot(const char *label, const std::vector<float> &values,
          const char *unit, float fixedMinimum = 0.0f,
          float fixedMaximum = 0.0f) {
  if (values.empty()) {
    ImGui::TextDisabled("%s: 履歴なし", label);
    return;
  }
  const auto [minimum, maximum] =
      std::minmax_element(values.begin(), values.end());
  char overlay[96] = {};
  std::snprintf(overlay, sizeof(overlay), "現在 %.3f%s", values.back(), unit);
  float plotMinimum = fixedMaximum > fixedMinimum ? fixedMinimum : *minimum;
  float plotMaximum = fixedMaximum > fixedMinimum ? fixedMaximum : *maximum;
  if (plotMaximum <= plotMinimum) {
    plotMaximum = plotMinimum + 1.0f;
  }
  ImGui::PlotLines(label, values.data(), static_cast<int>(values.size()), 0,
                   overlay, plotMinimum, plotMaximum, {-1.0f, 75.0f});
}

} // namespace

void DebugBallTelemetryInspector::Update(core::GameContext &ctx,
                                         bool simulationAdvanced) {
  m_history.Update(ctx.world, simulationAdvanced);
}

void DebugBallTelemetryInspector::Draw() {
  ImGui::Text("履歴: %zu / %zuフレーム", m_history.Samples().size(),
              DebugBallTelemetryHistory::kMaximumFrames);
  ImGui::SameLine();
  if (ImGui::Button("履歴を消去")) {
    m_history.Clear();
  }
  Plot("速度", m_history.SpeedSeries(), " m/s");
  Plot("Y速度", m_history.VerticalVelocitySeries(), " m/s");
  Plot("角速度", m_history.AngularSpeedSeries(), " rad/s");
  Plot("接地", m_history.GroundedSeries(), "", 0.0f, 1.0f);
  ImGui::TextDisabled("接地グラフ: 0=false / 1=true");
}

} // namespace game::debug
