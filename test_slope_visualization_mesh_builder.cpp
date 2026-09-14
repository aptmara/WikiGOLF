#include "src/game/systems/SlopeVisualizationMeshBuilder.h"
#include "src/game/systems/TerrainGenerator.h"
#include "src/game/utils/TerrainBounds.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

#define CHECK_TRUE(condition, message)                                        \
  do {                                                                        \
    if (!(condition)) {                                                       \
      std::cerr << "[FAIL] " << message << "\n";                            \
      std::exit(1);                                                           \
    }                                                                         \
  } while (false)

namespace {

game::systems::TerrainData MakeTerrain(float slopeX) {
  game::systems::TerrainData terrain;
  terrain.config.resolutionX = 9;
  terrain.config.resolutionZ = 9;
  terrain.config.worldWidth = 8.0f;
  terrain.config.worldDepth = 8.0f;
  terrain.heightMap.resize(81);

  for (int z = 0; z < terrain.config.resolutionZ; ++z) {
    for (int x = 0; x < terrain.config.resolutionX; ++x) {
      const float worldX = -4.0f + static_cast<float>(x);
      terrain.heightMap[z * terrain.config.resolutionX + x] =
          7.0f + worldX * slopeX;
    }
  }
  return terrain;
}

} // namespace

int main() {
  using game::systems::SlopeOverlayConfig;
  using game::systems::SlopeVisualizationMeshBuilder;

  const auto flatTerrain = MakeTerrain(0.0f);
  const auto bounds = game::utils::CalculateTerrainWorldBounds(flatTerrain);
  const DirectX::XMFLOAT3 outside{20.0f, 3.0f, -20.0f};
  const auto clamped = game::utils::ClampToTerrainBounds(outside, bounds);
  CHECK_TRUE(bounds.Contains(clamped.x, clamped.z),
             "clamped golfer position stays inside terrain bounds");

  SlopeOverlayConfig config;
  config.radius = 2.0f;
  config.cellSize = 0.5f;
  config.sampleOffset = 0.35f;
  config.maxSlope = 0.12f;

  const std::vector<DirectX::XMFLOAT3> edgeCenters = {
      {bounds.minX, 0.0f, bounds.minZ}, {bounds.minX, 0.0f, bounds.maxZ},
      {bounds.maxX, 0.0f, bounds.minZ}, {bounds.maxX, 0.0f, bounds.maxZ}};
  for (const auto &center : edgeCenters) {
    const auto mesh =
        SlopeVisualizationMeshBuilder::Build(flatTerrain, center, config);
    CHECK_TRUE(!mesh.IsEmpty(), "edge overlay still contains visible terrain");
    for (const auto &vertex : mesh.vertices) {
      CHECK_TRUE(bounds.Contains(vertex.position.x, vertex.position.z),
                 "overlay vertices do not extend beyond terrain bounds");
      CHECK_TRUE(std::fabs(vertex.color.w) < 0.0001f,
                 "flat terrain edge is not classified as a cliff");
    }
    for (std::uint32_t index : mesh.indices) {
      CHECK_TRUE(index < mesh.vertices.size(),
                 "clipped overlay indices remain valid");
    }
  }

  const auto slopedTerrain = MakeTerrain(0.06f);
  const auto slopeMesh = SlopeVisualizationMeshBuilder::Build(
      slopedTerrain, {bounds.maxX, 0.0f, 0.0f}, config);
  CHECK_TRUE(!slopeMesh.IsEmpty(), "sloped edge overlay is generated");
  for (const auto &vertex : slopeMesh.vertices) {
    CHECK_TRUE(std::fabs(vertex.color.w - 0.5f) < 0.001f,
               "one-sided edge sampling preserves the real terrain slope");
  }

  game::systems::TerrainData invalidTerrain;
  CHECK_TRUE(SlopeVisualizationMeshBuilder::Build(
                 invalidTerrain, {0.0f, 0.0f, 0.0f}, config)
                 .IsEmpty(),
             "invalid terrain does not generate an overlay");

  std::cout << "All slope visualization mesh builder tests passed.\n";
  return 0;
}
