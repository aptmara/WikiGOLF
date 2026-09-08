/**
 * @file MeshPrimitivesGrass.cpp
 * @brief プリミティブメッシュ生成ファクトリの実装
*/

#include "MeshPrimitives.h"
#include "TangentGenerator.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace graphics {

Mesh MeshPrimitives::CreateGrassClump(ID3D11Device *device) {
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  constexpr int bladeCount = 5;
  constexpr float pi = 3.14159265358979f;

  vertices.reserve(bladeCount * 5);
  indices.reserve(bladeCount * 18);

  for (int blade = 0; blade < bladeCount; ++blade) {
    const float angle = static_cast<float>(blade) / bladeCount * pi;
    const float sideX = std::cos(angle);
    const float sideZ = std::sin(angle);
    const float normalX = -sideZ;
    const float normalZ = sideX;
    const float height =
        0.78f + static_cast<float>((blade * 37) % 19) / 100.0f;
    const float halfWidth =
        0.085f + static_cast<float>((blade * 13) % 7) / 500.0f;
    const float rootOffsetX = std::cos(angle * 2.3f) * 0.11f;
    const float rootOffsetZ = std::sin(angle * 1.7f) * 0.11f;
    float bendDirection = -1.0f;
    if ((blade % 2) == 0) {
      bendDirection = 1.0f;
    }
    const float bend = bendDirection * 0.09f;
    const uint32_t base = static_cast<uint32_t>(vertices.size());

    const DirectX::XMFLOAT3 normal = {normalX, 0.18f, normalZ};
    const DirectX::XMFLOAT4 rootColor = {0.54f, 0.70f, 0.38f, 1.0f};
    const DirectX::XMFLOAT4 midColor = {0.76f, 0.92f, 0.52f, 1.0f};
    const DirectX::XMFLOAT4 tipColor = {0.90f, 1.00f, 0.66f, 1.0f};

    vertices.push_back({{rootOffsetX - sideX * halfWidth, 0.0f,
                         rootOffsetZ - sideZ * halfWidth},
                        normal, {0.0f, 1.0f}, rootColor});
    vertices.push_back({{rootOffsetX + sideX * halfWidth, 0.0f,
                         rootOffsetZ + sideZ * halfWidth},
                        normal, {1.0f, 1.0f}, rootColor});
    vertices.push_back({{rootOffsetX - sideX * halfWidth * 0.58f +
                             normalX * bend,
                         height * 0.58f,
                         rootOffsetZ - sideZ * halfWidth * 0.58f +
                             normalZ * bend},
                        normal, {0.18f, 0.42f}, midColor});
    vertices.push_back({{rootOffsetX + sideX * halfWidth * 0.58f +
                             normalX * bend,
                         height * 0.58f,
                         rootOffsetZ + sideZ * halfWidth * 0.58f +
                             normalZ * bend},
                        normal, {0.82f, 0.42f}, midColor});
    vertices.push_back({{rootOffsetX + normalX * bend * 2.15f, height,
                         rootOffsetZ + normalZ * bend * 2.15f},
                        normal, {0.5f, 0.0f}, tipColor});

    const uint32_t front[] = {base, base + 1, base + 2,
                              base + 2, base + 1, base + 3,
                              base + 2, base + 3, base + 4};
    indices.insert(indices.end(), front, front + 9);
    for (int tri = 0; tri < 3; ++tri) {
      indices.push_back(front[tri * 3 + 2]);
      indices.push_back(front[tri * 3 + 1]);
      indices.push_back(front[tri * 3]);
    }
  }

  ComputeTangents(vertices, indices);
  Mesh mesh;
  mesh.Create(device, vertices, indices);
  return mesh;
}

Mesh MeshPrimitives::CreateGrassPatch(ID3D11Device *device,
                                       uint32_t variantSeed, int gridSize,
                                       int bladesPerCell) {
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  // ゴルフ場のラフは独立した草株の集合ではなく、地表全体を覆う芝床から
  // 細い葉が高密度に立ち上がる。各セルに葉を分散し、根元を共有する
  // 放射状クランプや、葉のない大きな隙間を作らない。
  gridSize = std::max(gridSize, 2);
  bladesPerCell = std::max(bladesPerCell, 1);
  const int cellCount = gridSize * gridSize;
  constexpr float pi = 3.14159265358979f;
  const float variantOffset = static_cast<float>(variantSeed) * 811.87f;

  vertices.reserve(cellCount * bladesPerCell * 6);
  indices.reserve(cellCount * bladesPerCell * 24);

  for (int cz = 0; cz < gridSize; ++cz) {
    for (int cx = 0; cx < gridSize; ++cx) {
      const int cell = cz * gridSize + cx;
      const float cellSeed = static_cast<float>(cell + 1) + variantOffset;

      for (int blade = 0; blade < bladesPerCell; ++blade) {
        const float seed =
            cellSeed * 7.0f + static_cast<float>(blade + 1) * 3.371f;

        const float jitterX = std::sin(seed * 12.9898f) * 0.036f;
        const float jitterZ = std::sin(seed * 78.233f) * 0.036f;
        const float rootOffsetX =
            (static_cast<float>(cx) + 0.28f + 0.44f * blade) / gridSize -
            0.5f + jitterX;
        const float rootOffsetZ =
            (static_cast<float>(cz) + 0.72f - 0.44f * blade) / gridSize -
            0.5f + jitterZ;

        // 正方形グリッドの外周をそのまま間引くと、密度を落としても
        // パッチの輪郭自体は四角く残る。実際の葉の根元を基準にした
        // 円形距離とseed由来の低周波な揺らぎで輪郭を崩し、外周では
        // 密度と丈を連続的に落とす。配置側でパッチ同士を十分重ねている
        // ため、角を落としても芝面の隙間にはならない。
        const float edgeAngle = std::atan2(rootOffsetZ, rootOffsetX);
        const float edgePhase = variantOffset * 0.013f;
        const float edgeRadius =
            0.52f + std::sin(edgeAngle * 3.0f + edgePhase) * 0.045f +
            std::sin(edgeAngle * 5.0f - edgePhase * 1.7f) * 0.025f;
        const float edgeDistance =
            std::sqrt(rootOffsetX * rootOffsetX + rootOffsetZ * rootOffsetZ);
        const float featherStart = edgeRadius - 0.14f;
        const float featherT = std::clamp(
            (edgeDistance - featherStart) / (edgeRadius - featherStart),
            0.0f, 1.0f);
        const float feather = featherT * featherT * (3.0f - 2.0f * featherT);
        const float density = 1.0f - feather;

        const float dropoutRoll = std::abs(std::sin(seed * 47.13f + 11.7f));
        if (dropoutRoll > density) {
          continue;
        }

        // 芝床全体では方向を均一に分散するが、葉単体はほぼ直立させる。
        // 放射状に大きく開かせると雑草の株に見えるため行わない。
        const float angle = std::fmod(seed * 5.263f, pi * 2.0f);
        const float sideX = std::cos(angle);
        const float sideZ = std::sin(angle);
        const float normalX = -sideZ;
        const float normalZ = sideX;

        // 生き残った外周付近の株も、丈を短くして塊がなだらかに
        // 低くなりながら消えていくようにする（浅めに）。
        const float edgeHeightScale = 0.6f + 0.4f * density;
        const float height =
            (0.78f + 0.20f * (0.5f + 0.5f * std::sin(seed * 4.173f))) *
            edgeHeightScale;
        const float halfWidth =
            0.0045f + 0.0022f * (0.5f + 0.5f * std::sin(seed * 7.913f));
        const float lean = std::sin(seed * 3.117f) * 0.045f;
        const uint32_t base = static_cast<uint32_t>(vertices.size());

        const DirectX::XMFLOAT3 normal = {normalX, 0.12f, normalZ};
        const DirectX::XMFLOAT4 rootColor = {0.52f, 0.64f, 0.42f, 1.0f};
        const DirectX::XMFLOAT4 midColor = {0.82f, 0.91f, 0.68f, 1.0f};
        const DirectX::XMFLOAT4 tipColor = {0.74f, 0.84f, 0.58f, 1.0f};
        const float tipHalfWidth = halfWidth * 0.16f;

        vertices.push_back({{rootOffsetX - sideX * halfWidth, 0.0f,
                             rootOffsetZ - sideZ * halfWidth},
                            normal, {0.0f, 1.0f}, rootColor});
        vertices.push_back({{rootOffsetX + sideX * halfWidth, 0.0f,
                             rootOffsetZ + sideZ * halfWidth},
                            normal, {1.0f, 1.0f}, rootColor});
        vertices.push_back({{rootOffsetX - sideX * halfWidth * 0.62f +
                                 normalX * lean,
                             height * 0.58f,
                             rootOffsetZ - sideZ * halfWidth * 0.62f +
                                 normalZ * lean},
                            normal, {0.18f, 0.45f}, midColor});
        vertices.push_back({{rootOffsetX + sideX * halfWidth * 0.62f +
                                 normalX * lean,
                             height * 0.58f,
                             rootOffsetZ + sideZ * halfWidth * 0.62f +
                                 normalZ * lean},
                            normal, {0.82f, 0.45f}, midColor});
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

        const uint32_t front[] = {base,     base + 1, base + 3,
                                  base,     base + 3, base + 2,
                                  base + 2, base + 3, base + 5,
                                  base + 2, base + 5, base + 4};
        indices.insert(indices.end(), front, front + 12);
        for (int tri = 0; tri < 4; ++tri) {
          indices.push_back(front[tri * 3 + 2]);
          indices.push_back(front[tri * 3 + 1]);
          indices.push_back(front[tri * 3]);
        }
      }
    }
  }

  ComputeTangents(vertices, indices);
  Mesh mesh;
  mesh.Create(device, vertices, indices);
  return mesh;
}


} // namespace graphics
