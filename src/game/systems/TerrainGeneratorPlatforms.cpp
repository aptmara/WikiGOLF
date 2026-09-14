/**
 * @file TerrainGeneratorPlatforms.cpp
 * @brief TerrainGeneratorPlatforms の実装
*/

#include "TerrainGeneratorInternals.h"
#include "TerrainGenerator.h"
#include "../../core/Logger.h"
#include "../../graphics/TangentGenerator.h"
#include "../utils/GolfCupPhysics.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace game::systems {

using namespace DirectX;

float BaseGreenRadius(float worldWidth, float worldDepth, size_t holeCount) {
  if (holeCount == 0) {
    return 7.5f;
  }
  const float areaPerHole =
      worldWidth * worldDepth / static_cast<float>(holeCount);
  return std::clamp(std::sqrt(areaPerHole) * 0.55f, 3.5f, 7.5f);
}

void TerrainGenerator::CreatePlatforms(
    TerrainData &data, const std::vector<DirectX::XMFLOAT2> &holePositions) {
  const int resX = data.config.resolutionX;
  const int resZ = data.config.resolutionZ;
  const float worldW = data.config.worldWidth;
  const float worldD = data.config.worldDepth;
  if (resX < 2 || resZ < 2 || worldW <= 0.0f || worldD <= 0.0f ||
      holePositions.empty()) {
    return;
  }

  struct BunkerPatch {
    float centerX;
    float centerZ;
    float radiusX;
    float radiusZ;
    float cosAngle;
    float sinAngle;
  };

  // 形状はすべてメートル単位で決める。格子数で決めると縦長フィールドで
  // 縦方向だけ数十メートルに引き伸ばされ、段差の原因になる。
  const float cellX = worldW / static_cast<float>(resX - 1);
  const float cellZ = worldD / static_cast<float>(resZ - 1);
  const float areaPerHole =
      worldW * worldD / static_cast<float>(holePositions.size());
  const float baseGreenRadius =
      BaseGreenRadius(worldW, worldD, holePositions.size());
  // 芝のグリーン（マテリアル）はカップ周りだけにする。平らにならす範囲は
  // baseGreenRadius のまま広く取り、地表はバイオームの模様を残す。
  // ホールが密集する記事では小さく、まばらな記事では従来どおり大きなグリーンにする。
  const float greenMaterialRadius =
      std::clamp(std::sqrt(areaPerHole) * 0.24f, 1.4f, baseGreenRadius);
  const float greenMaterialScale = greenMaterialRadius / baseGreenRadius;
  // バンカーはホールがまばらなときだけ多めに置き、密集した記事ではまれにする。
  const float bunkerChance = std::clamp(areaPerHole / 900.0f, 0.05f, 1.0f);
  // カップのすぐ近くに水や溶岩（OB）を残さない範囲と、代わりに置く安全な地表。
  const float obFreeRadius = std::max(4.5f, greenMaterialRadius + 3.0f);
  const uint8_t obReplacement = data.config.biome == 2   ? uint8_t{4}
                                : data.config.biome == 3 ? uint8_t{7}
                                                         : uint8_t{1};

  // 高さは元地形を基準にし、ホールごとの効果を積み重ねずに合成する。
  const std::vector<float> original = data.heightMap;
  const size_t cellCount = original.size();
  std::vector<float> greenWeight(cellCount, 0.0f);
  std::vector<float> greenTargetSum(cellCount, 0.0f);
  std::vector<float> greenTargetWeight(cellCount, 0.0f);
  std::vector<float> depression(cellCount, 0.0f);

  auto sampleOriginal = [&](float wx, float wz) {
    const float fx = std::clamp((wx / worldW + 0.5f) * (resX - 1), 0.0f,
                                static_cast<float>(resX - 1));
    const float fz = std::clamp((0.5f - wz / worldD) * (resZ - 1), 0.0f,
                                static_cast<float>(resZ - 1));
    const int ix = std::clamp(static_cast<int>(fx), 0, resX - 2);
    const int iz = std::clamp(static_cast<int>(fz), 0, resZ - 2);
    const float tx = fx - ix;
    const float tz = fz - iz;
    return Lerp(Lerp(original[iz * resX + ix], original[iz * resX + ix + 1], tx),
                Lerp(original[(iz + 1) * resX + ix],
                     original[(iz + 1) * resX + ix + 1], tx),
                tz);
  };

  for (const auto &pos : holePositions) {
    const float u = pos.x / worldW + 0.5f;
    const float v = 0.5f - pos.y / worldD;
    const int cx = std::clamp(static_cast<int>(u * (resX - 1)), 0, resX - 1);
    const int cz = std::clamp(static_cast<int>(v * (resZ - 1)), 0, resZ - 1);

    std::mt19937 tempRng(cx * 73856093u ^ cz * 19349663u ^
                         data.config.biome * 83492791u);
    std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
    const uint32_t patchSeed = tempRng();

    const float radius = baseGreenRadius * (0.85f + dist01(tempRng) * 0.35f);
    const float targetHeight = sampleOriginal(pos.x, pos.y) + 0.05f;
    // グリーンは水平にする。周囲が傾いているほど縁のなじませ帯を広げ、
    // 平らなグリーンと周りの地形の間に段差を作らない。
    const float gradientSpan = std::max(radius, 2.0f);
    const float slopeX = (sampleOriginal(pos.x + gradientSpan, pos.y) -
                          sampleOriginal(pos.x - gradientSpan, pos.y)) /
                         (2.0f * gradientSpan);
    const float slopeZ = (sampleOriginal(pos.x, pos.y + gradientSpan) -
                          sampleOriginal(pos.x, pos.y - gradientSpan)) /
                         (2.0f * gradientSpan);
    const float rimDifference =
        std::sqrt(slopeX * slopeX + slopeZ * slopeZ) * radius;
    const float greenRadiusX = radius * (0.88f + dist01(tempRng) * 0.22f);
    const float greenRadiusZ = radius * (0.78f + dist01(tempRng) * 0.28f);
    const float greenAngle = (dist01(tempRng) - 0.5f) * 0.8f;
    const float greenCos = std::cos(greenAngle);
    const float greenSin = std::sin(greenAngle);
    // なじませ帯の幅（メートル）。縁の高さの差の 3 倍以上、最低 2.5m。
    const float fringeMeters = std::clamp(rimDifference * 3.0f, 2.5f, 10.0f);
    const float fringeRadius = 1.0f + fringeMeters / radius;

    int bunkerCount = 1 + static_cast<int>(dist01(tempRng) * 2.0f);
    if (data.config.biome == 2) {
      bunkerCount = std::max(1, bunkerCount - 1);
    }
    if (dist01(tempRng) > bunkerChance) {
      bunkerCount = 0;
    }

    std::vector<BunkerPatch> bunkers;
    bunkers.reserve(bunkerCount);
    const float bunkerStartAngle = dist01(tempRng) * 6.28318f;
    for (int b = 0; b < bunkerCount; ++b) {
      const float angle = bunkerStartAngle +
                          (6.28318f / static_cast<float>(bunkerCount)) * b +
                          (dist01(tempRng) - 0.5f) * 0.7f;
      const float distance = radius * (1.45f + dist01(tempRng) * 0.70f);
      const float patchAngle = angle + (dist01(tempRng) - 0.5f) * 1.0f;
      BunkerPatch patch;
      patch.centerX = std::cos(angle) * distance;
      patch.centerZ = std::sin(angle) * distance;
      patch.radiusX = std::max(radius * (0.38f + dist01(tempRng) * 0.24f),
                               cellX * 2.0f);
      patch.radiusZ = std::max(radius * (0.26f + dist01(tempRng) * 0.18f),
                               cellX * 1.5f);
      patch.cosAngle = std::cos(patchAngle);
      patch.sinAngle = std::sin(patchAngle);
      bunkers.push_back(patch);
    }

    const float extent =
        std::max({radius * 2.8f, radius * fringeRadius, obFreeRadius});
    const int minX = std::max(0, static_cast<int>(std::floor((pos.x - extent) / cellX +
                                                            (resX - 1) * 0.5f)));
    const int maxX = std::min(resX - 1, static_cast<int>(std::ceil((pos.x + extent) / cellX +
                                                                  (resX - 1) * 0.5f)));
    const int minZ = std::max(0, static_cast<int>(std::floor((worldD * 0.5f - pos.y - extent) / cellZ)));
    const int maxZ = std::min(resZ - 1, static_cast<int>(std::ceil((worldD * 0.5f - pos.y + extent) / cellZ)));

    for (int z = minZ; z <= maxZ; ++z) {
      const float worldZ =
          (0.5f - static_cast<float>(z) / static_cast<float>(resZ - 1)) * worldD;
      for (int x = minX; x <= maxX; ++x) {
        const float worldX =
            (static_cast<float>(x) / static_cast<float>(resX - 1) - 0.5f) *
            worldW;
        const float dx = worldX - pos.x;
        const float dz = worldZ - pos.y;
        const int idx = z * resX + x;
        if ((data.materialMap[idx] == 5 || data.materialMap[idx] == 6) &&
            dx * dx + dz * dz < obFreeRadius * obFreeRadius) {
          data.materialMap[idx] = obReplacement;
        }
        const float greenLocalX = greenCos * dx + greenSin * dz;
        const float greenLocalZ = -greenSin * dx + greenCos * dz;
        const float greenDistance =
            std::sqrt((greenLocalX * greenLocalX) /
                          (greenRadiusX * greenRadiusX) +
                      (greenLocalZ * greenLocalZ) /
                          (greenRadiusZ * greenRadiusZ));
        const float boundaryNoise =
            FractalNoise(worldX * 0.2f, worldZ * 0.2f, patchSeed) * 0.07f;
        const float shapedDistance = greenDistance - boundaryNoise;

        // グリーン本体からフリンジの外側まで、なめらかに元地形へ戻す。
        if (shapedDistance < fringeRadius) {
          const float weight = SmoothStep(fringeRadius, 1.0f, shapedDistance);
          greenWeight[idx] = std::max(greenWeight[idx], weight);
          const float targetWeight = std::max(weight, 0.001f);
          greenTargetSum[idx] += targetHeight * targetWeight;
          greenTargetWeight[idx] += targetWeight;
          if (shapedDistance < greenMaterialScale) {
            data.materialMap[idx] = 3;
            continue;
          }
          // グリーンの周りの細いカラー。ラフの上だけ短く刈り、ほかの地表は残す。
          const float collarWidth = 0.6f / radius;
          if (shapedDistance < greenMaterialScale + collarWidth &&
              data.materialMap[idx] == 1) {
            data.materialMap[idx] = 0;
          }
        }

        if (data.materialMap[idx] == 3) {
          continue;
        }

        for (const auto &bunker : bunkers) {
          const float bx = dx - bunker.centerX;
          const float bz = dz - bunker.centerZ;
          const float localX = bunker.cosAngle * bx + bunker.sinAngle * bz;
          const float localZ = -bunker.sinAngle * bx + bunker.cosAngle * bz;
          float patchDistance =
              std::sqrt((localX * localX) /
                            (bunker.radiusX * bunker.radiusX) +
                        (localZ * localZ) /
                            (bunker.radiusZ * bunker.radiusZ));
          patchDistance -= boundaryNoise * 0.8f;
          if (patchDistance < 1.25f) {
            // 砂の縁は少し外側までなだらかに下げ、深さは重ねず最大値を使う。
            const float bowl = SmoothStep(1.25f, 0.0f, patchDistance);
            depression[idx] = std::max(depression[idx], 0.28f * bowl);
            if (patchDistance < 1.0f) {
              data.materialMap[idx] = 2;
            }
            break;
          }
        }
      }
    }
  }

  for (size_t idx = 0; idx < cellCount; ++idx) {
    float height = original[idx];
    if (greenTargetWeight[idx] > 0.0f) {
      const float target = greenTargetSum[idx] / greenTargetWeight[idx];
      height = Lerp(height, target, greenWeight[idx]);
    }
    if (data.materialMap[idx] != 3) {
      height -= depression[idx] * (1.0f - greenWeight[idx]);
    }
    data.heightMap[idx] = height;
  }
}

void TerrainGenerator::FlattenCupSites(
    TerrainData &data, const std::vector<DirectX::XMFLOAT2> &holePositions) {
  const int resX = data.config.resolutionX;
  const int resZ = data.config.resolutionZ;
  const float worldW = data.config.worldWidth;
  const float worldD = data.config.worldDepth;
  if (resX < 2 || resZ < 2 || worldW <= 0.0f || worldD <= 0.0f ||
      data.heightMap.size() < static_cast<size_t>(resX * resZ)) {
    return;
  }

  const std::vector<float> original = data.heightMap;
  std::vector<float> weights(original.size(), 0.0f);
  std::vector<float> blendTargetSum(original.size(), 0.0f);
  std::vector<float> blendTargetWeight(original.size(), 0.0f);
  const float cellX = worldW / static_cast<float>(resX - 1);
  const float cellZ = worldD / static_cast<float>(resZ - 1);
  // 開口部に触れる格子セルの頂点がすべて同じ高さになる範囲を平坦にし、
  // その外側で元の地形へ滑らかにつなぐ。縦長フィールドでは軸ごとに
  // マス間隔が違うので、平坦範囲も軸ごとの楕円にする。
  const float coreRadiusX = game::physics::kGolfCupRadius + cellX * 1.5f;
  const float coreRadiusZ = game::physics::kGolfCupRadius + cellZ * 1.5f;
  // 斜面上のカップでも段差にならないよう、なじませ帯は最低 3m 確保する。
  const float blendRadiusX = coreRadiusX + std::max(cellX * 2.0f, 3.0f);
  const float blendRadiusZ = coreRadiusZ + std::max(cellZ * 2.0f, 3.0f);
  const float blendScale = std::max(blendRadiusX / coreRadiusX,
                                    blendRadiusZ / coreRadiusZ);

  auto sampleOriginal = [&](float x, float z) {
    const float fx = std::clamp((x / worldW + 0.5f) * (resX - 1), 0.0f,
                                static_cast<float>(resX - 1));
    const float fz = std::clamp((0.5f - z / worldD) * (resZ - 1), 0.0f,
                                static_cast<float>(resZ - 1));
    const int ix = std::clamp(static_cast<int>(fx), 0, resX - 2);
    const int iz = std::clamp(static_cast<int>(fz), 0, resZ - 2);
    const float tx = fx - ix;
    const float tz = fz - iz;
    const float h00 = original[iz * resX + ix];
    const float h10 = original[iz * resX + ix + 1];
    const float h01 = original[(iz + 1) * resX + ix];
    const float h11 = original[(iz + 1) * resX + ix + 1];
    return Lerp(Lerp(h00, h10, tx), Lerp(h01, h11, tx), tz);
  };

  for (const auto &pos : holePositions) {
    const float target = sampleOriginal(pos.x, pos.y);
    const int minX = std::max(
        0, static_cast<int>(std::floor((pos.x - blendRadiusX) / worldW * (resX - 1) +
                                       (resX - 1) * 0.5f)));
    const int maxX = std::min(
        resX - 1,
        static_cast<int>(std::ceil((pos.x + blendRadiusX) / worldW * (resX - 1) +
                                   (resX - 1) * 0.5f)));
    const int minZ = std::max(
        0, static_cast<int>(std::floor((0.5f - (pos.y + blendRadiusZ) / worldD) *
                                       (resZ - 1))));
    const int maxZ = std::min(
        resZ - 1,
        static_cast<int>(std::ceil((0.5f - (pos.y - blendRadiusZ) / worldD) *
                                   (resZ - 1))));

    for (int z = minZ; z <= maxZ; ++z) {
      const float worldZ =
          (0.5f - static_cast<float>(z) / static_cast<float>(resZ - 1)) * worldD;
      for (int x = minX; x <= maxX; ++x) {
        const float worldX =
            (static_cast<float>(x) / static_cast<float>(resX - 1) - 0.5f) *
            worldW;
        const float nx = (worldX - pos.x) / coreRadiusX;
        const float nz = (worldZ - pos.y) / coreRadiusZ;
        const float distance = std::sqrt(nx * nx + nz * nz);
        if (distance >= blendScale) {
          continue;
        }
        const float weight =
            distance <= 1.0f ? 1.0f : SmoothStep(blendScale, 1.0f, distance);
        const int idx = z * resX + x;
        if (weight < 1.0f) {
          // なじませ帯は重なったカップの目標高さを平均し、段差を作らない。
          blendTargetSum[idx] += target * weight;
          blendTargetWeight[idx] += weight;
        }
        if (weight <= weights[idx]) {
          continue;
        }
        weights[idx] = weight;
        if (weight >= 1.0f) {
          // 開口部に触れる頂点は、最初のカップの高さで完全に平らにする。
          data.heightMap[idx] = target;
          blendTargetSum[idx] = target;
          blendTargetWeight[idx] = 1.0f;
        }
      }
    }
  }

  for (size_t idx = 0; idx < original.size(); ++idx) {
    if (weights[idx] <= 0.0f || weights[idx] >= 1.0f ||
        blendTargetWeight[idx] <= 0.0f) {
      continue;
    }
    const float target = blendTargetSum[idx] / blendTargetWeight[idx];
    data.heightMap[idx] = Lerp(original[idx], target, weights[idx]);
  }
}

/**
 * @brief ハイト�（チ（�（にスムージング処理��適用します、（
*/

} // namespace game::systems
