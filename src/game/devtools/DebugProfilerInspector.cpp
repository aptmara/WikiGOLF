#include "DebugProfilerInspector.h"

#include "../../core/Profiler.h"
#include "imgui.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <vector>

namespace game::debug {
namespace {

void PlotSeries(const char *label, const std::vector<float> &values,
                const char *unit) {
  if (values.empty()) {
    ImGui::TextDisabled("%s: 履歴なし", label);
    return;
  }
  const float maximum = *(std::max_element(values.begin(), values.end()));
  char overlay[96] = {};
  std::snprintf(overlay, sizeof(overlay), "現在 %.3f%s / 最大 %.3f%s",
                values.back(), unit, maximum, unit);
  ImGui::PlotLines(label, values.data(), static_cast<int>(values.size()), 0,
                   overlay, 0.0f, maximum > 0.0f ? maximum * 1.1f : 1.0f,
                   {-1.0f, 72.0f});
}

template <typename Collection>
void SelectNamedItem(const char *label, const Collection &items,
                     std::string &selected) {
  const auto exists = std::find_if(items.begin(), items.end(),
                                   [&](const auto &item) {
                                     return item.name == selected;
                                   });
  if (exists == items.end() && !items.empty()) {
    selected = items.front().name;
  }
  if (ImGui::BeginCombo(label, selected.empty() ? "なし" : selected.c_str())) {
    for (const auto &item : items) {
      const bool active = item.name == selected;
      if (ImGui::Selectable(item.name.c_str(), active)) {
        selected = item.name;
      }
      if (active) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }
}

void DrawCpuScopeTable(const core::ProfilerFrameSnapshot &frame) {
  if (!ImGui::BeginTable("CpuScopeDetails", 4,
                         ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                             ImGuiTableFlags_ScrollY,
                         {0.0f, 180.0f})) {
    return;
  }
  ImGui::TableSetupColumn("スコープ");
  ImGui::TableSetupColumn("包括 ms");
  ImGui::TableSetupColumn("自己 ms");
  ImGui::TableSetupColumn("回数");
  ImGui::TableHeadersRow();
  for (const auto &scope : frame.cpuScopes) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(scope.name.c_str());
    ImGui::TableSetColumnIndex(1);
    ImGui::Text("%.3f", scope.inclusiveMs);
    ImGui::TableSetColumnIndex(2);
    ImGui::Text("%.3f", scope.exclusiveMs);
    ImGui::TableSetColumnIndex(3);
    ImGui::Text("%u", scope.calls);
  }
  ImGui::EndTable();
}

void DrawCounterTable(const core::ProfilerFrameSnapshot &frame) {
  if (!ImGui::BeginTable("CounterDetails", 2,
                         ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                             ImGuiTableFlags_ScrollY,
                         {0.0f, 180.0f})) {
    return;
  }
  ImGui::TableSetupColumn("カウンター");
  ImGui::TableSetupColumn("現在値");
  ImGui::TableHeadersRow();
  for (const auto &counter : frame.counters) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(counter.name.c_str());
    ImGui::TableSetColumnIndex(1);
    ImGui::Text("%.3f", counter.value);
  }
  ImGui::EndTable();
}

} // namespace

void DebugProfilerInspector::Draw() {
  const auto snapshots =
      core::Profiler::Instance().GetRecentCompletedFrameSnapshots(16);
  for (const auto &snapshot : snapshots) {
    m_history.Update(snapshot);
  }
  if (m_history.Frames().empty()) {
    ImGui::TextDisabled("完成したプロファイルフレームを待っています。");
    return;
  }

  const auto &latest = m_history.Frames().back();
  const core::ProfilerFrameSnapshot *latestGpu = nullptr;
  for (auto frame = m_history.Frames().rbegin();
       frame != m_history.Frames().rend(); ++frame) {
    if (frame->gpuReceived) {
      latestGpu = &*frame;
      break;
    }
  }
  ImGui::Text("フレーム: %llu / シーン: %s / 履歴: %zu",
              static_cast<unsigned long long>(latest.frameIndex),
              latest.scene.c_str(), m_history.Frames().size());
  ImGui::SameLine();
  if (ImGui::Button("履歴を消去")) {
    m_history.Clear();
    return;
  }

  if (ImGui::CollapsingHeader("全体", ImGuiTreeNodeFlags_DefaultOpen)) {
    PlotSeries("CPUフレーム", m_history.CpuFrameSeries(), " ms");
    PlotSeries("GPUフレーム", m_history.GpuScopeSeries("GPU.Frame"),
               " ms");
    ImGui::Text("プロセスCPU: %.1f%% / Working Set: %.1f MB / Private: %.1f MB",
                latest.processCpuPercent, latest.workingSetMb,
                latest.privateMb);
    ImGui::Text("Entity: %zu / 計測オーバーヘッド: %.4f ms",
                latest.entityCount, latest.profilerOverheadMs);
  }

  if (ImGui::CollapsingHeader("CPUスコープ",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    SelectNamedItem("CPUスコープ選択", latest.cpuScopes, m_cpuScope);
    PlotSeries("包括時間", m_history.CpuScopeSeries(m_cpuScope, true),
               " ms");
    PlotSeries("自己時間", m_history.CpuScopeSeries(m_cpuScope, false),
               " ms");
    DrawCpuScopeTable(latest);
  }

  if (ImGui::CollapsingHeader("GPUスコープ",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Text("結果受信: %s / 有効: %s",
                latestGpu ? "true" : "false",
                latestGpu && latestGpu->gpuValid ? "true" : "false");
    if (!latestGpu) {
      ImGui::TextDisabled("GPU結果は数フレーム遅れて到着します。");
    } else {
      ImGui::Text("GPU計測フレーム: %llu",
                  static_cast<unsigned long long>(latestGpu->frameIndex));
      SelectNamedItem("GPUスコープ選択", latestGpu->gpuScopes, m_gpuScope);
    }
    PlotSeries("GPU時間", m_history.GpuScopeSeries(m_gpuScope), " ms");
    ImGui::Text("頂点: %llu / プリミティブ: %llu / PS呼出: %llu",
                static_cast<unsigned long long>(
                    latestGpu ? latestGpu->pipeline.inputAssemblerVertices : 0),
                static_cast<unsigned long long>(
                    latestGpu ? latestGpu->pipeline.inputAssemblerPrimitives
                              : 0),
                static_cast<unsigned long long>(
                    latestGpu ? latestGpu->pipeline.pixelShaderInvocations
                              : 0));
  }

  if (ImGui::CollapsingHeader("物理詳細",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    constexpr std::array<const char *, 6> counters = {
        "Physics.SubSteps",       "Physics.TerrainSamples",
        "Physics.HoleCandidates", "Physics.StaticCandidates",
        "Physics.StaticChecks",   "Physics.SpatialCacheRebuilt"};
    PlotSeries("Physics.Update",
               m_history.CpuScopeSeries("Physics.Update", true), " ms");
    for (const char *counter : counters) {
      PlotSeries(counter, m_history.CounterSeries(counter), "");
    }
  }

  if (ImGui::CollapsingHeader("全カウンター")) {
    SelectNamedItem("カウンター選択", latest.counters, m_counter);
    PlotSeries("カウンター履歴", m_history.CounterSeries(m_counter), "");
    DrawCounterTable(latest);
  }
}

} // namespace game::debug
