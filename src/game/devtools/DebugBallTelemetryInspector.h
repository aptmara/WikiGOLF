#pragma once

#include "DebugBallTelemetryHistory.h"

namespace core {
struct GameContext;
}

namespace game::debug {

class DebugBallTelemetryInspector {
public:
  void Update(core::GameContext &ctx, bool simulationAdvanced);
  void Draw();

private:
  DebugBallTelemetryHistory m_history;
};

} // namespace game::debug
