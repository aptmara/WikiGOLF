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
    const float bend = (blade % 2 == 0 ? 1.0f : -1.0f) * 0.09f;
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
                                      uint32_t variantSeed) {
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  // ゴルフ場のラフは独立した草株の集合ではなく、地表全体を覆う芝床から
  // 細い葉が高密度に立ち上がる。各セルに葉を分散し、根元を共有する
  // 放射状クランプや、葉のない大きな隙間を作らない。
  constexpr int gridSize = 9;
  constexpr int bladesPerCell = 2;
  constexpr int cellCount = gridSize * gridSize;
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

        // 芝床全体では方向を均一に分散するが、葉単体はほぼ直立させる。
        // 放射状に大きく開かせると雑草の株に見えるため行わない。
        const float angle = std::fmod(seed * 5.263f, pi * 2.0f);
        const float sideX = std::cos(angle);
        const float sideZ = std::sin(angle);
        const float normalX = -sideZ;
        const float normalZ = sideX;

        const float height =
            0.78f + 0.20f * (0.5f + 0.5f * std::sin(seed * 4.173f));
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

Mesh MeshPrimitives::CreateTurfPatch(ID3D11Device *device,
                                     uint32_t variantSeed) {
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  // ラフの株のように向きをバラバラにすると「刈り込まれた芝」ではなく
  // ただの草むらに見えてしまう。ここでは株の向きのばらつきをラフの
  // 1/10程度に抑え、まっすぐ密に並んだ「刈り込み株」にする。実際に
  // 刈り跡の方向へ揃えるのは配置側（パッチ全体のyaw）で行う。
  constexpr int gridSize = 7;
  constexpr int bladesPerTuft = 2;
  constexpr int tuftCount = gridSize * gridSize;
  constexpr float pi = 3.14159265358979f;
  const float variantOffset = static_cast<float>(variantSeed) * 733.31f;

  vertices.reserve(tuftCount * bladesPerTuft * 6);
  indices.reserve(tuftCount * bladesPerTuft * 24);

  for (int cz = 0; cz < gridSize; ++cz) {
    for (int cx = 0; cx < gridSize; ++cx) {
      const int tuft = cz * gridSize + cx;
      const float tuftSeed = static_cast<float>(tuft + 1) + variantOffset;

      const float jitterX = std::sin(tuftSeed * 12.9898f) * 0.028f;
      const float jitterZ = std::sin(tuftSeed * 78.233f) * 0.028f;
      const float centerX =
          (static_cast<float>(cx) + 0.5f) / gridSize - 0.5f + jitterX;
      const float centerZ =
          (static_cast<float>(cz) + 0.5f) / gridSize - 0.5f + jitterZ;
      // 向きはほぼ一定（局所+Z寄り）。ラフの clumpFacing は 0..2πの
      // フルランダムだったが、ここでは±0.12rad程度の微揺れに留める。
      const float tuftFacing =
          (pi * 0.5f) + std::sin(tuftSeed * 5.263f) * 0.12f;
      const float tuftHeight =
          0.42f + 0.10f * (0.5f + 0.5f * std::sin(tuftSeed * 4.173f));

      for (int b = 0; b < bladesPerTuft; ++b) {
        const float seed =
            tuftSeed * 7.0f + static_cast<float>(b + 1) * 3.371f;
        const float bladeSpread =
            static_cast<float>(b) / static_cast<float>(bladesPerTuft - 1) -
            0.5f;
        const float angle = tuftFacing + bladeSpread * 0.10f;
        const float sideX = std::cos(angle);
        const float sideZ = std::sin(angle);
        const float normalX = -sideZ;
        const float normalZ = sideX;

        const float rootOffsetX = centerX + sideX * 0.012f * bladeSpread;
        const float rootOffsetZ = centerZ + sideZ * 0.012f * bladeSpread;

        const float height =
            tuftHeight * (0.88f + 0.20f * std::sin(seed * 4.7f + 1.3f));
        // ラフより細く短い刈り込み芝の葉。
        const float halfWidth =
            0.0060f + 0.0020f * (0.5f + 0.5f * std::sin(seed * 7.913f));
        const float naturalBend = std::sin(seed * 3.117f) * 0.03f;
        const uint32_t base = static_cast<uint32_t>(vertices.size());

        const DirectX::XMFLOAT3 normal = {normalX, 0.30f, normalZ};
        const DirectX::XMFLOAT4 rootColor = {0.20f, 0.46f, 0.16f, 1.0f};
        const DirectX::XMFLOAT4 midColor = {0.34f, 0.62f, 0.24f, 1.0f};
        const DirectX::XMFLOAT4 tipColor = {0.44f, 0.72f, 0.30f, 1.0f};

        const float tipHalfWidth = halfWidth * 0.35f;

        vertices.push_back({{rootOffsetX - sideX * halfWidth, 0.0f,
                             rootOffsetZ - sideZ * halfWidth},
                            normal, {0.0f, 1.0f}, rootColor});
        vertices.push_back({{rootOffsetX + sideX * halfWidth, 0.0f,
                             rootOffsetZ + sideZ * halfWidth},
                            normal, {1.0f, 1.0f}, rootColor});
        vertices.push_back({{rootOffsetX - sideX * halfWidth * 0.65f +
                                 normalX * naturalBend,
                             height * 0.55f,
                             rootOffsetZ - sideZ * halfWidth * 0.65f +
                                 normalZ * naturalBend},
                            normal, {0.18f, 0.45f}, midColor});
        vertices.push_back({{rootOffsetX + sideX * halfWidth * 0.65f +
                                 normalX * naturalBend,
                             height * 0.55f,
                             rootOffsetZ + sideZ * halfWidth * 0.65f +
                                 normalZ * naturalBend},
                            normal, {0.82f, 0.45f}, midColor});
        vertices.push_back({{rootOffsetX - sideX * tipHalfWidth +
                                 normalX * naturalBend * 1.8f,
                             height,
                             rootOffsetZ - sideZ * tipHalfWidth +
                                 normalZ * naturalBend * 1.8f},
                            normal, {0.32f, 0.0f}, tipColor});
        vertices.push_back({{rootOffsetX + sideX * tipHalfWidth +
                                 normalX * naturalBend * 1.8f,
                             height,
                             rootOffsetZ + sideZ * tipHalfWidth +
                                 normalZ * naturalBend * 1.8f},
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
      const float radialSlope = ring == 1 ? -0.18f : 0.08f;
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
