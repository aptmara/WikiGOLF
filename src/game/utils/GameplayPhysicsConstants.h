#pragma once

namespace game::physics {

// builtin/sphere has a radius of 0.5, so a uniform scale of 0.08 produces
// the visible gameplay radius below.
constexpr float kBallVisualScale = 0.08f;
constexpr float kBallRadius = kBallVisualScale * 0.5f;

// The article texture overlay is the surface the player actually sees.
// Physics, placement, prediction, and contact effects use the same height.
constexpr float kTerrainVisualSurfaceOffset = 0.02f;

constexpr float kMaxSimulationDeltaTime = 0.033f;

inline float ToVisualSurfaceHeight(float terrainHeight) {
  return terrainHeight + kTerrainVisualSurfaceOffset;
}

} // namespace game::physics
