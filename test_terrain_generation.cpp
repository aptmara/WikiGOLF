#include "src/game/systems/TerrainGenerator.h"
#include <algorithm>
#include <array>
#include <cmath>
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

constexpr uint8_t kFairway = 0;
constexpr uint8_t kRough = 1;
constexpr uint8_t kBunker = 2;
constexpr uint8_t kGreen = 3;

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

uint8_t MaterialAt(const game::systems::TerrainData &data, float worldX,
                   float worldZ) {
  const int gx = ToGridX(data, worldX);
  const int gz = ToGridZ(data, worldZ);
  return data.materialMap[gz * data.config.resolutionX + gx];
}

int CountIsolatedHazardCells(const game::systems::TerrainData &data) {
  const int resX = data.config.resolutionX;
  const int resZ = data.config.resolutionZ;
  int isolated = 0;
  for (int z = 1; z < resZ - 1; ++z) {
    for (int x = 1; x < resX - 1; ++x) {
      uint8_t material = data.materialMap[z * resX + x];
      if (material < kBunker || material == kGreen) {
        continue;
      }

      int sameNeighbors = 0;
      for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
          if (dx == 0 && dz == 0) {
            continue;
          }
          if (data.materialMap[(z + dz) * resX + (x + dx)] == material) {
            ++sameNeighbors;
          }
        }
      }
      if (sameNeighbors == 0) {
        ++isolated;
      }
    }
  }
  return isolated;
}

float MaxAdjacentHeightDelta(const game::systems::TerrainData &data) {
  const int resX = data.config.resolutionX;
  const int resZ = data.config.resolutionZ;
  float maxDelta = 0.0f;
  for (int z = 0; z < resZ; ++z) {
    for (int x = 0; x < resX; ++x) {
      const float height = data.heightMap[z * resX + x];
      if (x + 1 < resX) {
        maxDelta = std::max(
            maxDelta,
            std::abs(height - data.heightMap[z * resX + x + 1]));
      }
      if (z + 1 < resZ) {
        maxDelta = std::max(
            maxDelta,
            std::abs(height - data.heightMap[(z + 1) * resX + x]));
      }
    }
  }
  return maxDelta;
}

float VisualColorDistance(const DirectX::XMFLOAT3 &a,
                          const DirectX::XMFLOAT3 &b) {
  float dx = a.x - b.x;
  float dy = a.y - b.y;
  float dz = a.z - b.z;
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

float MaxAdjacentVisualColorDelta(const game::systems::TerrainData &data) {
  const int resX = data.config.resolutionX;
  const int resZ = data.config.resolutionZ;
  float maxDelta = 0.0f;
  for (int z = 0; z < resZ; ++z) {
    for (int x = 0; x < resX; ++x) {
      const auto &color = data.visualMaterialColors[z * resX + x];
      if (x + 1 < resX) {
        maxDelta = std::max(
            maxDelta,
            VisualColorDistance(color,
                                data.visualMaterialColors[z * resX + x + 1]));
      }
      if (z + 1 < resZ) {
        maxDelta = std::max(
            maxDelta,
            VisualColorDistance(
                color, data.visualMaterialColors[(z + 1) * resX + x]));
      }
    }
  }
  return maxDelta;
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
  CHECK(data.vertices.size() == data.materialMap.size(),
        "Terrain mesh keeps one material value per vertex");
  CHECK(data.visualMaterialColors.size() == data.materialMap.size(),
        "Terrain keeps one visual material color per physics material");
  CHECK(MaxAdjacentVisualColorDelta(data) < 0.35f,
        "Visual material colors transition smoothly between adjacent cells");

  bool vertexMaterialsMatch = true;
  for (size_t i = 0; i < data.vertices.size(); ++i) {
    int encodedMaterial =
        static_cast<int>(std::floor(data.vertices[i].color.w * 255.0f));
    if (encodedMaterial != data.materialMap[i]) {
      vertexMaterialsMatch = false;
      break;
    }
  }
  CHECK(vertexMaterialsMatch,
        "Terrain vertex alpha consistently encodes the material layer");

  auto repeated =
      game::systems::TerrainGenerator::GenerateTerrain("Course variety seed",
                                                       holes, config);
  bool repeatedVisualColorsMatch =
      repeated.visualMaterialColors.size() == data.visualMaterialColors.size();
  if (repeatedVisualColorsMatch) {
    for (size_t i = 0; i < data.visualMaterialColors.size(); ++i) {
      const auto &a = data.visualMaterialColors[i];
      const auto &b = repeated.visualMaterialColors[i];
      if (a.x != b.x || a.y != b.y || a.z != b.z) {
        repeatedVisualColorsMatch = false;
        break;
      }
    }
  }
  CHECK(repeated.materialMap == data.materialMap &&
            repeated.heightMap == data.heightMap && repeatedVisualColorsMatch,
        "Terrain generation is deterministic for the same article and config");
  CHECK(CountIsolatedHazardCells(data) == 0,
        "Generated terrain contains no one-cell hazard speckles");
  CHECK(MaxAdjacentHeightDelta(data) < config.heightScale,
        "Generated terrain contains no abrupt one-cell height steps");

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
    if (biome == 1) {
      CHECK(themedCounts[kBunker] + themedCounts[7] > themedCounts[kRough],
            "Desert terrain is dominated by sand and stone outside the course");
    }
    CHECK(CountIsolatedHazardCells(themed) == 0,
          "Biome terrain contains no one-cell hazard speckles");
    CHECK(unique >= 4, "Biome course keeps at least four terrain types");
  }

  CHECK(variedSeedsWithHazards >= 3,
        "Most biome variants include hazard or gimmick terrain");

  game::systems::TerrainConfig tutorialConfig;
  tutorialConfig.resolutionX = 64;
  tutorialConfig.resolutionZ = 96;
  tutorialConfig.worldWidth = 96.0f;
  tutorialConfig.worldDepth = 144.0f;
  tutorialConfig.heightScale = 1.0f;
  tutorialConfig.biome = 0;

  std::vector<DirectX::XMFLOAT2> tutorialHoles = {
      {0.0f, -28.0f}, {-30.0f, -12.0f}, {24.0f, -3.0f},
      {-24.0f, 20.0f}, {12.0f, 42.0f}, {0.0f, 56.0f}};

  auto tutorialData =
      game::systems::TerrainGenerator::GenerateTutorialTerrain(
          tutorialConfig, tutorialHoles);

  CHECK(tutorialData.materialMap.size() ==
            static_cast<size_t>(tutorialConfig.resolutionX *
                                tutorialConfig.resolutionZ),
        "Tutorial material map has one entry per terrain vertex");
  CHECK(!tutorialData.vertices.empty() && !tutorialData.indices.empty(),
        "Tutorial terrain mesh is generated");
  CHECK(tutorialData.visualMaterialColors.size() ==
            tutorialData.materialMap.size(),
        "Tutorial terrain generates visual material colors");

  std::array<int, 8> tutorialCounts{};
  for (uint8_t mat : tutorialData.materialMap) {
    if (mat < tutorialCounts.size()) {
      ++tutorialCounts[mat];
    }
  }

  CHECK(tutorialCounts[kFairway] > 0, "Tutorial fairway is present");
  CHECK(tutorialCounts[kRough] > 0, "Tutorial rough is present");
  CHECK(tutorialCounts[kBunker] > 0, "Tutorial bunker is present");
  CHECK(tutorialCounts[kGreen] > 0, "Tutorial green is present");
  CHECK(tutorialCounts[kWater] > 0, "Tutorial water hazard is present");
  CHECK(MaterialAt(tutorialData, 0.0f, -28.0f) == kFairway,
        "Tutorial fairway lesson is on fairway");
  CHECK(MaterialAt(tutorialData, 24.0f, -3.0f) == kBunker,
        "Tutorial bunker lesson is in the bunker");
  CHECK(MaterialAt(tutorialData, -24.0f, 20.0f) == kWater,
        "Tutorial water lesson is in the water hazard");
  CHECK(MaterialAt(tutorialData, 0.0f, 56.0f) == kGreen,
        "Tutorial goal is on the green");

  std::cout << "All terrain generation tests passed!\n";
  return 0;
}
