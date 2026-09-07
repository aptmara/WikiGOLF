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

      // パッチが敷き詰められる場所では隣接パッチと重なって縁が隠れるが、
      // 境界付近で単独になった時、株の生えている塊そのものが正方形の
      // シルエットとして見えてしまう。セル中心からパッチ外周までの
      // チェビシェフ距離で密度と丈を滑らかに落とし、外周のごく一部
      // （80%〜100%）だけをまばら・低く刈り込むことで、通常の密な
      // 芝面の見た目はほぼ変えずに、単独になった時だけ正方形の輪郭を
      // やわらげる。間引く範囲を広くしすぎると、隣接パッチとの重なり
      // 幅（現状わずか）だけでは埋まらない隙間ができ、逆に密度不足で
      // 四角い塊が点在して見えてしまうため、ごく浅くとどめる。
      const float cellCenterX = (static_cast<float>(cx) + 0.5f) / gridSize - 0.5f;
      const float cellCenterZ = (static_cast<float>(cz) + 0.5f) / gridSize - 0.5f;
      const float edgeDistance =
          std::max(std::abs(cellCenterX), std::abs(cellCenterZ)) / 0.5f;
      constexpr float kFeatherStart = 0.82f;
      const float featherT = std::clamp(
          (edgeDistance - kFeatherStart) / (1.0f - kFeatherStart), 0.0f, 1.0f);
      const float feather = featherT * featherT * (3.0f - 2.0f * featherT);
      const float density = 1.0f - feather;

      for (int blade = 0; blade < bladesPerCell; ++blade) {
        const float seed =
            cellSeed * 7.0f + static_cast<float>(blade + 1) * 3.371f;

        // 外周に近いほど株を間引く（密度が低いほど生える確率が下がる）。
        const float dropoutRoll = std::abs(std::sin(seed * 47.13f + 11.7f));
        if (dropoutRoll > density) {
          continue;
        }

        const float jitterX = std::sin(seed * 12.9898f) * 0.036f;
        const float jitterZ = std::sin(seed * 78.233f) * 0.036f;
        const float rootOffsetX =
            (static_cast<float>(cx) + 0.28f + 0.44f * blade) / gridSize -
            0.5f + jitterX;
        const float rootOffsetZ =
            (static_cast<float>(cz) + 0.72f - 0.44f * blade) / gridSize -
            0.5f + jitterZ;

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
