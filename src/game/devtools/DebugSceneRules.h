#pragma once

#include <optional>
#include <string_view>

namespace game::debug {

enum class DebugSceneTarget { Title, Loading, Golf, Result, Settings };

inline DebugSceneTarget EntryTarget(DebugSceneTarget target) {
  if (target == DebugSceneTarget::Golf) {
    return DebugSceneTarget::Loading;
  }
  return target;
}

inline bool RequiresCleanReset(DebugSceneTarget target, bool) {
  return target != DebugSceneTarget::Settings;
}

inline std::optional<DebugSceneTarget>
SceneTargetFromName(std::string_view sceneName) {
  if (sceneName == "TitleScene") {
    return DebugSceneTarget::Title;
  }
  if (sceneName == "LoadingScene") {
    return DebugSceneTarget::Loading;
  }
  if (sceneName == "WikiGolfScene") {
    return DebugSceneTarget::Golf;
  }
  if (sceneName == "ResultScene") {
    return DebugSceneTarget::Result;
  }
  if (sceneName == "SettingsScene") {
    return DebugSceneTarget::Settings;
  }
  return std::nullopt;
}

} // namespace game::debug
