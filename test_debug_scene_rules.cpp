#include "src/game/devtools/DebugSceneRules.h"
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
  return 0;
}
