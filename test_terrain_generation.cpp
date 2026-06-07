#include "src/game/systems/TerrainGenerator.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>

#define CHECK(condition, message)                                              \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << "[FAIL] " << message << "\n";                              \
      std::exit(1);                                                            \
    }                                                                          \
    std::cout << "[PASS] " << message << "\n";                                \
  } while (0)

namespace {

constexpr uint8_t kWater = 5;
constexpr uint8_t kLava = 6;

int ToGridX(const game::systems::TerrainData &data, float worldX) {
  float u = worldX / data.config.worldWidth + 0.5f;
  return std::clamp(static_cast<int>(u * (data.config.resolutionX - 1)), 0,
                    data.config.resolutionX - 1);
}

int ToGridZ(const game::systems::TerrainData &data, float worldZ) {
  float v = 0.5f - worldZ / data.config.worldDepth;
  return std::clamp(static_cast<int>(v * (data.config.resolutionZ - 1)), 0,
                    data.config.resolutionZ - 1);
}

} // namespace

int main() {
  game::systems::TerrainConfig config;
  config.resolutionX = 64;
  config.resolutionZ = 96;
  config.worldWidth = 80.0f;
  config.worldDepth = 120.0f;
  config.heightScale = 1.7f;
  config.biome = 0;

  std::vector<DirectX::XMFLOAT2> holes = {
      {-18.0f, 34.0f}, {14.0f, 5.0f}, {-8.0f, -32.0f}};

  auto data =
      game::systems::TerrainGenerator::GenerateTerrain("Course variety seed",
                                                       holes, config);
  CHECK(data.materialMap.size() ==
            static_cast<size_t>(config.resolutionX * config.resolutionZ),
        "Material map has one entry per terrain vertex");
  CHECK(!data.vertices.empty() && !data.indices.empty(),
        "Terrain mesh is generated");

  std::array<int, 8> counts{};
  for (uint8_t mat : data.materialMap) {
    if (mat < counts.size()) {
      ++counts[mat];
    }
  }

  CHECK(counts[0] > 0, "Fairway is present");
  CHECK(counts[1] > 0, "Rough is present");
  CHECK(counts[2] > 0, "Bunker is present");
  CHECK(counts[3] > 0, "Green is present");

  for (const auto &hole : holes) {
    int gx = ToGridX(data, hole.x);
    int gz = ToGridZ(data, hole.y);
    uint8_t mat = data.materialMap[gz * config.resolutionX + gx];
    CHECK(mat == 3, "Hole center is converted to green");
  }

  int variedSeedsWithHazards = 0;
  for (int biome = 0; biome < 4; ++biome) {
    config.biome = biome;
    auto themed = game::systems::TerrainGenerator::GenerateTerrain(
        "Course variety biome " + std::to_string(biome), holes, config);
    std::array<int, 8> themedCounts{};
    for (uint8_t mat : themed.materialMap) {
      if (mat < themedCounts.size()) {
        ++themedCounts[mat];
      }
    }
    int unique = 0;
    for (int count : themedCounts) {
      if (count > 0) {
        ++unique;
      }
    }
    if (themedCounts[2] + themedCounts[4] + themedCounts[5] +
            themedCounts[6] + themedCounts[7] >
        0) {
      ++variedSeedsWithHazards;
    }
    int obCount = themedCounts[kWater] + themedCounts[kLava];
    if (biome == 0 || biome == 1) {
      CHECK(obCount == 0,
            "Safe or dry biomes do not generate water/lava OB hazards");
    } else {
      CHECK(obCount > 0,
            "Ice and rocky biomes generate natural OB hazard terrain");
    }
    CHECK(unique >= 4, "Biome course keeps at least four terrain types");
  }

  CHECK(variedSeedsWithHazards >= 3,
        "Most biome variants include hazard or gimmick terrain");
  std::cout << "All terrain generation tests passed!\n";
  return 0;
}
