#include "DebugOverlay.h"

#include "DebugTimeController.h"
#include "DebugSceneNavigator.h"
#include "DebugCupInInspector.h"
#include "DebugGameplayInspector.h"
#include "DebugRenderState.h"
#include "../../core/GameContext.h"
#include "../../core/Logger.h"
#include "../../core/SceneManager.h"
#include "../components/PhysicsComponents.h"
#include "../components/WikiComponents.h"
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
  if (!ImGui::Begin("WikiGOLF デバッグ [F1]", &m_visible)) {
    ImGui::End();
    return;
  }

  const char *sceneName = ctx.sceneManager && ctx.sceneManager->Current()
                              ? ctx.sceneManager->Current()->GetName()
                              : "NoScene";
  ImGui::Text("シーン: %s", sceneName);
  const auto *collisionEvents =
      ctx.world.GetGlobal<game::components::CollisionEvents>();
  const bool collisionCaptured =
      m_collisionHistory.Update(collisionEvents, ctx.dt > 0.0f);
  if (collisionCaptured && collisionEvents) {
    for (const auto &event : collisionEvents->events) {
      const ecs::Entity entityA = static_cast<ecs::Entity>(event.entityA);
      const ecs::Entity entityB = static_cast<ecs::Entity>(event.entityB);
      if (MatchesCollisionBreakRule(
              event, m_collisionBreak,
              ctx.world.Has<game::components::GolfHole>(entityA),
              ctx.world.Has<game::components::GolfHole>(entityB),
              ctx.world.Has<game::components::TerrainCollider>(entityA),
              ctx.world.Has<game::components::TerrainCollider>(entityB))) {
        time.SetPaused(true);
        break;
      }
    }
  }

  if (ImGui::BeginTabBar("DebugTabs")) {
    if (ImGui::BeginTabItem("シミュレーション")) {
      DrawSimulation(time);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("ゲーム状態")) {
      DrawGameplayInspector(ctx);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("ホール判定")) {
      DrawCupInInspector(ctx, m_colliderSettings);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("ボール操作")) {
      m_ballInspector.Draw(ctx);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("カメラ")) {
      m_freeCamera.DrawControls();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("ログ")) {
      DrawLog();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("プロファイラー")) {
      m_profilerInspector.Draw();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("衝突")) {
      DrawColliders(ctx, time);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("シーン")) {
      DrawSceneSelector(ctx, time);
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  ImGui::End();
}

void DebugOverlay::DrawSceneSelector(core::GameContext &ctx,
                                     DebugTimeController &time) {
  using Target = DebugSceneTarget;
  const auto navigate = [&](Target target, bool reload = false) {
    time.Reset();
    DebugSceneNavigator::Navigate(ctx, target, reload);
  };
  if (ImGui::Button("タイトル")) {
    navigate(Target::Title);
  }
  ImGui::SameLine();
  if (ImGui::Button("ローディング")) {
    navigate(Target::Loading);
  }
  ImGui::SameLine();
  if (ImGui::Button("ゴルフ（ロード経由）")) {
    navigate(Target::Golf);
  }
  ImGui::SameLine();
  if (ImGui::Button("リザルト")) {
    navigate(Target::Result);
  }
  ImGui::SameLine();
  if (ImGui::Button("設定")) {
    navigate(Target::Settings);
  }

  const char *currentName =
      ctx.sceneManager && ctx.sceneManager->Current()
          ? ctx.sceneManager->Current()->GetName()
          : "";
  const auto currentTarget = SceneTargetFromName(currentName);
  ImGui::BeginDisabled(!currentTarget.has_value());
  if (ImGui::Button("現在のシーンを再読込") && currentTarget) {
    navigate(*currentTarget, true);
  }
  ImGui::EndDisabled();
  if (!currentTarget) {
    ImGui::TextDisabled("現在のシーンは再読込に対応していません。");
  }
}

void DebugOverlay::DrawColliders(core::GameContext &ctx,
                                 DebugTimeController &time) {
  if (ImGui::Checkbox("地形メッシュを隠して当たり判定のみ表示",
                      &m_hideTerrainMeshes) && m_hideTerrainMeshes) {
    m_colliderSettings.enabled = true;
    m_colliderSettings.terrain = true;
  }
  ctx.world.SetGlobal(DebugRenderState{m_hideTerrainMeshes});
  ImGui::Checkbox("コライダーを表示", &m_colliderSettings.enabled);
  ImGui::Checkbox("球", &m_colliderSettings.spheres);
  ImGui::SameLine();
  ImGui::Checkbox("ボックス", &m_colliderSettings.boxes);
  ImGui::SameLine();
  ImGui::Checkbox("円柱", &m_colliderSettings.cylinders);
  ImGui::Checkbox("地形境界", &m_colliderSettings.terrain);
  ImGui::SameLine();
  ImGui::Checkbox("地形マテリアル", &m_colliderSettings.terrainMaterials);
  ImGui::SameLine();
  ImGui::Checkbox("ゴールホール", &m_colliderSettings.holes);
  ImGui::Checkbox("Entity ID", &m_colliderSettings.entityIds);
  ImGui::Checkbox("接触点", &m_colliderSettings.contactPoints);
  ImGui::SameLine();
  ImGui::Checkbox("衝突法線", &m_colliderSettings.collisionNormals);
  ImGui::Checkbox("ボール速度ベクトル", &m_colliderSettings.velocityVector);
  ImGui::Checkbox("ボール軌跡", &m_colliderSettings.ballTrail);
  ImGui::Checkbox("ホールイン判定範囲", &m_colliderSettings.cupInGuide);
  if (m_colliderSettings.ballTrail) {
    ImGui::SetNextItemWidth(180.0f);
    ImGui::SliderInt("軌跡点数", &m_colliderSettings.trailMaximumPoints, 30,
                     600);
    ImGui::SetNextItemWidth(180.0f);
    ImGui::SliderInt("サンプル間隔（フレーム）",
                     &m_colliderSettings.trailSampleInterval, 1, 10);
    if (ImGui::Button("軌跡を消去")) {
      ++m_colliderSettings.trailClearGeneration;
    }
  }
  ImGui::TextColored({0.3f, 0.9f, 0.4f, 1.0f}, "緑: コライダー");
  ImGui::SameLine();
  ImGui::TextColored({1.0f, 0.25f, 0.25f, 1.0f}, "赤: 衝突中");
  ImGui::SameLine();
  ImGui::TextColored({1.0f, 0.85f, 0.2f, 1.0f}, "黄: ホール");
  ImGui::SameLine();
  ImGui::TextColored({1.0f, 0.88f, 0.15f, 1.0f}, "黄線: 速度");
  ImGui::SameLine();
  ImGui::TextColored({0.15f, 0.85f, 1.0f, 1.0f}, "水色: 軌跡");
  if (m_colliderSettings.terrainMaterials) {
    ImGui::Text("地形色: 緑=Fairway / 黄緑=Rough / 黄=Bunker / 明緑=Green");
    ImGui::Text("水色=Ice / 青=Water / 赤=Lava / 灰=Stone");
  }
  ImGui::Checkbox("条件一致時に一時停止", &m_collisionBreak.enabled);
  ImGui::SameLine();
  ImGui::TextDisabled("現在: %s", time.IsPaused() ? "停止中" : "実行中");
  if (m_collisionBreak.enabled) {
    ImGui::Checkbox("任意の衝突", &m_collisionBreak.anyCollision);
    ImGui::SameLine();
    ImGui::Checkbox("ホール", &m_collisionBreak.hole);
    ImGui::SameLine();
    ImGui::Checkbox("地形", &m_collisionBreak.terrain);
    ImGui::Checkbox("指定Entity", &m_collisionBreak.entity);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    ImGui::InputScalar("##BreakEntity", ImGuiDataType_U32,
                       &m_collisionBreak.entityId);
    if (!m_collisionBreak.anyCollision && !m_collisionBreak.hole &&
        !m_collisionBreak.terrain && !m_collisionBreak.entity) {
      ImGui::TextDisabled("停止条件が1つも選択されていません。");
    }
  }

  const auto *events =
      ctx.world.GetGlobal<game::components::CollisionEvents>();
  ImGui::SeparatorText("現在フレームの衝突イベント");
  if (!events) {
    ImGui::TextDisabled("衝突イベントリソースはありません。");
  } else {
    ImGui::Text("接触数: %zu", events->events.size());
  }
  if (events && ImGui::BeginTable("CollisionEvents", 4,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
    ImGui::TableSetupColumn("Entity A");
    ImGui::TableSetupColumn("Entity B");
    ImGui::TableSetupColumn("接触点");
    ImGui::TableSetupColumn("貫通量");
    ImGui::TableHeadersRow();
    for (const auto &event : events->events) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::Text("%u", event.entityA);
      ImGui::TableSetColumnIndex(1);
      ImGui::Text("%u", event.entityB);
      ImGui::TableSetColumnIndex(2);
      ImGui::Text("%.2f, %.2f, %.2f", event.contactPoint.x,
                  event.contactPoint.y, event.contactPoint.z);
      ImGui::TableSetColumnIndex(3);
      ImGui::Text("%.4f", event.penetrationDepth);
    }
    ImGui::EndTable();
  }

  ImGui::SeparatorText("衝突履歴（直近100件）");
  ImGui::Text("記録数: %zu", m_collisionHistory.Records().size());
  ImGui::SameLine();
  if (ImGui::Button("履歴を消去")) {
    m_collisionHistory.Clear();
  }
  if (ImGui::BeginTable("CollisionHistory", 5,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_ScrollY,
                        {0.0f, 190.0f})) {
    ImGui::TableSetupColumn("Frame");
    ImGui::TableSetupColumn("Entity A");
    ImGui::TableSetupColumn("Entity B");
    ImGui::TableSetupColumn("接触点");
    ImGui::TableSetupColumn("貫通量");
    ImGui::TableHeadersRow();
    const auto &records = m_collisionHistory.Records();
    for (auto record = records.rbegin(); record != records.rend(); ++record) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::Text("%llu",
                  static_cast<unsigned long long>(record->simulationFrame));
      ImGui::TableSetColumnIndex(1);
      ImGui::Text("%u", record->event.entityA);
      ImGui::TableSetColumnIndex(2);
      ImGui::Text("%u", record->event.entityB);
      ImGui::TableSetColumnIndex(3);
      ImGui::Text("%.2f, %.2f, %.2f", record->event.contactPoint.x,
                  record->event.contactPoint.y, record->event.contactPoint.z);
      ImGui::TableSetColumnIndex(4);
      ImGui::Text("%.4f", record->event.penetrationDepth);
    }
    ImGui::EndTable();
  }
}

void DebugOverlay::DrawSimulation(DebugTimeController &time) {
  bool paused = time.IsPaused();
  if (ImGui::Checkbox("一時停止 [F5]", &paused)) {
    time.SetPaused(paused);
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(!time.IsPaused());
  if (ImGui::Button("1フレーム進める [F6]")) {
    time.RequestStep();
  }
  ImGui::EndDisabled();

  ImGui::SeparatorText("時間倍率 [F7で切替]");
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
  ImGui::Checkbox("デバッグ", &m_logLevels[0]);
  ImGui::SameLine();
  ImGui::Checkbox("情報", &m_logLevels[1]);
  ImGui::SameLine();
  ImGui::Checkbox("警告", &m_logLevels[2]);
  ImGui::SameLine();
  ImGui::Checkbox("エラー", &m_logLevels[3]);
  ImGui::SetNextItemWidth(360.0f);
  ImGui::InputTextWithHint("##LogFilter", "カテゴリまたはメッセージを絞り込み",
                           m_logFilter, sizeof(m_logFilter));
  ImGui::SameLine();
  if (ImGui::Button("消去")) {
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
