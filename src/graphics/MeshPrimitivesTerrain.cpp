/**
 * @file MeshPrimitivesTerrain.cpp
 * @brief プリミティブメッシュ生成ファクトリの実装
*/

#include "MeshPrimitives.h"
#include "TangentGenerator.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace graphics {

Mesh MeshPrimitives::CreateSandCrater(ID3D11Device *device, int segments) {
  segments = (std::max)(segments, 12);
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  constexpr float pi = 3.14159265358979f;

  vertices.reserve(1 + (segments + 1) * 3);
  indices.reserve(segments * 15);
  vertices.push_back({{0.0f, -0.11f, 0.0f},
                      {0.0f, 1.0f, 0.0f},
                      {0.5f, 0.5f},
                      {0.43f, 0.30f, 0.15f, 1.0f}});

  const float radii[] = {0.30f, 0.62f, 1.0f};
  const float heights[] = {-0.065f, 0.055f, 0.0f};
  const DirectX::XMFLOAT4 colors[] = {
      {0.60f, 0.43f, 0.23f, 1.0f},
      {0.98f, 0.82f, 0.51f, 1.0f},
      {0.80f, 0.67f, 0.42f, 1.0f},
  };

  uint32_t ringStart[3] = {};
  for (int ring = 0; ring < 3; ++ring) {
    ringStart[ring] = static_cast<uint32_t>(vertices.size());
    for (int segment = 0; segment <= segments; ++segment) {
      const float ratio =
          static_cast<float>(segment) / static_cast<float>(segments);
      const float angle = ratio * pi * 2.0f;
      const float x = std::cos(angle);
      const float z = std::sin(angle);
      const float edgeNoise =
          1.0f + 0.035f * std::sin(angle * 5.0f) +
          0.022f * std::sin(angle * 11.0f + 0.7f);
      float radialSlope = 0.08f;
      if (ring == 1) {
        radialSlope = -0.18f;
      }
      DirectX::XMFLOAT3 normal = {-x * radialSlope, 1.0f,
                                  -z * radialSlope};
      vertices.push_back(
          {{x * radii[ring] * edgeNoise, heights[ring],
            z * radii[ring] * edgeNoise},
           normal,
           {x * radii[ring] * 0.5f + 0.5f,
            z * radii[ring] * 0.5f + 0.5f},
           colors[ring]});
    }
  }

  for (int segment = 0; segment < segments; ++segment) {
    indices.insert(indices.end(),
                   {0, ringStart[0] + static_cast<uint32_t>(segment),
                    ringStart[0] + static_cast<uint32_t>(segment + 1)});
  }
  for (int ring = 0; ring < 2; ++ring) {
    for (int segment = 0; segment < segments; ++segment) {
      const uint32_t inner =
          ringStart[ring] + static_cast<uint32_t>(segment);
      const uint32_t outer =
          ringStart[ring + 1] + static_cast<uint32_t>(segment);
      indices.insert(indices.end(),
                     {inner, outer, inner + 1, inner + 1, outer, outer + 1});
    }
  }

  ComputeTangents(vertices, indices);
  Mesh mesh;
  mesh.Create(device, vertices, indices);
  return mesh;
}

Mesh MeshPrimitives::CreatePlane(ID3D11Device *device, float width,
                                 float depth) {
  float hw = width * 0.5f;
  float hd = depth * 0.5f;

  std::vector<Vertex> vertices = {
      {{-hw, 0.0f, hd}, {0, 1, 0}, {0, 0}, {1, 1, 1, 1}},
      {{hw, 0.0f, hd}, {0, 1, 0}, {1, 0}, {1, 1, 1, 1}},
      {{hw, 0.0f, -hd}, {0, 1, 0}, {1, 1}, {1, 1, 1, 1}},
      {{-hw, 0.0f, -hd}, {0, 1, 0}, {0, 1}, {1, 1, 1, 1}},
  };

  std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3};

  ComputeTangents(vertices, indices);

  Mesh mesh;
  mesh.Create(device, vertices, indices);
  return mesh;
}

Mesh MeshPrimitives::CreateQuad(ID3D11Device *device) {
  std::vector<Vertex> vertices = {
      {{-0.5f, 0.5f, 0.0f}, {0, 0, -1}, {0, 0}, {1, 1, 1, 1}},
      {{0.5f, 0.5f, 0.0f}, {0, 0, -1}, {1, 0}, {1, 1, 1, 1}},
      {{0.5f, -0.5f, 0.0f}, {0, 0, -1}, {1, 1}, {1, 1, 1, 1}},
      {{-0.5f, -0.5f, 0.0f}, {0, 0, -1}, {0, 1}, {1, 1, 1, 1}},
  };

  std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3};

  ComputeTangents(vertices, indices);

  Mesh mesh;
  mesh.Create(device, vertices, indices);
  return mesh;
}

} // namespace graphics
