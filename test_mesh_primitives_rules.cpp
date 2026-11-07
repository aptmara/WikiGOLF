#include "src/graphics/MeshPrimitives.h"
#include <iostream>

#define CHECK(condition, message)                                             \
  do {                                                                        \
    if (!(condition)) {                                                       \
      std::cerr << "[FAIL] " << message << "\n";                              \
      return 1;                                                               \
    }                                                                          \
    std::cout << "[PASS] " << message << "\n";                               \
  } while (false)

int main() {
  ID3D11Device *device = nullptr;

  CHECK(!graphics::MeshPrimitives::CreateTriangle(device).IsValid(),
        "Triangle creation rejects a null device");
  CHECK(!graphics::MeshPrimitives::CreateCube(device).IsValid(),
        "Cube creation rejects a null device");
  CHECK(!graphics::MeshPrimitives::CreateSphere(device).IsValid(),
        "Sphere creation rejects a null device");
  CHECK(!graphics::MeshPrimitives::CreateCylinder(device).IsValid(),
        "Cylinder creation rejects a null device");
  CHECK(!graphics::MeshPrimitives::CreateGolfCup(device).IsValid(),
        "Golf cup creation rejects a null device");
  CHECK(!graphics::MeshPrimitives::CreateRock(device).IsValid(),
        "Rock creation rejects a null device");
  CHECK(!graphics::MeshPrimitives::CreateSandCrater(device).IsValid(),
        "Sand crater creation rejects a null device");
  CHECK(!graphics::MeshPrimitives::CreatePlane(device).IsValid(),
        "Plane creation rejects a null device");
  CHECK(!graphics::MeshPrimitives::CreateQuad(device).IsValid(),
        "Quad creation rejects a null device");

  std::cout << "All MeshPrimitives rule tests passed.\n";
  return 0;
}
