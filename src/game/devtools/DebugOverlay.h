#pragma once

#include <array>
#include "DebugColliderRenderer.h"
#include "DebugBallInspector.h"
#include "DebugCollisionHistory.h"
#include "DebugCollisionBreakRules.h"
#include "DebugFreeCamera.h"
#include "DebugEntityInspector.h"
#include "DebugCheckpointInspector.h"
#include "DebugProfilerInspector.h"

namespace core {
struct GameContext;
}

namespace game::debug {

class DebugTimeController;

class DebugOverlay {
public:
  void Toggle() { m_visible = !m_visible; }
  bool IsVisible() const { return m_visible; }
  void Draw(core::GameContext &ctx, DebugTimeController &time);
  void ApplyFreeCamera(core::GameContext &ctx, float realDeltaSeconds) {
    m_freeCamera.Apply(ctx, realDeltaSeconds);
  }
  const DebugColliderSettings &GetColliderSettings() const {
    return m_colliderSettings;
  }

private:
  void DrawSimulation(DebugTimeController &time);
  void DrawLog();
  void DrawColliders(core::GameContext &ctx, DebugTimeController &time);
  void DrawSceneSelector(core::GameContext &ctx, DebugTimeController &time);

  bool m_visible = false;
  std::array<bool, 4> m_logLevels = {true, true, true, true};
  char m_logFilter[128] = {};
  DebugColliderSettings m_colliderSettings;
  DebugBallInspector m_ballInspector;
  DebugCollisionHistory m_collisionHistory;
  DebugFreeCamera m_freeCamera;
  DebugEntityInspector m_entityInspector;
  DebugCheckpointInspector m_checkpointInspector;
  DebugProfilerInspector m_profilerInspector;
  DebugCollisionBreakSettings m_collisionBreak;
  bool m_hideTerrainMeshes = false;
};

} // namespace game::debug
