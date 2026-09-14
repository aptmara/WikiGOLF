/**
 * @file TerrainBackdrop.cpp
 * @brief コースの四方に広がる、見た目専用の延長地形（山並み）を生成します。
 */

#include "TerrainBackdrop.h"
#include "TerrainGeneratorInternals.h"
#include "TerrainMaterialAssets.h"
#include "../../graphics/TangentGenerator.h"
#include <algorithm>
#include <cmath>

namespace game::systems {
namespace {

constexpr uint8_t kFairway = 0;
constexpr uint8_t kRough = 1;
constexpr uint8_t kSand = 2;
constexpr uint8_t kGreen = 3;
constexpr uint8_t kIce = 4;
constexpr uint8_t kStone = 7;

/** @brief コース格子の連続座標（範囲内に丸めたもの）。 */
struct GridPosition {
  int x0, z0, x1, z1;
  float tx, tz;
};

GridPosition ToGrid(const TerrainData &data, float worldX, float worldZ) {
  const int resX = data.config.resolutionX;
  const int resZ = data.config.resolutionZ;
  const float fx = std::clamp((worldX / data.config.worldWidth + 0.5f) * (resX - 1),
                              0.0f, static_cast<float>(resX - 1));
  const float fz = std::clamp((0.5f - worldZ / data.config.worldDepth) * (resZ - 1),
                              0.0f, static_cast<float>(resZ - 1));
  GridPosition g;
  g.x0 = std::min(static_cast<int>(fx), resX - 2);
  g.z0 = std::min(static_cast<int>(fz), resZ - 2);
  g.x1 = g.x0 + 1;
  g.z1 = g.z0 + 1;
  g.tx = fx - g.x0;
  g.tz = fz - g.z0;
  return g;
}

/** @brief コース地形の高さ（範囲外は縁へ丸める）。 */
float CourseHeight(const TerrainData &data, float worldX, float worldZ) {
  const int resX = data.config.resolutionX;
  const GridPosition g = ToGrid(data, worldX, worldZ);
  const auto &h = data.heightMap;
  return Lerp(Lerp(h[g.z0 * resX + g.x0], h[g.z0 * resX + g.x1], g.tx),
              Lerp(h[g.z1 * resX + g.x0], h[g.z1 * resX + g.x1], g.tx), g.tz);
}

/** @brief コース地形の最寄りの地表マテリアル。 */
uint8_t CourseMaterial(const TerrainData &data, float worldX, float worldZ) {
  const int resX = data.config.resolutionX;
  const GridPosition g = ToGrid(data, worldX, worldZ);
  const int x = g.tx < 0.5f ? g.x0 : g.x1;
  const int z = g.tz < 0.5f ? g.z0 : g.z1;
  return data.materialMap[z * resX + x];
}

/** @brief コース地形の描画色（なければマテリアルの色）。 */
DirectX::XMFLOAT3 CourseColor(const TerrainData &data, float worldX, float worldZ) {
  const int resX = data.config.resolutionX;
  if (data.visualMaterialColors.size() != data.heightMap.size()) {
    return TerrainMaterialMapColor(CourseMaterial(data, worldX, worldZ));
  }
  const GridPosition g = ToGrid(data, worldX, worldZ);
  const auto &c = data.visualMaterialColors;
  auto mix = [&](const DirectX::XMFLOAT3 &a, const DirectX::XMFLOAT3 &b, float t) {
    return DirectX::XMFLOAT3{Lerp(a.x, b.x, t), Lerp(a.y, b.y, t), Lerp(a.z, b.z, t)};
  };
  return mix(mix(c[g.z0 * resX + g.x0], c[g.z0 * resX + g.x1], g.tx),
             mix(c[g.z1 * resX + g.x0], c[g.z1 * resX + g.x1], g.tx), g.tz);
}

/**
 * @brief 座標を回転・歪ませてから値ノイズを取ります。
 * @details 値ノイズは格子に沿った四角い塊が見えやすいので、角度を変えて重ねる。
 */
float OrganicNoise(float x, float z, float scale, float angle, uint32_t seed) {
  const float c = std::cos(angle);
  const float s = std::sin(angle);
  float u = (c * x - s * z) / scale;
  float v = (s * x + c * z) / scale;
  const float warpU = ValueNoise(u * 0.5f + 11.7f, v * 0.5f, seed ^ 0xa1u);
  const float warpV = ValueNoise(u * 0.5f, v * 0.5f - 7.3f, seed ^ 0xb2u);
  u += warpU * 0.8f;
  v += warpV * 0.8f;
  return ValueNoise(u, v, seed) * 0.75f +
         ValueNoise(v * 2.1f + 3.1f, u * 2.1f, seed ^ 0xc3u) * 0.25f;
}

struct BackdropStyle {
  float amplitude;   // 最も高い峰の高さ
  float ridgeWeight; // 岩の尾根の鋭さ
  float terrace;     // 段丘の段の高さ（0 で無効）
};

BackdropStyle StyleForBiome(int biome) {
  switch (biome) {
  case 1: return {42.0f, 0.10f, 7.0f};  // 砂漠: 段になったメサ
  case 2: return {48.0f, 0.22f, 0.0f};  // 氷原: 雪をかぶった峰
  case 3: return {62.0f, 0.40f, 0.0f};  // 岩場: 切り立った岩山
  default: return {42.0f, 0.16f, 0.0f}; // 草原: 緑の山
  }
}

/** @brief 標高（峰に対する割合）ごとの地表の段。低い方から並べる。 */
struct MountainBand {
  float threshold; // この割合以上でこの地表になる
  uint8_t material;
};

std::vector<MountainBand> MountainBands(int biome) {
  switch (biome) {
  case 1: return {{0.0f, kSand}, {0.7f, kStone}};
  case 2: return {{0.0f, kRough}, {0.2f, kStone}, {0.38f, kIce}};
  case 3: return {{0.0f, kRough}, {0.12f, kStone}};
  default: return {{0.0f, kRough}, {0.62f, kStone}};
  }
}

/** @brief 山並みの地表マテリアル。境目はノイズで散らし、等高線状の直線にしない。 */
uint8_t MountainMaterial(int biome, float relative, bool riser, float jitter) {
  if (biome == 1 && riser) {
    return kStone; // メサの崖は岩肌
  }
  const auto bands = MountainBands(biome);
  const float jittered = relative + jitter * 0.12f;
  uint8_t material = bands.front().material;
  for (const auto &band : bands) {
    if (jittered >= band.threshold) material = band.material;
  }
  return material;
}

/** @brief 山並みの地表の色。段の境目をなめらかに混ぜる。 */
DirectX::XMFLOAT3 MountainColor(int biome, float relative, bool riser) {
  const auto bands = MountainBands(biome);
  DirectX::XMFLOAT3 color = TerrainMaterialMapColor(bands.front().material);
  for (size_t i = 1; i < bands.size(); ++i) {
    const float t = SmoothStep(bands[i].threshold - 0.08f,
                               bands[i].threshold + 0.08f, relative);
    const DirectX::XMFLOAT3 next = TerrainMaterialMapColor(bands[i].material);
    color = {Lerp(color.x, next.x, t), Lerp(color.y, next.y, t), Lerp(color.z, next.z, t)};
  }
  if (biome == 1 && riser) {
    const DirectX::XMFLOAT3 stone = TerrainMaterialMapColor(kStone);
    color = {Lerp(color.x, stone.x, 0.7f), Lerp(color.y, stone.y, 0.7f),
             Lerp(color.z, stone.z, 0.7f)};
  }
  return color;
}

} // namespace

TerrainBackdropSample SampleTerrainBackdrop(const TerrainData &data, float worldX,
                                            float worldZ, uint32_t seed) {
  TerrainBackdropSample sample;
  const float halfW = data.config.worldWidth * 0.5f;
  const float halfD = data.config.worldDepth * 0.5f;

  // コース上の最寄り点（縁）と、そこからの距離
  const float edgeX = std::clamp(worldX, -halfW, halfW);
  const float edgeZ = std::clamp(worldZ, -halfD, halfD);
  const float offX = worldX - edgeX;
  const float offZ = worldZ - edgeZ;
  const float distance = std::sqrt(offX * offX + offZ * offZ);
  if (distance <= 0.0f) {
    sample.height = CourseHeight(data, worldX, worldZ);
    sample.material = CourseMaterial(data, worldX, worldZ);
    sample.color = CourseColor(data, worldX, worldZ);
    return sample;
  }

  // 延長地形（コースと同じ規則でコースの外まで生成した地形）をそのまま使う。
  // 縁ではコースの実際の高さ・色とずれるので、その差を縁から少しの距離で消していく。
  const float seamFade = 1.0f - SmoothStep(0.0f, kTerrainBackdropSeam, distance);
  const TerrainData *extension = data.extension.get();
  float ground = CourseHeight(data, edgeX, edgeZ);
  uint8_t groundMaterial = CourseMaterial(data, edgeX, edgeZ);
  DirectX::XMFLOAT3 groundColor = CourseColor(data, edgeX, edgeZ);
  if (extension) {
    const float edgeOffset = ground - CourseHeight(*extension, edgeX, edgeZ);
    ground = CourseHeight(*extension, worldX, worldZ) + edgeOffset * seamFade;
    const DirectX::XMFLOAT3 extensionEdge = CourseColor(*extension, edgeX, edgeZ);
    const DirectX::XMFLOAT3 extensionHere = CourseColor(*extension, worldX, worldZ);
    groundColor = {extensionHere.x + (groundColor.x - extensionEdge.x) * seamFade,
                   extensionHere.y + (groundColor.y - extensionEdge.y) * seamFade,
                   extensionHere.z + (groundColor.z - extensionEdge.z) * seamFade};
    const float seamDither =
        OrganicNoise(worldX, worldZ, 3.0f, 0.4f, seed ^ 0x1fu) * 0.5f + 0.5f;
    if (seamDither >= seamFade) {
      groundMaterial = CourseMaterial(*extension, worldX, worldZ);
    }
  }
  if (groundMaterial == kGreen) {
    groundMaterial = kFairway; // グリーンはコース内だけのもの
  }

  // 山並み。ノイズはワールド座標で取るので、四方と角で形がつながる。
  const BackdropStyle style = StyleForBiome(data.config.biome);
  const float group = OrganicNoise(worldX, worldZ, 210.0f, 0.41f, seed) * 0.7f +
                      OrganicNoise(worldX, worldZ, 90.0f, 1.13f, seed ^ 0x5au) * 0.3f;
  const float peakHeight =
      style.amplitude * std::clamp(0.62f + 0.5f * group, 0.2f, 1.1f);
  // 縁の近くは延長地形そのものを見せ、その先から山並みを重ねる。
  const float rise = SmoothStep(kTerrainBackdropBlend * 0.3f,
                                kTerrainBackdropReach * 0.9f, distance);
  const float body = OrganicNoise(worldX, worldZ, 55.0f, 0.73f, seed ^ 0x13u);
  const float ridgeNoise = OrganicNoise(worldX, worldZ, 32.0f, 2.27f, seed ^ 0x77u);
  const float ridge = 1.0f - std::min(std::abs(ridgeNoise) * 1.4f, 1.0f);
  float mountain = peakHeight * rise * (0.78f + 0.22f * body) +
                   style.amplitude * style.ridgeWeight * ridge * ridge * rise;

  bool riser = false;
  if (style.terrace > 0.0f && mountain > 0.0f) {
    // メサ: 平らな段と急な崖を交互に作る。
    const float level = mountain / style.terrace;
    const float floorLevel = std::floor(level);
    const float frac = level - floorLevel;
    riser = frac > 0.72f;
    mountain = (floorLevel + SmoothStep(0.72f, 1.0f, frac)) * style.terrace;
  }
  sample.height = ground + mountain;

  // 地表: 低いところは延長地形の地表、山が高くなるほど山の地表（岩・雪など）にする。
  const float relative = mountain / std::max(style.amplitude, 1.0f);
  // 境目のゆらぎは山が育つにつれて効かせ、縁（山の高さ 0）では延長地形の地表のままにする。
  const float bandJitter = OrganicNoise(worldX, worldZ, 9.0f, 1.9f, seed ^ 0x6eu) *
                           SmoothStep(0.0f, 0.08f, relative);
  const auto bands = MountainBands(data.config.biome);
  const float firstBand = bands.size() > 1 ? bands[1].threshold : 1.0f;
  const bool mountainSurface =
      (data.config.biome == 1 && riser) || relative + bandJitter * 0.12f >= firstBand;
  sample.material = mountainSurface
                        ? MountainMaterial(data.config.biome, relative, riser, bandJitter)
                        : groundMaterial;
  const DirectX::XMFLOAT3 mountainColor =
      MountainColor(data.config.biome, relative + bandJitter * 0.12f, riser);
  const float mountainMix =
      SmoothStep(firstBand - 0.1f, firstBand + 0.05f, relative + bandJitter * 0.12f);
  sample.color = {Lerp(groundColor.x, mountainColor.x, mountainMix),
                  Lerp(groundColor.y, mountainColor.y, mountainMix),
                  Lerp(groundColor.z, mountainColor.z, mountainMix)};
  return sample;
}

std::vector<TerrainBackdropChunk> BuildTerrainBackdrop(const TerrainData &data,
                                                       uint32_t seed) {
  std::vector<TerrainBackdropChunk> chunks;
  const int resX = data.config.resolutionX;
  const int resZ = data.config.resolutionZ;
  const float worldW = data.config.worldWidth;
  const float worldD = data.config.worldDepth;
  if (resX < 2 || resZ < 2 || worldW <= 0.0f || worldD <= 0.0f ||
      data.heightMap.size() < static_cast<size_t>(resX) * resZ) {
    return chunks;
  }
  const float halfW = worldW * 0.5f;
  const float halfD = worldD * 0.5f;

  constexpr int kOuterSteps = 44; // 縁から外側への分割数（縁の近くほど細かい）
  auto outerAxis = [&](float edge, float direction) {
    // direction = -1 なら座標の小さい側。戻り値は昇順。
    std::vector<float> axis;
    axis.reserve(kOuterSteps + 1);
    for (int i = 0; i <= kOuterSteps; ++i) {
      const float t = direction < 0.0f
                          ? 1.0f - static_cast<float>(i) / kOuterSteps
                          : static_cast<float>(i) / kOuterSteps;
      axis.push_back(edge + direction * kTerrainBackdropReach * t * t);
    }
    return axis;
  };

  // コースの縁に沿う軸は、コースの格子点（奥行きは 1 つおき）に合わせて隙間を作らない。
  std::vector<float> interiorX;
  interiorX.reserve(resX);
  for (int x = 0; x < resX; ++x) {
    interiorX.push_back((static_cast<float>(x) / (resX - 1) - 0.5f) * worldW);
  }
  std::vector<float> interiorZ;
  for (int z = resZ - 1; z >= 0; z -= 2) {
    interiorZ.push_back((0.5f - static_cast<float>(z) / (resZ - 1)) * worldD);
  }
  if (interiorZ.back() < halfD) {
    interiorZ.push_back(halfD);
  }

  const std::vector<float> leftX = outerAxis(-halfW, -1.0f);
  const std::vector<float> rightX = outerAxis(halfW, 1.0f);
  const std::vector<float> backZ = outerAxis(-halfD, -1.0f);
  const std::vector<float> frontZ = outerAxis(halfD, 1.0f);
  std::vector<float> fullX = leftX;
  fullX.insert(fullX.end(), interiorX.begin() + 1, interiorX.end() - 1);
  fullX.insert(fullX.end(), rightX.begin(), rightX.end());

  auto buildGrid = [&](const std::vector<float> &xs, const std::vector<float> &zs,
                       size_t rowBegin, size_t rowEnd) {
    TerrainBackdropChunk chunk;
    const size_t columns = xs.size();
    const size_t rows = rowEnd - rowBegin + 1;
    chunk.vertices.reserve(columns * rows);
    for (size_t r = rowBegin; r <= rowEnd; ++r) {
      const float worldZ = zs[r];
      for (float worldX : xs) {
        const TerrainBackdropSample s = SampleTerrainBackdrop(data, worldX, worldZ, seed);
        constexpr float kProbe = 1.0f;
        const float hX0 = SampleTerrainBackdrop(data, worldX - kProbe, worldZ, seed).height;
        const float hX1 = SampleTerrainBackdrop(data, worldX + kProbe, worldZ, seed).height;
        const float hZ0 = SampleTerrainBackdrop(data, worldX, worldZ - kProbe, seed).height;
        const float hZ1 = SampleTerrainBackdrop(data, worldX, worldZ + kProbe, seed).height;
        DirectX::XMVECTOR n =
            DirectX::XMVectorSet(-(hX1 - hX0), 2.0f * kProbe, -(hZ1 - hZ0), 0.0f);
        n = DirectX::XMVector3Normalize(n);

        graphics::Vertex vertex;
        vertex.position = {worldX, s.height, worldZ};
        DirectX::XMStoreFloat3(&vertex.normal, n);
        // 地形タイルと同じく 2m で 1 回繰り返す座標系にする。
        vertex.texCoord = {(worldX + halfW) * 0.5f, (halfD - worldZ) * 0.5f};
        vertex.color = {s.color.x, s.color.y, s.color.z,
                        (static_cast<float>(s.material) + 0.5f) / 255.0f};
        chunk.vertices.push_back(vertex);
      }
    }
    const uint32_t stride = static_cast<uint32_t>(columns);
    chunk.indices.reserve((rows - 1) * (columns - 1) * 6);
    for (uint32_t r = 0; r + 1 < rows; ++r) {
      for (uint32_t c = 0; c + 1 < columns; ++c) {
        const uint32_t i0 = r * stride + c;
        const uint32_t i1 = i0 + 1;
        const uint32_t i2 = i0 + stride;
        const uint32_t i3 = i2 + 1;
        // 地形タイルは「列 +X・行 -Z」。ここでは行が +Z へ進むので巻き順を反転する。
        chunk.indices.insert(chunk.indices.end(), {i0, i2, i1, i1, i2, i3});
      }
    }
    graphics::ComputeTangents(chunk.vertices, chunk.indices);
    chunks.push_back(std::move(chunk));
  };

  // 左右の帯（コースの奥行きの範囲）。奥行き方向に分割する。
  constexpr size_t kRowsPerChunk = 120;
  for (const auto *xs : {&leftX, &rightX}) {
    for (size_t row = 0; row + 1 < interiorZ.size(); row += kRowsPerChunk) {
      buildGrid(*xs, interiorZ, row, std::min(interiorZ.size() - 1, row + kRowsPerChunk));
    }
  }
  // 手前と奥の帯（角を含む全幅）
  buildGrid(fullX, backZ, 0, backZ.size() - 1);
  buildGrid(fullX, frontZ, 0, frontZ.size() - 1);
  return chunks;
}

} // namespace game::systems
