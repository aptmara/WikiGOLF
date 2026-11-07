#include "src/game/systems/GrassFieldRules.h"
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

bool NearlyEqual(float a, float b, float tolerance = 0.0001f) {
  return std::abs(a - b) < tolerance;
}

} // namespace

int main() {
  using namespace game::systems;
  using core::GraphicsPreset;

  Check(!GetGrassLayerParams(GraphicsPreset::Low, GrassLayer::Rough).enabled &&
            !GetGrassLayerParams(GraphicsPreset::Low, GrassLayer::Turf).enabled,
        "low preset draws no grass");

  const GraphicsPreset presets[] = {GraphicsPreset::Medium, GraphicsPreset::High,
                                    GraphicsPreset::ExHigh, GraphicsPreset::Ultra};
  float previousRough = 0.0f;
  float previousTurf = 0.0f;
  for (GraphicsPreset preset : presets) {
    const auto rough = GetGrassLayerParams(preset, GrassLayer::Rough);
    const auto turf = GetGrassLayerParams(preset, GrassLayer::Turf);
    Check(rough.enabled && turf.enabled, "medium and above draw both layers");
    Check(rough.baseDensity > previousRough && turf.baseDensity > previousTurf,
          "higher presets are denser");
    Check(turf.baseDensity > rough.baseDensity,
          "cut turf is denser than rough");
    Check(rough.maxBladeHeight > turf.maxBladeHeight,
          "rough blades are taller than turf");
    Check(GrassLodCount(rough) <= kGrassMaxLodCount &&
              GrassLodCount(turf) <= kGrassMaxLodCount,
          "lod count fits the draw regions");
    previousRough = rough.baseDensity;
    previousTurf = turf.baseDensity;
  }

  const auto ultraTurf = GetGrassLayerParams(GraphicsPreset::Ultra, GrassLayer::Turf);
  const uint32_t base = GrassBaseBladesPerTile(ultraTurf);
  Check(base % kGrassLeavesPerShoot == 0, "blades come in whole shoots");
  Check(base >= static_cast<uint32_t>(ultraTurf.baseDensity),
        "rounding to shoots never lowers the density");
  for (uint32_t lod = 1; lod < kGrassMaxLodCount; ++lod) {
    Check(GrassBladesForLod(ultraTurf, lod) <= GrassBladesForLod(ultraTurf, lod - 1),
          "each lod draws a prefix no longer than the previous one");
    Check(GrassBladesForLod(ultraTurf, lod) % kGrassLeavesPerShoot == 0,
          "each lod draws whole shoots");
  }

  // LOD k は、そのタイルの最も近い点での目標密度以上の葉を含む
  const float r0 = ultraTurf.fullDensityDistance;
  const uint32_t lodCount = GrassLodCount(ultraTurf);
  for (float distance = 0.5f; distance < ultraTurf.maxDistance; distance += 0.37f) {
    const uint32_t lod = GrassLodForDistance(distance, r0, lodCount);
    const float drawn = static_cast<float>(GrassBladesForLod(ultraTurf, lod)) /
                        static_cast<float>(base);
    Check(drawn + 1.0e-4f >= GrassDensityFraction(distance, r0),
          "chosen lod keeps enough blades for the target density");
  }
  Check(GrassLodForDistance(r0 * 0.5f, r0, lodCount) == 0,
        "inside r0 uses the full lod");
  Check(GrassLodForDistance(1000.0f, r0, lodCount) == lodCount - 1,
        "far tiles clamp to the last lod");

  Check(NearlyEqual(GrassDensityFraction(1.0f, 2.0f), 1.0f),
        "full density inside r0");
  Check(NearlyEqual(GrassDensityFraction(4.0f, 2.0f), 0.25f),
        "density falls with the square of distance");

  Check(GrassVerticesPerBlade(0) == 7 && GrassIndicesPerBlade(0) == 15,
        "nearest blades have three segments");
  Check(GrassVerticesPerBlade(1) == 5 && GrassIndicesPerBlade(1) == 9,
        "second lod has two segments");
  Check(GrassVerticesPerBlade(5) == 3 && GrassIndicesPerBlade(5) == 3,
        "far blades are single triangles");
  Check(GrassShapeLod(GrassLayer::Rough, 0) == 0 && GrassShapeLod(GrassLayer::Rough, 1) == 1 &&
            GrassShapeLod(GrassLayer::Rough, 6) == 2,
        "rough blades follow the lod shape");
  Check(GrassShapeLod(GrassLayer::Turf, 0) == 2 && GrassShapeLod(GrassLayer::Turf, 4) == 2,
        "short turf blades are always single triangles");

  Check(NearlyEqual(GrassWidthScaleForDensity(1.0f), 1.0f) &&
            NearlyEqual(GrassWidthScaleForDensity(1.0f / 64.0f), kGrassMaxWidthScale) &&
            GrassWidthScaleForDensity(0.25f) > 2.8f,
        "far blades widen to keep coverage, up to the cap");

  Check(NearlyEqual(GrassDistanceScaleForBudget(1.0f), 1.0f) &&
            NearlyEqual(GrassDistanceScaleForBudget(0.25f), 0.5f),
        "budget scale maps to r0 by square root");

  GrassGpuLoadController controller;
  for (int i = 0; i < 120; ++i) {
    UpdateGrassGpuLoad(controller, 25.0f);
  }
  Check(controller.budgetScale < 0.5f, "heavy gpu frames reduce the budget");
  const float lowered = controller.budgetScale;
  UpdateGrassGpuLoad(controller, 0.0f);
  Check(NearlyEqual(controller.budgetScale, lowered),
        "unmeasured frames leave the budget unchanged");
  for (int i = 0; i < 2000; ++i) {
    UpdateGrassGpuLoad(controller, 5.0f);
  }
  Check(NearlyEqual(controller.budgetScale, 1.0f),
        "budget recovers fully with headroom");

  if (failures == 0) {
    std::cout << "grass field rules: all tests passed\n";
  }
  return failures == 0 ? 0 : 1;
}
