/**
 * @file GrassFieldBuilder.cpp
 * @brief 地形から手続き的に生やす芝の元データを作る処理の実装
 */

#include "GrassFieldBuilder.h"
#include "GrassFieldRules.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace game::systems {

namespace {

/** @brief TerrainMaterial（WikiComponents.h）の値 */
constexpr uint8_t kMaterialFairway = 0;
constexpr uint8_t kMaterialRough = 1;
constexpr uint8_t kMaterialGreen = 3;

constexpr float kInfiniteDistance = 1.0e9f;
/**
 * @brief 芝を生やさない所（バンカー・水など）の格子の中心から、生え始める距離と生えそろう距離[m]
 * @details 地形の色は 0.8m 間隔の頂点の間で混ざるので、境目の半ばまでは砂に見える。
 *          その外側から生やし始める。
 */
constexpr float kAllowedStartDistance = 0.55f;
constexpr float kAllowedFullDistance = 1.3f;

float SmoothStep(float edge0, float edge1, float x) {
  const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

uint8_t ToByte(float value) {
  return static_cast<uint8_t>(
      std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

/**
 * @brief 2パスのチャンファー距離変換。seed が真のテクセルからの距離[テクセル]を返す。
 */
std::vector<float> DistanceTransform(const std::vector<uint8_t> &seed,
                                     int width, int height) {
  constexpr float kStraight = 1.0f;
  constexpr float kDiagonal = 1.41421356f;
  std::vector<float> distance(seed.size(), kInfiniteDistance);
  for (size_t i = 0; i < seed.size(); ++i) {
    if (seed[i]) {
      distance[i] = 0.0f;
    }
  }
  auto at = [&](int x, int z) -> float & {
    return distance[static_cast<size_t>(z) * width + x];
  };
  for (int z = 0; z < height; ++z) {
    for (int x = 0; x < width; ++x) {
      float value = at(x, z);
      if (x > 0) value = std::min(value, at(x - 1, z) + kStraight);
      if (z > 0) {
        value = std::min(value, at(x, z - 1) + kStraight);
        if (x > 0) value = std::min(value, at(x - 1, z - 1) + kDiagonal);
        if (x + 1 < width) value = std::min(value, at(x + 1, z - 1) + kDiagonal);
      }
      at(x, z) = value;
    }
  }
  for (int z = height - 1; z >= 0; --z) {
    for (int x = width - 1; x >= 0; --x) {
      float value = at(x, z);
      if (x + 1 < width) value = std::min(value, at(x + 1, z) + kStraight);
      if (z + 1 < height) {
        value = std::min(value, at(x, z + 1) + kStraight);
        if (x + 1 < width) value = std::min(value, at(x + 1, z + 1) + kDiagonal);
        if (x > 0) value = std::min(value, at(x - 1, z + 1) + kDiagonal);
      }
      at(x, z) = value;
    }
  }
  return distance;
}

/** @brief 3x3 の箱型ぼかし（端はクランプ） */
void BoxBlur(std::vector<float> &values, int width, int height) {
  std::vector<float> temp(values.size());
  for (int z = 0; z < height; ++z) {
    for (int x = 0; x < width; ++x) {
      const int x0 = std::max(x - 1, 0);
      const int x1 = std::min(x + 1, width - 1);
      const size_t row = static_cast<size_t>(z) * width;
      temp[row + x] = (values[row + x0] + values[row + x] + values[row + x1]) /
                      3.0f;
    }
  }
  for (int z = 0; z < height; ++z) {
    const int z0 = std::max(z - 1, 0);
    const int z1 = std::min(z + 1, height - 1);
    for (int x = 0; x < width; ++x) {
      values[static_cast<size_t>(z) * width + x] =
          (temp[static_cast<size_t>(z0) * width + x] +
           temp[static_cast<size_t>(z) * width + x] +
           temp[static_cast<size_t>(z1) * width + x]) /
          3.0f;
    }
  }
}

float LayerDensity(GrassLayer layer, const GrassFieldTexel &texel) {
  return layer == GrassLayer::Rough ? GrassRoughLayerDensity(texel)
                                    : GrassTurfLayerDensity(texel);
}

void BuildTileGrid(components::GrassFieldData &field, GrassLayer layer,
                   float tileSize) {
  auto &grid = field.layers[static_cast<uint32_t>(layer)];
  grid.tileSize = tileSize;
  grid.resolutionX =
      std::max(1, static_cast<int>(std::ceil(field.width / tileSize)));
  grid.resolutionZ =
      std::max(1, static_cast<int>(std::ceil(field.depth / tileSize)));
  grid.tiles.assign(static_cast<size_t>(grid.resolutionX) * grid.resolutionZ,
                    DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f));

  const float maxZ = field.minZ + field.depth;
  const float fieldCellX = field.width / field.fieldResolutionX;
  const float fieldCellZ = field.depth / field.fieldResolutionZ;
  const float heightCellX = field.width / (field.heightResolutionX - 1);
  const float heightCellZ = field.depth / (field.heightResolutionZ - 1);
  // 境界のゆらぎ（GrassFieldCommon.hlsli）で読む位置がずれる分の余白
  constexpr float kMargin = 0.6f;

  for (int tz = 0; tz < grid.resolutionZ; ++tz) {
    for (int tx = 0; tx < grid.resolutionX; ++tx) {
      const float x0 = field.minX + tx * tileSize - kMargin;
      const float x1 = field.minX + (tx + 1) * tileSize + kMargin;
      const float z0 = field.minZ + tz * tileSize - kMargin;
      const float z1 = field.minZ + (tz + 1) * tileSize + kMargin;

      float density = 0.0f;
      const int fx0 = std::clamp(
          static_cast<int>(std::floor((x0 - field.minX) / fieldCellX)), 0,
          field.fieldResolutionX - 1);
      const int fx1 = std::clamp(
          static_cast<int>(std::floor((x1 - field.minX) / fieldCellX)), 0,
          field.fieldResolutionX - 1);
      const int fz0 = std::clamp(
          static_cast<int>(std::floor((maxZ - z1) / fieldCellZ)), 0,
          field.fieldResolutionZ - 1);
      const int fz1 = std::clamp(
          static_cast<int>(std::floor((maxZ - z0) / fieldCellZ)), 0,
          field.fieldResolutionZ - 1);
      for (int fz = fz0; fz <= fz1; ++fz) {
        for (int fx = fx0; fx <= fx1; ++fx) {
          const auto texel = DecodeGrassFieldTexel(
              field.fieldTexels[static_cast<size_t>(fz) *
                                    field.fieldResolutionX +
                                fx]);
          density = std::max(density, LayerDensity(layer, texel));
        }
      }
      if (density <= 0.0f) {
        continue;
      }

      float minHeight = (std::numeric_limits<float>::max)();
      float maxHeight = (std::numeric_limits<float>::lowest)();
      const int hx0 = std::clamp(
          static_cast<int>(std::floor((x0 - field.minX) / heightCellX)), 0,
          field.heightResolutionX - 1);
      const int hx1 = std::clamp(
          static_cast<int>(std::ceil((x1 - field.minX) / heightCellX)), 0,
          field.heightResolutionX - 1);
      const int hz0 = std::clamp(
          static_cast<int>(std::floor((maxZ - z1) / heightCellZ)), 0,
          field.heightResolutionZ - 1);
      const int hz1 = std::clamp(
          static_cast<int>(std::ceil((maxZ - z0) / heightCellZ)), 0,
          field.heightResolutionZ - 1);
      for (int hz = hz0; hz <= hz1; ++hz) {
        for (int hx = hx0; hx <= hx1; ++hx) {
          const float h =
              field.heights[static_cast<size_t>(hz) * field.heightResolutionX +
                            hx];
          minHeight = std::min(minHeight, h);
          maxHeight = std::max(maxHeight, h);
        }
      }
      grid.tiles[static_cast<size_t>(tz) * grid.resolutionX + tx] =
          DirectX::XMFLOAT4(minHeight + field.heightOffset,
                            maxHeight + field.heightOffset, density, 0.0f);
    }
  }
}

} // namespace

GrassFieldTexel DecodeGrassFieldTexel(uint32_t packed) {
  GrassFieldTexel texel;
  texel.rough = static_cast<float>(packed & 0xffu) / 255.0f;
  texel.turf = static_cast<float>((packed >> 8u) & 0xffu) / 255.0f;
  texel.green = static_cast<float>((packed >> 16u) & 0xffu) / 255.0f;
  texel.allowed = static_cast<float>((packed >> 24u) & 0xffu) / 255.0f;
  return texel;
}

float GrassRoughLayerDensity(const GrassFieldTexel &texel) {
  return std::clamp(texel.rough / 0.2f, 0.0f, 1.0f) * texel.allowed;
}

float GrassTurfLayerDensity(const GrassFieldTexel &texel) {
  return std::clamp(texel.turf, 0.0f, 1.0f) * texel.allowed;
}

float GrassSemiRoughFactor(float distanceToRough) {
  return 0.55f * (1.0f - SmoothStep(0.9f, 1.8f, distanceToRough));
}

std::shared_ptr<components::GrassFieldData>
BuildGrassField(const GrassFieldTerrainView &terrain,
                const GrassFieldBuildOptions &options) {
  if (!terrain.heights || !terrain.materials || terrain.resolutionX < 2 ||
      terrain.resolutionZ < 2 || terrain.width <= 0.0f ||
      terrain.depth <= 0.0f) {
    return nullptr;
  }

  auto field = std::make_shared<components::GrassFieldData>();
  field->minX = -terrain.width * 0.5f;
  field->minZ = -terrain.depth * 0.5f;
  field->width = terrain.width;
  field->depth = terrain.depth;
  field->heightOffset = options.heightOffset;
  field->heightResolutionX = terrain.resolutionX;
  field->heightResolutionZ = terrain.resolutionZ;
  field->heights.assign(
      terrain.heights,
      terrain.heights +
          static_cast<size_t>(terrain.resolutionX) * terrain.resolutionZ);

  const int resX = std::max(
      2, static_cast<int>(std::ceil(terrain.width / kGrassFieldCellSize)));
  const int resZ = std::max(
      2, static_cast<int>(std::ceil(terrain.depth / kGrassFieldCellSize)));
  field->fieldResolutionX = resX;
  field->fieldResolutionZ = resZ;
  const size_t texelCount = static_cast<size_t>(resX) * resZ;
  const float cellX = terrain.width / resX;
  const float cellZ = terrain.depth / resZ;
  const float cellSize = 0.5f * (cellX + cellZ);

  std::vector<uint8_t> isRough(texelCount, 0);
  std::vector<uint8_t> isTurf(texelCount, 0);
  std::vector<uint8_t> isGreen(texelCount, 0);
  std::vector<uint8_t> isBlocked(texelCount, 0);
  for (int z = 0; z < resZ; ++z) {
    const float v = (z + 0.5f) / resZ;
    const int gz = std::clamp(
        static_cast<int>(v * (terrain.resolutionZ - 1) + 0.5f), 0,
        terrain.resolutionZ - 1);
    for (int x = 0; x < resX; ++x) {
      const float u = (x + 0.5f) / resX;
      const int gx = std::clamp(
          static_cast<int>(u * (terrain.resolutionX - 1) + 0.5f), 0,
          terrain.resolutionX - 1);
      const uint8_t material =
          terrain.materials[static_cast<size_t>(gz) * terrain.resolutionX + gx];
      const size_t index = static_cast<size_t>(z) * resX + x;
      const bool grass = material == kMaterialFairway ||
                         material == kMaterialRough ||
                         material == kMaterialGreen;
      if (options.everythingIsRough) {
        isRough[index] = 1;
        continue;
      }
      isRough[index] = material == kMaterialRough ? 1 : 0;
      isTurf[index] =
          (material == kMaterialFairway || material == kMaterialGreen) ? 1 : 0;
      isGreen[index] = material == kMaterialGreen ? 1 : 0;
      isBlocked[index] = grass ? 0 : 1;
    }
  }

  const std::vector<float> roughDistance =
      DistanceTransform(isRough, resX, resZ);
  const std::vector<float> blockedDistance =
      DistanceTransform(isBlocked, resX, resZ);

  std::vector<float> rough(texelCount);
  std::vector<float> turf(texelCount);
  std::vector<float> green(texelCount);
  std::vector<float> allowed(texelCount);
  for (size_t i = 0; i < texelCount; ++i) {
    if (isRough[i]) {
      rough[i] = 1.0f;
    } else if (isTurf[i]) {
      rough[i] = GrassSemiRoughFactor(roughDistance[i] * cellSize);
    }
    turf[i] = isTurf[i] ? 1.0f : 0.0f;
    green[i] = isGreen[i] ? 1.0f : 0.0f;
    allowed[i] = isBlocked[i] ? 0.0f
                              : SmoothStep(kAllowedStartDistance, kAllowedFullDistance,
                                           blockedDistance[i] * cellSize);
  }
  BoxBlur(rough, resX, resZ);
  BoxBlur(turf, resX, resZ);
  BoxBlur(green, resX, resZ);
  // ぼかしで生やさない所の側へ染み出さないよう、ぼかす前の値を超えないようにする
  const std::vector<float> sharpAllowed = allowed;
  BoxBlur(allowed, resX, resZ);
  for (size_t i = 0; i < texelCount; ++i) {
    allowed[i] = std::min(allowed[i], sharpAllowed[i]);
  }

  field->fieldTexels.resize(texelCount);
  for (size_t i = 0; i < texelCount; ++i) {
    field->fieldTexels[i] = static_cast<uint32_t>(ToByte(rough[i])) |
                            (static_cast<uint32_t>(ToByte(turf[i])) << 8u) |
                            (static_cast<uint32_t>(ToByte(green[i])) << 16u) |
                            (static_cast<uint32_t>(ToByte(allowed[i])) << 24u);
  }

  BuildTileGrid(*field, GrassLayer::Rough,
                GetGrassLayerParams(core::GraphicsPreset::Ultra,
                                    GrassLayer::Rough)
                    .tileSize);
  BuildTileGrid(*field, GrassLayer::Turf,
                GetGrassLayerParams(core::GraphicsPreset::Ultra,
                                    GrassLayer::Turf)
                    .tileSize);
  return field;
}

GrassFieldTexel SampleGrassFieldTexel(const components::GrassFieldData &field,
                                      float x, float z) {
  const float u = (x - field.minX) / field.width;
  const float v = (field.minZ + field.depth - z) / field.depth;
  if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f ||
      field.fieldTexels.empty()) {
    return {};
  }
  const int fx = std::clamp(static_cast<int>(u * field.fieldResolutionX), 0,
                            field.fieldResolutionX - 1);
  const int fz = std::clamp(static_cast<int>(v * field.fieldResolutionZ), 0,
                            field.fieldResolutionZ - 1);
  return DecodeGrassFieldTexel(
      field.fieldTexels[static_cast<size_t>(fz) * field.fieldResolutionX + fx]);
}

float SampleGrassFieldHeight(const components::GrassFieldData &field, float x,
                             float z) {
  const float u = (x - field.minX) / field.width;
  const float v = (field.minZ + field.depth - z) / field.depth;
  const int resX = field.heightResolutionX;
  const int resZ = field.heightResolutionZ;
  const float fx = std::clamp(u, 0.0f, 1.0f) * (resX - 1);
  const float fz = std::clamp(v, 0.0f, 1.0f) * (resZ - 1);
  const int ix = std::clamp(static_cast<int>(fx), 0, resX - 2);
  const int iz = std::clamp(static_cast<int>(fz), 0, resZ - 2);
  const float dx = fx - ix;
  const float dz = fz - iz;
  const auto h = [&](int px, int pz) {
    return field.heights[static_cast<size_t>(pz) * resX + px];
  };
  const float h0 = h(ix, iz) * (1.0f - dx) + h(ix + 1, iz) * dx;
  const float h1 = h(ix, iz + 1) * (1.0f - dx) + h(ix + 1, iz + 1) * dx;
  return h0 * (1.0f - dz) + h1 * dz + field.heightOffset;
}

} // namespace game::systems
