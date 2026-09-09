#include "src/game/systems/GrassStreamingRules.h"
#include <cmath>
#include <iostream>

namespace {

int failures = 0;

void Check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

bool NearlyEqual(float a, float b) {
  return std::abs(a - b) < 0.0001f;
}

} // namespace

int main() {
  using namespace game::systems;

  const auto largeBounds = CalculateGrassStreamingBounds(
      1000.0f, 1200.0f, 100.0f, -80.0f, 60.0f, 24.0f);
  Check(NearlyEqual(largeBounds.minX, 16.0f) &&
            NearlyEqual(largeBounds.maxX, 184.0f) &&
            NearlyEqual(largeBounds.minZ, -164.0f) &&
            NearlyEqual(largeBounds.maxZ, 4.0f),
        "large fields keep a view-sized streaming window");

  const auto smallBounds = CalculateGrassStreamingBounds(
      80.0f, 120.0f, 0.0f, 0.0f, 60.0f, 24.0f);
  Check(NearlyEqual(smallBounds.minX, -40.0f) &&
            NearlyEqual(smallBounds.maxX, 40.0f) &&
            NearlyEqual(smallBounds.minZ, -60.0f) &&
            NearlyEqual(smallBounds.maxZ, 60.0f),
        "small fields clamp the window to field bounds");

  const uint32_t cellSeed = MakeGrassCellSeed(1234u, 18, 27);
  Check(cellSeed == MakeGrassCellSeed(1234u, 18, 27),
        "the same global grass cell keeps the same seed");
  Check(cellSeed != MakeGrassCellSeed(1234u, 18, 28),
        "neighboring grass cells receive different seeds");
  Check(GrassRandom01(cellSeed, 0) >= 0.0f &&
            GrassRandom01(cellSeed, 0) < 1.0f &&
            GrassRandom01(cellSeed, 0) != GrassRandom01(cellSeed, 1),
        "fast grass random samples are deterministic and normalized");

  const uint64_t chunkKey = MakeGrassStreamChunkKey(-17, 29);
  Check(GrassStreamChunkX(chunkKey) == -17 &&
            GrassStreamChunkZ(chunkKey) == 29,
        "signed grass chunk coordinates round-trip through their key");

  if (failures == 0) {
    std::cout << "All grass streaming rule tests passed.\n";
  }
  return failures == 0 ? 0 : 1;
}
