#include "src/graphics/TangentGenerator.h"
#include <DirectXMath.h>
#include <cmath>
#include <iostream>
#include <vector>

#define CHECK_CLOSE3(actual, expected, eps, message)                           \
  do {                                                                         \
    if (std::fabs((actual).x - (expected).x) > (eps) ||                        \
        std::fabs((actual).y - (expected).y) > (eps) ||                        \
        std::fabs((actual).z - (expected).z) > (eps)) {                        \
      std::cerr << "[FAIL] " << message << " (" << (actual).x << ","           \
                << (actual).y << "," << (actual).z << " vs expected "          \
                << (expected).x << "," << (expected).y << ","                  \
                << (expected).z << ")\n";                                      \
      std::exit(1);                                                            \
    } else {                                                                   \
      std::cout << "[PASS] " << message << "\n";                               \
    }                                                                          \
  } while (0)

int main() {
  std::vector<graphics::Vertex> vertices = {
      {{0, 0, 0}, {0, 1, 0}, {0, 0}, {1, 1, 1, 1}},
      {{1, 0, 0}, {0, 1, 0}, {1, 0}, {1, 1, 1, 1}},
      {{0, 0, 1}, {0, 1, 0}, {0, 1}, {1, 1, 1, 1}},
      {{1, 0, 1}, {0, 1, 0}, {1, 1}, {1, 1, 1, 1}},
  };

  std::vector<uint32_t> indices = {0, 1, 2, 2, 1, 3};

  graphics::ComputeTangents(vertices, indices);

  const float eps = 0.01f;
  graphics::Vertex &v = vertices[0];
  CHECK_CLOSE3(v.tangent, DirectX::XMFLOAT3{1, 0, 0}, eps,
               "Tangent points +X on UV U");
  CHECK_CLOSE3(v.bitangent, DirectX::XMFLOAT3{0, 0, 1}, eps,
               "Bitangent points +Z on UV V");

  std::cout << "All tangent generator tests passed!\n";
  return 0;
}
