#include "src/game/systems/GrassRenderRules.h"
#include "src/game/systems/GrassStreamingRules.h"
#include <cmath>
#include <iostream>
#include <vector>

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

  const int exactGridCount = CalculateGrassGridCellCount(80.0f, 1.0f);
  Check(exactGridCount == 80,
        "grass grid covers an exact multiple of the cell spacing");

  const int roundedGridCount = CalculateGrassGridCellCount(80.1f, 1.0f);
  Check(roundedGridCount == 81 &&
            static_cast<float>(roundedGridCount) >= 80.1f,
        "grass grid rounds up to cover the full field extent");

  const auto centerSamples =
      CalculateGrassNormalSampleRange(0.0f, 0.15f, -40.0f, 40.0f);
  Check(NearlyEqual(centerSamples.lower, -0.15f) &&
            NearlyEqual(centerSamples.upper, 0.15f),
        "grass normals use centered samples inside the field");

  const auto edgeSamples =
      CalculateGrassNormalSampleRange(40.0f, 0.15f, -40.0f, 40.0f);
  Check(NearlyEqual(edgeSamples.lower, 39.85f) &&
            NearlyEqual(edgeSamples.upper, 40.0f),
        "grass normals do not sample beyond the field edge");

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

  Check(NearlyEqual(CalculateGrassShaderFadeEnd(0.88f, 1.0f), 58.0f) &&
            NearlyEqual(CalculateGrassShaderFadeEnd(0.88f, 0.7f), 40.6f),
        "rough grass fade end follows the preset rough fade scale");
  Check(NearlyEqual(CalculateGrassShaderFadeEnd(0.14f, 0.7f), 21.0f) &&
            NearlyEqual(CalculateGrassShaderFadeEnd(0.06f, 0.5f), 17.0f),
        "fairway and green fade ends ignore the rough fade scale");
  Check(NearlyEqual(CalculateGrassShaderFadeEnd(0.88f, 0.7f, 0.5f), 20.3f),
        "the budget draw scale shortens every grass fade end");

  {
    // LOD付き：近距離10三角形、LOD 2三角形、切り替え10m、描画40m
    std::vector<GrassBudgetSample> samples;
    for (int i = 0; i < 40; ++i) {
      GrassBudgetSample sample;
      sample.distance = static_cast<float>(i) + 0.5f;
      sample.lodSwitchDistance = 10.0f;
      sample.maxDrawDistance = 40.0f;
      sample.nearTriangles = 10;
      sample.lodTriangles = 2;
      samples.push_back(sample);
    }
    // 無制限時: 近距離10個×10 + LOD30個×2 = 160
    const auto unlimited = SolveGrassDrawBudget(samples, 0);
    Check(NearlyEqual(unlimited.lodScale, 1.0f) &&
              NearlyEqual(unlimited.drawScale, 1.0f) &&
              unlimited.estimatedTriangles == 160,
          "an unlimited grass budget keeps full distances");

    const auto fits = SolveGrassDrawBudget(samples, 160);
    Check(NearlyEqual(fits.lodScale, 1.0f) && NearlyEqual(fits.drawScale, 1.0f),
          "a budget that already fits keeps full distances");

    const auto densityOnly = SolveGrassDrawBudget(samples, 130);
    Check(densityOnly.lodScale < 1.0f && densityOnly.lodScale >= 0.4f &&
              NearlyEqual(densityOnly.drawScale, 1.0f) &&
              densityOnly.estimatedTriangles <= 130,
          "a small overage lowers mid-distance density before culling");

    const auto culled = SolveGrassDrawBudget(samples, 70);
    Check(culled.drawScale < 1.0f && culled.drawScale >= 0.5f &&
              culled.estimatedTriangles <= 70,
          "a larger overage culls far grass after lowering density");

    const auto floorReached = SolveGrassDrawBudget(samples, 1);
    Check(floorReached.drawScale >= 0.5f && floorReached.lodScale < 0.4f,
          "grass never culls closer than the minimum draw scale");
  }

  {
    GrassGpuLoadController controller;
    UpdateGrassGpuLoad(controller, 0.0f);
    Check(NearlyEqual(controller.budgetScale, 1.0f) &&
              NearlyEqual(controller.smoothedGpuMs, 0.0f),
          "an unmeasured GPU frame leaves the grass budget untouched");

    for (int i = 0; i < 30; ++i) {
      UpdateGrassGpuLoad(controller, 22.0f);
    }
    Check(controller.budgetScale < 0.5f,
          "sustained GPU overload quickly shrinks the grass budget");

    GrassGpuLoadController stalled;
    for (int i = 0; i < 200; ++i) {
      UpdateGrassGpuLoad(stalled, 400.0f);
    }
    Check(NearlyEqual(stalled.smoothedGpuMs, 28.0f) &&
              stalled.budgetScale >= 0.15f,
          "extreme GPU stalls are clamped and never remove all grass");

    const float reduced = controller.budgetScale;
    for (int i = 0; i < 30; ++i) {
      UpdateGrassGpuLoad(controller, 8.0f);
    }
    Check(controller.budgetScale > reduced &&
              controller.budgetScale < reduced + 0.2f,
          "GPU headroom restores the grass budget gradually");
  }

  if (failures == 0) {
    std::cout << "All grass streaming rule tests passed.\n";
  }
  return failures == 0 ? 0 : 1;
}
