#include "DebugOverlay.h"

#include "DebugTimeController.h"
#include "../../core/GameContext.h"
#include "../../core/Logger.h"
#include "../../core/SceneManager.h"
#include "imgui.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>

namespace game::debug {
namespace {

bool ContainsCaseInsensitive(const std::string &text, const char *filter) {
  if (!filter || filter[0] == '\0') {
    return true;
  }
  std::string source = text;
  std::string needle = filter;
  std::transform(source.begin(), source.end(), source.begin(),
                 [](unsigned char value) { return std::tolower(value); });
  std::transform(needle.begin(), needle.end(), needle.begin(),
                 [](unsigned char value) { return std::tolower(value); });
  return source.find(needle) != std::string::npos;
}

ImVec4 LogColor(core::LogLevel level) {
  switch (level) {
  case core::LogLevel::Debug:
    return {0.65f, 0.65f, 0.65f, 1.0f};
  case core::LogLevel::Info:
    return {0.9f, 0.9f, 0.9f, 1.0f};
  case core::LogLevel::Warning:
    return {1.0f, 0.8f, 0.2f, 1.0f};
  case core::LogLevel::Error:
    return {1.0f, 0.3f, 0.3f, 1.0f};
  }
  return {1.0f, 1.0f, 1.0f, 1.0f};
}

} // namespace

void DebugOverlay::Draw(core::GameContext &ctx, DebugTimeController &time) {
  if (!m_visible) {
    return;
  }

  ImGui::SetNextWindowSize({620.0f, 520.0f}, ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("WikiGOLF Debug [F1]", &m_visible)) {
    ImGui::End();
    return;
  }

  const char *sceneName = ctx.sceneManager && ctx.sceneManager->Current()
                              ? ctx.sceneManager->Current()->GetName()
                              : "NoScene";
  ImGui::Text("Scene: %s", sceneName);

  if (ImGui::BeginTabBar("DebugTabs")) {
    if (ImGui::BeginTabItem("Simulation")) {
      DrawSimulation(time);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Log")) {
      DrawLog();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Collision")) {
      DrawColliders();
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  ImGui::End();
}

void DebugOverlay::DrawColliders() {
  ImGui::Checkbox("Show collider debug", &m_colliderSettings.enabled);
  ImGui::Checkbox("Sphere", &m_colliderSettings.spheres);
  ImGui::SameLine();
  ImGui::Checkbox("Box", &m_colliderSettings.boxes);
  ImGui::SameLine();
  ImGui::Checkbox("Cylinder", &m_colliderSettings.cylinders);
  ImGui::Checkbox("Terrain bounds", &m_colliderSettings.terrain);
  ImGui::SameLine();
  ImGui::Checkbox("Goal holes", &m_colliderSettings.holes);
  ImGui::Checkbox("Entity ID", &m_colliderSettings.entityIds);
  ImGui::TextColored({0.3f, 0.9f, 0.4f, 1.0f}, "Green: collider");
  ImGui::SameLine();
  ImGui::TextColored({1.0f, 0.25f, 0.25f, 1.0f}, "Red: colliding");
  ImGui::SameLine();
  ImGui::TextColored({1.0f, 0.85f, 0.2f, 1.0f}, "Yellow: hole");
}

void DebugOverlay::DrawSimulation(DebugTimeController &time) {
  bool paused = time.IsPaused();
  if (ImGui::Checkbox("Paused [F5]", &paused)) {
    time.SetPaused(paused);
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(!time.IsPaused());
  if (ImGui::Button("Step frame [F6]")) {
    time.RequestStep();
  }
  ImGui::EndDisabled();

  ImGui::SeparatorText("Time scale [F7 cycles]");
  for (int index = 0;
       index < static_cast<int>(DebugTimeController::kTimeScales.size());
       ++index) {
    if (index > 0) {
      ImGui::SameLine();
    }
    const float scale = DebugTimeController::kTimeScales[index];
    char label[16] = {};
    std::snprintf(label, sizeof(label), "%.2fx", scale);
    const bool selected = time.GetTimeScale() == scale;
    if (selected) {
      ImGui::PushStyleColor(ImGuiCol_Button, {0.2f, 0.55f, 0.25f, 1.0f});
    }
    if (ImGui::Button(label)) {
      time.SetTimeScaleIndex(index);
    }
    if (selected) {
      ImGui::PopStyleColor();
    }
  }
}

void DebugOverlay::DrawLog() {
  ImGui::Checkbox("Debug", &m_logLevels[0]);
  ImGui::SameLine();
  ImGui::Checkbox("Info", &m_logLevels[1]);
  ImGui::SameLine();
  ImGui::Checkbox("Warning", &m_logLevels[2]);
  ImGui::SameLine();
  ImGui::Checkbox("Error", &m_logLevels[3]);
  ImGui::SetNextItemWidth(360.0f);
  ImGui::InputTextWithHint("##LogFilter", "Filter category or message",
                           m_logFilter, sizeof(m_logFilter));
  ImGui::SameLine();
  if (ImGui::Button("Clear")) {
    core::Logger::Instance().ClearRecentEntries();
  }

  const auto entries = core::Logger::Instance().GetRecentEntries();
  ImGui::BeginChild("LogEntries", {0.0f, 0.0f}, ImGuiChildFlags_Borders,
                    ImGuiWindowFlags_HorizontalScrollbar);
  for (const auto &entry : entries) {
    const int level = static_cast<int>(entry.level);
    if (level < 0 || level >= static_cast<int>(m_logLevels.size()) ||
        !m_logLevels[level]) {
      continue;
    }
    if (!ContainsCaseInsensitive(entry.category, m_logFilter) &&
        !ContainsCaseInsensitive(entry.text, m_logFilter)) {
      continue;
    }
    ImGui::TextColored(LogColor(entry.level), "%s", entry.text.c_str());
  }
  ImGui::EndChild();
}

} // namespace game::debug
