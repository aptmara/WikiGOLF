#pragma once

#include "DebugSceneRules.h"

namespace core {
struct GameContext;
}

namespace game::debug {

class DebugSceneNavigator {
public:
  static void Navigate(core::GameContext &ctx, DebugSceneTarget target,
                       bool replaceCurrent = false);
};

} // namespace game::debug
