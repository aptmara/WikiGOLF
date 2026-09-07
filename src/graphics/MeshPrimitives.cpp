/**
 * @file MeshPrimitives.cpp
 * @brief プリミティブメッシュ生成ファクトリの実装
*/

#include "MeshPrimitives.h"
#include "TangentGenerator.h"
#include <algorithm>
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

Mesh MeshPrimitives::CreateCylinder(ID3D11Device *device, int segments) {
  segments = (std::max)(segments, 3);
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  const float radius = 0.5f;
  const float halfHeight = 0.5f;
  const float pi = 3.14159265358979f;

  vertices.reserve((segments + 1) * 4 + 2);
  indices.reserve(segments * 12);

  for (int i = 0; i <= segments; ++i) {
    float t = static_cast<float>(i) / static_cast<float>(segments);
    float angle = t * pi * 2.0f;
    float nx = std::cos(angle);
    float nz = std::sin(angle);
    vertices.push_back(
        {{nx * radius, -halfHeight, nz * radius}, {nx, 0.0f, nz}, {t, 1.0f},
         {1, 1, 1, 1}});
    vertices.push_back(
        {{nx * radius, halfHeight, nz * radius}, {nx, 0.0f, nz}, {t, 0.0f},
         {1, 1, 1, 1}});
  }

  for (int i = 0; i < segments; ++i) {
    uint32_t bottom = static_cast<uint32_t>(i * 2);
    uint32_t top = bottom + 1;
    uint32_t nextBottom = bottom + 2;
    uint32_t nextTop = bottom + 3;
    indices.insert(indices.end(),
                   {bottom, top, nextBottom, nextBottom, top, nextTop});
  }

  uint32_t bottomCenter = static_cast<uint32_t>(vertices.size());
  vertices.push_back(
      {{0.0f, -halfHeight, 0.0f}, {0, -1, 0}, {0.5f, 0.5f}, {1, 1, 1, 1}});
  uint32_t bottomRing = static_cast<uint32_t>(vertices.size());
  for (int i = 0; i <= segments; ++i) {
    float angle = static_cast<float>(i) / static_cast<float>(segments) * pi * 2.0f;
    float x = std::cos(angle);
    float z = std::sin(angle);
    vertices.push_back({{x * radius, -halfHeight, z * radius},
                        {0, -1, 0},
                        {x * 0.5f + 0.5f, z * 0.5f + 0.5f},
                        {1, 1, 1, 1}});
  }

  uint32_t topCenter = static_cast<uint32_t>(vertices.size());
  vertices.push_back(
      {{0.0f, halfHeight, 0.0f}, {0, 1, 0}, {0.5f, 0.5f}, {1, 1, 1, 1}});
  uint32_t topRing = static_cast<uint32_t>(vertices.size());
  for (int i = 0; i <= segments; ++i) {
    float angle = static_cast<float>(i) / static_cast<float>(segments) * pi * 2.0f;
    float x = std::cos(angle);
    float z = std::sin(angle);
    vertices.push_back({{x * radius, halfHeight, z * radius},
                        {0, 1, 0},
                        {x * 0.5f + 0.5f, z * 0.5f + 0.5f},
                        {1, 1, 1, 1}});
  }

  for (int i = 0; i < segments; ++i) {
    uint32_t current = static_cast<uint32_t>(i);
    indices.insert(indices.end(),
                   {bottomCenter, bottomRing + current,
                    bottomRing + current + 1, topCenter,
                    topRing + current + 1, topRing + current});
  }

  ComputeTangents(vertices, indices);
  Mesh mesh;
  mesh.Create(device, vertices, indices);
  return mesh;
}

Mesh MeshPrimitives::CreateRock(ID3D11Device *device, int rings, int sectors) {
  rings = (std::max)(rings, 4);
  sectors = (std::max)(sectors, 6);
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  const float pi = 3.14159265358979f;

  vertices.reserve((rings + 1) * (sectors + 1));
  indices.reserve(rings * sectors * 6);

  for (int ring = 0; ring <= rings; ++ring) {
    float v = static_cast<float>(ring) / static_cast<float>(rings);
    float latitude = -pi * 0.5f + v * pi;
    float y = std::sin(latitude);
    float horizontal = std::cos(latitude);

    for (int sector = 0; sector <= sectors; ++sector) {
      float u = static_cast<float>(sector) / static_cast<float>(sectors);
      float longitude = u * pi * 2.0f;
      float x = horizontal * std::cos(longitude);
      float z = horizontal * std::sin(longitude);
      float distortion = 0.86f + 0.10f * std::sin(longitude * 3.0f + latitude) +
                         0.07f * std::sin(longitude * 5.0f - latitude * 2.0f);
      float px = x * 0.52f * distortion;
      float py = y * 0.40f * (0.94f + 0.06f * std::cos(longitude * 4.0f));
      float pz = z * 0.48f * distortion;
      float normalLength = std::sqrt(px * px + py * py + pz * pz);
      if (normalLength < 0.00001f) {
        normalLength = 1.0f;
      }
      vertices.push_back({{px, py, pz},
                          {px / normalLength, py / normalLength,
                           pz / normalLength},
                          {u, v},
                          {1, 1, 1, 1}});
    }
  }

  const int stride = sectors + 1;
  for (int ring = 0; ring < rings; ++ring) {
    for (int sector = 0; sector < sectors; ++sector) {
      uint32_t current = static_cast<uint32_t>(ring * stride + sector);
      uint32_t next = current + static_cast<uint32_t>(stride);
      indices.insert(indices.end(),
                     {current, next, current + 1, current + 1, next, next + 1});
    }
  }

  ComputeTangents(vertices, indices);
  Mesh mesh;
  mesh.Create(device, vertices, indices);
  return mesh;
}

} // namespace graphics
