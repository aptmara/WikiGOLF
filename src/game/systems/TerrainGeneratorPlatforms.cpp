#include "TerrainGeneratorInternals.h"
#include "TerrainGenerator.h"
#include "../../core/Logger.h"
#include "../../graphics/TangentGenerator.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace game::systems {

using namespace DirectX;

void TerrainGenerator::CreatePlatforms(
    TerrainData &data, const std::vector<DirectX::XMFLOAT2> &holePositions) {
  const int resX = data.config.resolutionX;
  const int resZ = data.config.resolutionZ;
  const float worldW = data.config.worldWidth;
  const float worldD = data.config.worldDepth;

  struct BunkerPatch {
    float centerX;
    float centerZ;
    float radiusX;
    float radiusZ;
    float cosAngle;
    float sinAngle;
  };

  for (const auto &pos : holePositions) {
    float u = pos.x / worldW + 0.5f;
    float v = 0.5f - pos.y / worldD;
    int cx = std::clamp(static_cast<int>(u * (resX - 1)), 0, resX - 1);
    int cz = std::clamp(static_cast<int>(v * (resZ - 1)), 0, resZ - 1);

    std::mt19937 tempRng(cx * 73856093u ^ cz * 19349663u ^
                         data.config.biome * 83492791u);
    std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
    const uint32_t patchSeed = tempRng();

    const int radius =
        std::max(4, resX / 12 + static_cast<int>(dist01(tempRng) * 4.0f));
    const float targetHeight = GetHeight(data, cx, cz) + 0.05f;
    const float bowlDepth = 0.2f;
    const float greenRadiusX = radius * (0.88f + dist01(tempRng) * 0.22f);
    const float greenRadiusZ = radius * (0.78f + dist01(tempRng) * 0.28f);
    const float greenAngle = (dist01(tempRng) - 0.5f) * 0.8f;
    const float greenCos = std::cos(greenAngle);
    const float greenSin = std::sin(greenAngle);

    int bunkerCount = 1 + static_cast<int>(dist01(tempRng) * 2.0f);
    if (data.config.biome == 2) {
      bunkerCount = std::max(1, bunkerCount - 1);
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
      patch.radiusX = radius * (0.32f + dist01(tempRng) * 0.24f);
      patch.radiusZ = radius * (0.20f + dist01(tempRng) * 0.18f);
      patch.cosAngle = std::cos(patchAngle);
      patch.sinAngle = std::sin(patchAngle);
      bunkers.push_back(patch);
    }

    for (int z = cz - radius * 3; z <= cz + radius * 3; ++z) {
      for (int x = cx - radius * 3; x <= cx + radius * 3; ++x) {
        if (x < 0 || x >= resX || z < 0 || z >= resZ) {
          continue;
        }

        const float dx = static_cast<float>(x - cx);
        const float dz = static_cast<float>(z - cz);
        const float cupDistance = std::sqrt(dx * dx + dz * dz);
        const int idx = z * resX + x;
        const float greenLocalX = greenCos * dx + greenSin * dz;
        const float greenLocalZ = -greenSin * dx + greenCos * dz;
        const float greenDistance =
            std::sqrt((greenLocalX * greenLocalX) /
                          (greenRadiusX * greenRadiusX) +
                      (greenLocalZ * greenLocalZ) /
                          (greenRadiusZ * greenRadiusZ));
        const float boundaryNoise =
            FractalNoise(static_cast<float>(x) * 0.16f,
                         static_cast<float>(z) * 0.16f, patchSeed) *
            0.07f;

        if (greenDistance < 1.0f + boundaryNoise) {
          data.materialMap[idx] = 3;
          if (cupDistance < 3.0f) {
            SetHeight(data, x, z, targetHeight - bowlDepth);
          } else {
            SetHeight(data, x, z, targetHeight);
          }
          continue;
        }

        if (greenDistance < 1.18f + boundaryNoise) {
          if (data.materialMap[idx] == 0 || data.materialMap[idx] == 1) {
            data.materialMap[idx] = 0;
          }
          float t = SmoothStep(1.0f, 1.18f, greenDistance - boundaryNoise);
          const float currentH = GetHeight(data, x, z);
          SetHeight(data, x, z, Lerp(targetHeight, currentH, t));
          continue;
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
          if (patchDistance < 1.0f) {
            data.materialMap[idx] = 2;
            float depression = SmoothStep(1.0f, 0.0f, patchDistance);
            SetHeight(data, x, z,
                      GetHeight(data, x, z) - 0.18f * depression);
            break;
          }
        }
      }
    }
  }
}

/**
 * @brief ハイト�（チ（�（にスムージング処理��適用します、（
 */

} // namespace game::systems
