#include "MeshPrimitives.h"
#include "TangentGenerator.h"
#include <cmath>
#include <vector>


namespace graphics {

Mesh MeshPrimitives::CreateTriangle(ID3D11Device *device) {
  std::vector<Vertex> vertices = {
      {{0.0f, 0.5f, 0.0f},
       {0.0f, 0.0f, -1.0f},
       {0.5f, 0.0f},
       {1.0f, 0.0f, 0.0f, 1.0f}},
      {{0.5f, -0.5f, 0.0f},
       {0.0f, 0.0f, -1.0f},
       {1.0f, 1.0f},
       {0.0f, 1.0f, 0.0f, 1.0f}},
      {{-0.5f, -0.5f, 0.0f},
       {0.0f, 0.0f, -1.0f},
       {0.0f, 1.0f},
       {0.0f, 0.0f, 1.0f, 1.0f}},
  };
  std::vector<uint32_t> indices = {0, 1, 2};

  ComputeTangents(vertices, indices);

  Mesh mesh;
  mesh.Create(device, vertices, indices);
  return mesh;
}

Mesh MeshPrimitives::CreateCube(ID3D11Device *device) {
  const float s = 0.5f;
  std::vector<Vertex> vertices = {
      // 前面 (Z-)
      {{-s, -s, -s}, {0, 0, -1}, {0, 1}, {1, 1, 1, 1}},
      {{-s, s, -s}, {0, 0, -1}, {0, 0}, {1, 1, 1, 1}},
      {{s, s, -s}, {0, 0, -1}, {1, 0}, {1, 1, 1, 1}},
      {{s, -s, -s}, {0, 0, -1}, {1, 1}, {1, 1, 1, 1}},
      // 背面 (Z+)
      {{s, -s, s}, {0, 0, 1}, {0, 1}, {1, 1, 1, 1}},
      {{s, s, s}, {0, 0, 1}, {0, 0}, {1, 1, 1, 1}},
      {{-s, s, s}, {0, 0, 1}, {1, 0}, {1, 1, 1, 1}},
      {{-s, -s, s}, {0, 0, 1}, {1, 1}, {1, 1, 1, 1}},
      // 上面 (Y+)
      {{-s, s, -s}, {0, 1, 0}, {0, 1}, {1, 1, 1, 1}},
      {{-s, s, s}, {0, 1, 0}, {0, 0}, {1, 1, 1, 1}},
      {{s, s, s}, {0, 1, 0}, {1, 0}, {1, 1, 1, 1}},
      {{s, s, -s}, {0, 1, 0}, {1, 1}, {1, 1, 1, 1}},
      // 下面 (Y-)
      {{-s, -s, s}, {0, -1, 0}, {0, 1}, {1, 1, 1, 1}},
      {{-s, -s, -s}, {0, -1, 0}, {0, 0}, {1, 1, 1, 1}},
      {{s, -s, -s}, {0, -1, 0}, {1, 0}, {1, 1, 1, 1}},
      {{s, -s, s}, {0, -1, 0}, {1, 1}, {1, 1, 1, 1}},
      // 左面 (X-)
      {{-s, -s, s}, {-1, 0, 0}, {0, 1}, {1, 1, 1, 1}},
      {{-s, s, s}, {-1, 0, 0}, {0, 0}, {1, 1, 1, 1}},
      {{-s, s, -s}, {-1, 0, 0}, {1, 0}, {1, 1, 1, 1}},
      {{-s, -s, -s}, {-1, 0, 0}, {1, 1}, {1, 1, 1, 1}},
      // 右面 (X+)
      {{s, -s, -s}, {1, 0, 0}, {0, 1}, {1, 1, 1, 1}},
      {{s, s, -s}, {1, 0, 0}, {0, 0}, {1, 1, 1, 1}},
      {{s, s, s}, {1, 0, 0}, {1, 0}, {1, 1, 1, 1}},
      {{s, -s, s}, {1, 0, 0}, {1, 1}, {1, 1, 1, 1}},
  };

  std::vector<uint32_t> indices;
  for (uint32_t face = 0; face < 6; ++face) {
    uint32_t base = face * 4;
    indices.push_back(base + 0);
    indices.push_back(base + 1);
    indices.push_back(base + 2);
    indices.push_back(base + 0);
    indices.push_back(base + 2);
    indices.push_back(base + 3);
  }

  ComputeTangents(vertices, indices);

  Mesh mesh;
  mesh.Create(device, vertices, indices);
  return mesh;
}

Mesh MeshPrimitives::CreateSphere(ID3D11Device *device, int segments) {
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;

  const float radius = 0.5f;
  const int rings = segments;
  const int sectors = segments;

  const float R = 1.0f / static_cast<float>(rings - 1);
  const float S = 1.0f / static_cast<float>(sectors - 1);
  const float PI = 3.14159265358979f;

  for (int r = 0; r < rings; ++r) {
    for (int s = 0; s < sectors; ++s) {
      float y = sin(-PI * 0.5f + PI * r * R);
      float x = cos(2 * PI * s * S) * sin(PI * r * R);
      float z = sin(2 * PI * s * S) * sin(PI * r * R);

      Vertex v;
      v.position = {x * radius, y * radius, z * radius};
      v.normal = {x, y, z};
      v.texCoord = {s * S, r * R};
      v.color = {1, 1, 1, 1};
      vertices.push_back(v);
    }
  }

  for (int r = 0; r < rings - 1; ++r) {
    for (int s = 0; s < sectors - 1; ++s) {
      uint32_t current = r * sectors + s;
      uint32_t next = current + sectors;

      indices.push_back(current);
      indices.push_back(next);
      indices.push_back(current + 1);

      indices.push_back(current + 1);
      indices.push_back(next);
      indices.push_back(next + 1);
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

} // namespace graphics
