#include "src/game/devtools/DebugSceneRules.h"
#include "src/game/devtools/DebugTimeController.h"
#include "src/game/systems/PostProcessSystem.h"
#include <cmath>
#include <iostream>

int main() {
  using game::debug::DebugSceneTarget;
  const auto title = game::debug::SceneTargetFromName("TitleScene");
  const auto loading = game::debug::SceneTargetFromName("LoadingScene");
  const auto golf = game::debug::SceneTargetFromName("WikiGolfScene");
  const auto result = game::debug::SceneTargetFromName("ResultScene");
  const auto settings = game::debug::SceneTargetFromName("SettingsScene");
  const auto unknown = game::debug::SceneTargetFromName("UnknownScene");
  if (title != DebugSceneTarget::Title ||
      loading != DebugSceneTarget::Loading ||
      golf != DebugSceneTarget::Golf || result != DebugSceneTarget::Result ||
      settings != DebugSceneTarget::Settings || unknown.has_value()) {
    std::cerr << "Scene reload mapping failed\n";
    return 1;
  }

  if (game::debug::EntryTarget(DebugSceneTarget::Golf) !=
          DebugSceneTarget::Loading ||
      game::debug::EntryTarget(DebugSceneTarget::Result) !=
          DebugSceneTarget::Result ||
      !game::debug::RequiresCleanReset(DebugSceneTarget::Title, false) ||
      !game::debug::RequiresCleanReset(DebugSceneTarget::Golf, true) ||
      game::debug::RequiresCleanReset(DebugSceneTarget::Settings, false)) {
    std::cerr << "Safe scene transition policy failed\n";
    return 1;
  }

  game::debug::DebugTimeController time;
  time.SetPaused(true);
  time.SetTimeScaleIndex(4);
  time.RequestStep();
  time.Reset();
  if (time.IsPaused() || std::abs(time.GetTimeScale() - 1.0f) > 0.0001f ||
      std::abs(time.SimulationDelta(0.25f) - 0.25f) > 0.0001f) {
    std::cerr << "Scene transition did not reset debug time\n";
    return 1;
  }

  game::systems::PostProcessSystem postProcess;
  postProcess.SetFog({0.0f, 0.0f, 0.0f}, 1.0f, 0.0f, 1.0f);
  postProcess.ResetToDefaults();
  const auto &constants = postProcess.GetConstants();
  if (constants.fogColor.w != 0.0f || constants.colorTint.w != 1.0f) {
    std::cerr << "Scene transition did not restore post-process defaults\n";
    return 1;
  }
  return 0;
}
