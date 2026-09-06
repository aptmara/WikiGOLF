#pragma once

namespace game::physics {

// Physics/hit-detection radius. Kept independent of kBallVisualScale below so
// the ball can be made to look bigger or smaller without changing collision,
// placement, or prediction behavior (all of which use this constant).
constexpr float kBallRadius = 0.04f;

// builtin/sphere has a radius of 0.5, so a uniform scale of 0.08 would make
// the ball's visible size match kBallRadius exactly. Deliberately larger than
// that here so the ball reads clearly on screen while the actual hit
// detection stays at kBallRadius above.
constexpr float kBallVisualScale = 0.128f;

// The article texture overlay is the surface the player actually sees.
// Physics, placement, prediction, and contact effects use the same height.
constexpr float kTerrainVisualSurfaceOffset = 0.02f;

constexpr float kMaxSimulationDeltaTime = 0.033f;

inline float ToVisualSurfaceHeight(float terrainHeight) {
  return terrainHeight + kTerrainVisualSurfaceOffset;
}

} // namespace game::physics
