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

namespace {

Mesh CreateTurfPatchMesh(ID3D11Device *device, uint32_t variantSeed,
                         int gridSize, int bladesPerCell,
                         float minimumHalfWidth,
                         float maximumHalfWidth, float rootJitterRatio,
                         float angleVariation, float maximumLean) {
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  const int cellCount = gridSize * gridSize;
  const float variantOffset = static_cast<float>(variantSeed) * 733.31f;

  vertices.reserve(cellCount * bladesPerCell * 4);
  indices.reserve(cellCount * bladesPerCell * 6);

  for (int cz = 0; cz < gridSize; ++cz) {
    for (int cx = 0; cx < gridSize; ++cx) {
      const int cell = cz * gridSize + cx;
      const float cellSeed = static_cast<float>(cell + 1) + variantOffset;

      for (int blade = 0; blade < bladesPerCell; ++blade) {
        constexpr float rootPatternX[] = {0.25f, 0.75f, 0.25f, 0.75f};
        constexpr float rootPatternZ[] = {0.75f, 0.25f, 0.25f, 0.75f};
        const int patternIndex = blade % 4;
        const float seed =
            cellSeed * 7.0f + static_cast<float>(blade + 1) * 3.371f;
        const float cellJitter =
            rootJitterRatio / static_cast<float>(gridSize);
        const float jitterX = std::sin(seed * 12.9898f) * cellJitter;
        const float jitterZ = std::sin(seed * 78.233f) * cellJitter;
        const float rootOffsetX =
            (static_cast<float>(cx) + rootPatternX[patternIndex]) / gridSize -
            0.5f + jitterX;
        const float rootOffsetZ =
            (static_cast<float>(cz) + rootPatternZ[patternIndex]) / gridSize -
            0.5f + jitterZ;

        // 局所+Z方向の刈り目へほぼ揃える。パッチ全体のyawを配置側で
        // 変えることで、FairwayとGreenそれぞれの刈り方向へ合わせる。
        const float angle = std::sin(seed * 5.263f) * angleVariation;
        const float sideX = std::cos(angle);
        const float sideZ = std::sin(angle);
        const float normalX = -sideZ;
        const float normalZ = sideX;

        const float height =
            0.88f + 0.10f * (0.5f + 0.5f * std::sin(seed * 4.173f));
        const float widthVariation =
            0.5f + 0.5f * std::sin(seed * 7.913f);
        const float halfWidth =
            minimumHalfWidth +
            (maximumHalfWidth - minimumHalfWidth) * widthVariation;
        const float lean = std::sin(seed * 3.117f) * maximumLean;
        const uint32_t base = static_cast<uint32_t>(vertices.size());

        const DirectX::XMFLOAT3 normal = {normalX, 0.10f, normalZ};
        const DirectX::XMFLOAT4 rootColor = {0.62f, 0.72f, 0.52f, 1.0f};
        const DirectX::XMFLOAT4 tipColor = {0.84f, 0.91f, 0.70f, 1.0f};
        const float tipHalfWidth = halfWidth * 0.28f;

        vertices.push_back({{rootOffsetX - sideX * halfWidth, 0.0f,
                             rootOffsetZ - sideZ * halfWidth},
                            normal, {0.0f, 1.0f}, rootColor});
        vertices.push_back({{rootOffsetX + sideX * halfWidth, 0.0f,
                             rootOffsetZ + sideZ * halfWidth},
                            normal, {1.0f, 1.0f}, rootColor});
        vertices.push_back({{rootOffsetX - sideX * tipHalfWidth +
                                 normalX * lean * 2.0f,
                             height,
                             rootOffsetZ - sideZ * tipHalfWidth +
                                 normalZ * lean * 2.0f},
                            normal, {0.32f, 0.0f}, tipColor});
        vertices.push_back({{rootOffsetX + sideX * tipHalfWidth +
                                 normalX * lean * 2.0f,
                             height,
                             rootOffsetZ + sideZ * tipHalfWidth +
                                 normalZ * lean * 2.0f},
                            normal, {0.68f, 0.0f}, tipColor});

        const uint32_t front[] = {base, base + 1, base + 3,
                                  base, base + 3, base + 2};
        indices.insert(indices.end(), front, front + 6);
      }
    }
  }

  ComputeTangents(vertices, indices);
  Mesh mesh;
  mesh.Create(device, vertices, indices);
  return mesh;
}

} // namespace

Mesh MeshPrimitives::CreateTurfPatch(ID3D11Device *device,
                                     uint32_t variantSeed) {
  // 中距離では葉数を抑え、地形シェーダーの微細繊維へ連続させる。
  return CreateTurfPatchMesh(device, variantSeed, 12, 2, 0.0024f, 0.0036f,
                             0.12f, 0.09f, 0.0032f);
}

Mesh MeshPrimitives::CreateDenseTurfPatch(ID3D11Device *device,
                                          uint32_t variantSeed) {
  // 隣接セルとの境界まで同じ葉間隔を保つ高密度の近距離短芝。
  return CreateTurfPatchMesh(device, variantSeed, 24, 2, 0.0020f, 0.0032f,
                             0.12f, 0.09f, 0.0032f);
}

Mesh MeshPrimitives::CreateUltraDenseTurfPatch(ID3D11Device *device,
                                               uint32_t variantSeed) {
  // 最初の2枚は中距離LODと同じ位置に置き、追加2枚だけで密度を倍にする。
  return CreateTurfPatchMesh(device, variantSeed, 24, 4, 0.0020f, 0.0032f,
                             0.12f, 0.09f, 0.0032f);
}

Mesh MeshPrimitives::CreateDenseFairwayTurfPatch(ID3D11Device *device,
                                                 uint32_t variantSeed) {
  // 小区画内で根元、向き、寝方をばらつかせ、刈り方向を保ちながら
  // 低角度では葉同士が重なった連続面に見える密度にする。
  return CreateTurfPatchMesh(device, variantSeed, 32, 2, 0.0022f, 0.0036f,
                             0.22f, 0.32f, 0.0090f);
}

Mesh MeshPrimitives::CreateUltraDenseFairwayTurfPatch(
    ID3D11Device *device, uint32_t variantSeed) {
  // 中距離LODの葉を保持したまま追加2枚を重ね、接近時だけ密度を倍にする。
  return CreateTurfPatchMesh(device, variantSeed, 32, 4, 0.0022f, 0.0036f,
                             0.22f, 0.32f, 0.0090f);
}

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
