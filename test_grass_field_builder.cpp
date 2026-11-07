#include "src/game/systems/GrassFieldBuilder.h"
#include "src/game/systems/GrassFieldRules.h"
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

constexpr uint8_t kFairway = 0;
constexpr uint8_t kRough = 1;
constexpr uint8_t kBunker = 2;
constexpr uint8_t kGreen = 3;

/** @brief 40m 四方、x < -10 がラフ、-4 < x < 4 かつ z > 10 がバンカー、x > 12 がグリーン */
struct TestTerrain {
  int resolution = 81;
  float size = 40.0f;
  std::vector<float> heights;
  std::vector<uint8_t> materials;

  TestTerrain() {
    heights.resize(static_cast<size_t>(resolution) * resolution);
    materials.resize(heights.size());
    for (int gz = 0; gz < resolution; ++gz) {
      for (int gx = 0; gx < resolution; ++gx) {
        const float x = -size * 0.5f + size * gx / (resolution - 1);
        const float z = size * 0.5f - size * gz / (resolution - 1);
        const size_t i = static_cast<size_t>(gz) * resolution + gx;
        heights[i] = std::sin(x * 0.3f) * 1.5f + z * 0.05f;
        uint8_t material = kFairway;
        if (x < -10.0f) material = kRough;
        if (x > -4.0f && x < 4.0f && z > 10.0f) material = kBunker;
        if (x > 12.0f) material = kGreen;
        materials[i] = material;
      }
    }
  }

  game::systems::GrassFieldTerrainView View() const {
    game::systems::GrassFieldTerrainView view;
    view.heights = heights.data();
    view.materials = materials.data();
    view.resolutionX = resolution;
    view.resolutionZ = resolution;
    view.width = size;
    view.depth = size;
    return view;
  }
};

} // namespace

int main() {
  using namespace game::systems;
  const TestTerrain terrain;

  Check(BuildGrassField(GrassFieldTerrainView{}) == nullptr,
        "an empty terrain builds no field");

  const auto field = BuildGrassField(terrain.View());
  Check(field != nullptr, "a valid terrain builds a field");
  if (!field) {
    return 1;
  }
  Check(field->fieldResolutionX == 100 && field->fieldResolutionZ == 100,
        "field texels are 0.4m");

  const auto rough = SampleGrassFieldTexel(*field, -16.0f, 0.0f);
  Check(rough.rough > 0.95f && rough.allowed > 0.95f && rough.turf < 0.05f,
        "rough grows full rough");
  Check(GrassRoughLayerDensity(rough) > 0.95f, "rough layer is dense on rough");

  const auto fairway = SampleGrassFieldTexel(*field, 5.0f, 0.0f);
  Check(fairway.turf > 0.95f && fairway.rough < 0.05f && fairway.green < 0.05f,
        "fairway far from rough is pure turf");
  Check(GrassTurfLayerDensity(fairway) > 0.95f, "turf layer is dense on fairway");

  const auto semi = SampleGrassFieldTexel(*field, -9.6f, 0.0f);
  Check(semi.rough > 0.3f && semi.rough < 0.6f && semi.turf > 0.5f,
        "fairway next to rough gets a semi-rough band");
  const auto pastBand = SampleGrassFieldTexel(*field, -7.0f, 0.0f);
  Check(pastBand.rough < 0.05f, "semi-rough band ends within a few metres");

  const auto bunker = SampleGrassFieldTexel(*field, 0.0f, 15.0f);
  Check(bunker.allowed < 0.05f, "no grass grows in the bunker");
  Check(GrassRoughLayerDensity(bunker) < 0.05f && GrassTurfLayerDensity(bunker) < 0.05f,
        "both layers are empty in the bunker");
  const auto bunkerEdge = SampleGrassFieldTexel(*field, 4.1f, 15.0f);
  Check(bunkerEdge.allowed > 0.2f && bunkerEdge.allowed < 0.99f,
        "grass thins out towards the bunker edge");
  for (float x = -3.6f; x <= 3.6f; x += 0.4f) {
    Check(SampleGrassFieldTexel(*field, x, 15.0f).allowed < 0.01f &&
              SampleGrassFieldTexel(*field, x, 10.9f).allowed < 0.01f,
          "softening the edge never spreads grass into the bunker");
  }

  const auto green = SampleGrassFieldTexel(*field, 16.0f, 0.0f);
  Check(green.green > 0.95f && green.turf > 0.95f, "green is short turf");

  Check(SampleGrassFieldTexel(*field, 100.0f, 0.0f).allowed == 0.0f,
        "outside the field there is no grass");

  // CPU の地形の高さ（WikiTerrainSystem::GetHeight）と同じ双線形補間
  for (float z = -19.0f; z < 19.0f; z += 3.7f) {
    for (float x = -19.0f; x < 19.0f; x += 2.9f) {
      const float u = x / terrain.size + 0.5f;
      const float v = 0.5f - z / terrain.size;
      const float fx = u * (terrain.resolution - 1);
      const float fz = v * (terrain.resolution - 1);
      const int ix = std::min(static_cast<int>(fx), terrain.resolution - 2);
      const int iz = std::min(static_cast<int>(fz), terrain.resolution - 2);
      const float dx = fx - ix;
      const float dz = fz - iz;
      const auto h = [&](int px, int pz) {
        return terrain.heights[static_cast<size_t>(pz) * terrain.resolution + px];
      };
      const float expected =
          (h(ix, iz) * (1 - dx) + h(ix + 1, iz) * dx) * (1 - dz) +
          (h(ix, iz + 1) * (1 - dx) + h(ix + 1, iz + 1) * dx) * dz;
      Check(std::abs(SampleGrassFieldHeight(*field, x, z) - expected) < 1.0e-4f,
            "grass height matches the terrain height");
    }
  }

  // タイルは密度と高さの範囲を持ち、葉の根本の高さを含む
  const auto &roughTiles = field->layers[static_cast<uint32_t>(GrassLayer::Rough)];
  const auto &turfTiles = field->layers[static_cast<uint32_t>(GrassLayer::Turf)];
  Check(roughTiles.tileSize == 2.0f && roughTiles.resolutionX == 20,
        "rough tiles are 2m");
  Check(turfTiles.tileSize == 1.0f && turfTiles.resolutionX == 40,
        "turf tiles are 1m");
  const auto tileAt = [&](const game::components::GrassTileGrid &grid, float x, float z) {
    const int tx = static_cast<int>((x - field->minX) / grid.tileSize);
    const int tz = static_cast<int>((z - field->minZ) / grid.tileSize);
    return grid.tiles[static_cast<size_t>(tz) * grid.resolutionX + tx];
  };
  Check(tileAt(roughTiles, -16.0f, 0.0f).z > 0.95f, "rough tile on rough is dense");
  Check(tileAt(roughTiles, 5.0f, 0.0f).z < 0.05f, "rough tile on fairway is empty");
  Check(tileAt(turfTiles, 5.0f, 0.0f).z > 0.95f, "turf tile on fairway is dense");
  Check(tileAt(turfTiles, 0.0f, 15.0f).z < 0.05f, "turf tile in the bunker is empty");
  for (float z = -18.5f; z < 18.5f; z += 1.3f) {
    for (float x = -18.5f; x < 18.5f; x += 1.1f) {
      const auto tile = tileAt(turfTiles, x, z);
      if (tile.z <= 0.0f) {
        continue;
      }
      const float height = SampleGrassFieldHeight(*field, x, z);
      Check(height >= tile.x - 1.0e-4f && height <= tile.y + 1.0e-4f,
            "tile height range contains the ground");
    }
  }

  const auto again = BuildGrassField(terrain.View());
  Check(again->fieldTexels == field->fieldTexels, "building is deterministic");

  GrassFieldBuildOptions title;
  title.everythingIsRough = true;
  title.heightOffset = -0.5f;
  const auto titleField = BuildGrassField(terrain.View(), title);
  const auto titleBunker = SampleGrassFieldTexel(*titleField, 0.0f, 15.0f);
  Check(titleBunker.rough > 0.95f && titleBunker.allowed > 0.95f,
        "title field grows rough everywhere");
  Check(std::abs(SampleGrassFieldHeight(*titleField, 3.0f, 4.0f) -
                 (SampleGrassFieldHeight(*field, 3.0f, 4.0f) - 0.5f)) < 1.0e-4f,
        "height offset shifts the ground");

  Check(std::abs(GrassSemiRoughFactor(0.0f) - 0.55f) < 1.0e-4f &&
            GrassSemiRoughFactor(5.0f) == 0.0f,
        "semi-rough factor fades with distance");

  if (failures == 0) {
    std::cout << "grass field builder: all tests passed\n";
  }
  return failures == 0 ? 0 : 1;
}
