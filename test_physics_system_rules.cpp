#include "src/game/systems/PhysicsSystemInternals.h"
#include <cmath>
#include <iostream>

#define CHECK(condition, message)                                             \
  do {                                                                        \
    if (!(condition)) {                                                       \
      std::cerr << "[FAIL] " << message << "\n";                            \
      return 1;                                                               \
    }                                                                         \
    std::cout << "[PASS] " << message << "\n";                              \
  } while (false)

#define CHECK_CLOSE(actual, expected, epsilon, message)                       \
  CHECK(std::fabs((actual) - (expected)) <= (epsilon), message)

int main() {
  using namespace DirectX;
  using namespace game::systems;

  CHECK(GridCoord(-0.1f, 1.0f) == -1, "Grid coordinates use floor semantics");
  CHECK(MakeGridKey(2, 3) != MakeGridKey(3, 2),
        "Grid keys distinguish both axes");

  uint32_t cursor = 0;
  CHECK_CLOSE(GetJitterFromTable(cursor, 0.2f), 0.906f, 1e-6f,
              "Jitter table keeps the first deterministic sample");
  CHECK_CLOSE(GetJitterFromTable(cursor, 0.2f), 1.024f, 1e-6f,
              "Jitter table advances in deterministic order");

  XMVECTOR normal;
  float depth = 0.0f;
  XMFLOAT4 identityRotation = {0.0f, 0.0f, 0.0f, 1.0f};
  CHECK(!CheckSphereOBB({2.0f, 0.0f, 0.0f}, 0.5f, {0.0f, 0.0f, 0.0f},
                        {2.0f, 2.0f, 2.0f}, identityRotation, normal,
                        depth),
        "Sphere outside an OBB does not collide");
  CHECK(CheckSphereOBB({1.4f, 0.0f, 0.0f}, 0.5f, {0.0f, 0.0f, 0.0f},
                       {2.0f, 2.0f, 2.0f}, identityRotation, normal, depth),
        "Sphere near an OBB collides");
  CHECK_CLOSE(XMVectorGetX(normal), 1.0f, 1e-5f,
              "OBB collision normal points away from the box");
  CHECK_CLOSE(depth, 0.1f, 1e-5f, "OBB collision depth is preserved");

  TerrainData terrain;
  terrain.config.resolutionX = 2;
  terrain.config.resolutionZ = 2;
  terrain.config.worldWidth = 2.0f;
  terrain.config.worldDepth = 2.0f;
  terrain.heightMap = {0.0f, 2.0f, 4.0f, 6.0f};
  terrain.materialMap = {0, 1, 2, 3};
  terrain.normals.assign(4, XMFLOAT3{0.0f, 1.0f, 0.0f});
  TerrainSample sample = SampleTerrainAt(terrain, 0.0f, 0.0f);
  CHECK(sample.valid, "Terrain sampling accepts coordinates inside the map");
  CHECK_CLOSE(sample.height, 3.0f, 1e-5f,
              "Terrain sampling bilinearly interpolates height");
  CHECK(sample.material == 0, "Terrain sampling returns the lower cell material");

  std::cout << "All physics system rule tests passed!\n";
  return 0;
}
