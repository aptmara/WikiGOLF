#include "src/game/systems/SkyGlobeLayout.h"
#include "src/game/systems/TerrainBackdrop.h"
#include "src/game/systems/TerrainGenerator.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>

#define CHECK(condition, message)                                              \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << "[FAIL] " << message << "\n";                              \
      std::exit(1);                                                            \
    }                                                                          \
    std::cout << "[PASS] " << message << "\n";                                \
  } while (0)

namespace {

constexpr uint8_t kWater = 5;
constexpr uint8_t kLava = 6;

constexpr uint8_t kFairway = 0;
constexpr uint8_t kRough = 1;
constexpr uint8_t kBunker = 2;
constexpr uint8_t kGreen = 3;

int ToGridX(const game::systems::TerrainData &data, float worldX) {
  float u = worldX / data.config.worldWidth + 0.5f;
  return std::clamp(static_cast<int>(u * (data.config.resolutionX - 1)), 0,
                    data.config.resolutionX - 1);
}

int ToGridZ(const game::systems::TerrainData &data, float worldZ) {
  float v = 0.5f - worldZ / data.config.worldDepth;
  return std::clamp(static_cast<int>(v * (data.config.resolutionZ - 1)), 0,
                    data.config.resolutionZ - 1);
}

uint8_t MaterialAt(const game::systems::TerrainData &data, float worldX,
                   float worldZ) {
  const int gx = ToGridX(data, worldX);
  const int gz = ToGridZ(data, worldZ);
  return data.materialMap[gz * data.config.resolutionX + gx];
}

int CountIsolatedHazardCells(const game::systems::TerrainData &data) {
  const int resX = data.config.resolutionX;
  const int resZ = data.config.resolutionZ;
  int isolated = 0;
  for (int z = 1; z < resZ - 1; ++z) {
    for (int x = 1; x < resX - 1; ++x) {
      uint8_t material = data.materialMap[z * resX + x];
      if (material < kBunker || material == kGreen) {
        continue;
      }

      int sameNeighbors = 0;
      for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
          if (dx == 0 && dz == 0) {
            continue;
          }
          if (data.materialMap[(z + dz) * resX + (x + dx)] == material) {
            ++sameNeighbors;
          }
        }
      }
      if (sameNeighbors == 0) {
        ++isolated;
      }
    }
  }
  return isolated;
}

float MaxAdjacentHeightDelta(const game::systems::TerrainData &data) {
  const int resX = data.config.resolutionX;
  const int resZ = data.config.resolutionZ;
  float maxDelta = 0.0f;
  for (int z = 0; z < resZ; ++z) {
    for (int x = 0; x < resX; ++x) {
      const float height = data.heightMap[z * resX + x];
      if (x + 1 < resX) {
        maxDelta = std::max(
            maxDelta,
            std::abs(height - data.heightMap[z * resX + x + 1]));
      }
      if (z + 1 < resZ) {
        maxDelta = std::max(
            maxDelta,
            std::abs(height - data.heightMap[(z + 1) * resX + x]));
      }
    }
  }
  return maxDelta;
}

struct SlopeStats {
  float maxDegrees = 0.0f;
  float steepRatio = 0.0f; // 40度を超える隣接辺の割合（山腹は 30 度前後まで許す）
};

SlopeStats MeasureSlopes(const game::systems::TerrainData &data) {
  const int resX = data.config.resolutionX;
  const int resZ = data.config.resolutionZ;
  const float cellX = data.config.worldWidth / static_cast<float>(resX - 1);
  const float cellZ = data.config.worldDepth / static_cast<float>(resZ - 1);
  const float steepTangent = std::tan(40.0f * 3.14159265f / 180.0f);
  float maxTangent = 0.0f;
  int steepEdges = 0;
  int edges = 0;
  auto accumulate = [&](float delta, float spacing) {
    const float tangent = std::abs(delta) / spacing;
    maxTangent = std::max(maxTangent, tangent);
    if (tangent > steepTangent) {
      ++steepEdges;
    }
    ++edges;
  };
  for (int z = 0; z < resZ; ++z) {
    for (int x = 0; x < resX; ++x) {
      const float height = data.heightMap[z * resX + x];
      if (x + 1 < resX) {
        accumulate(height - data.heightMap[z * resX + x + 1], cellX);
      }
      if (z + 1 < resZ) {
        accumulate(height - data.heightMap[(z + 1) * resX + x], cellZ);
      }
    }
  }
  SlopeStats stats;
  stats.maxDegrees = std::atan(maxTangent) * 180.0f / 3.14159265f;
  stats.steepRatio =
      edges > 0 ? static_cast<float>(steepEdges) / static_cast<float>(edges)
                : 0.0f;
  return stats;
}

struct HoleFlatness {
  float averageDegrees = 0.0f;
  float p95Degrees = 0.0f;
};

// 各ホール中心から半径 2.5m 以内の地面の最大傾斜を集計する。
HoleFlatness MeasureHoleFlatness(const game::systems::TerrainData &data,
                                 const std::vector<DirectX::XMFLOAT2> &holes) {
  const int resX = data.config.resolutionX;
  const int resZ = data.config.resolutionZ;
  const float cellX = data.config.worldWidth / static_cast<float>(resX - 1);
  const float cellZ = data.config.worldDepth / static_cast<float>(resZ - 1);
  std::vector<float> slopes;
  slopes.reserve(holes.size());
  for (const auto &hole : holes) {
    const int cx = ToGridX(data, hole.x);
    const int cz = ToGridZ(data, hole.y);
    const int rx = static_cast<int>(std::ceil(2.5f / cellX));
    const int rz = static_cast<int>(std::ceil(2.5f / cellZ));
    float maxTangent = 0.0f;
    for (int z = std::max(0, cz - rz); z < std::min(resZ - 1, cz + rz); ++z) {
      for (int x = std::max(0, cx - rx); x < std::min(resX - 1, cx + rx); ++x) {
        const float h = data.heightMap[z * resX + x];
        maxTangent = std::max(
            maxTangent, std::abs(h - data.heightMap[z * resX + x + 1]) / cellX);
        maxTangent = std::max(
            maxTangent,
            std::abs(h - data.heightMap[(z + 1) * resX + x]) / cellZ);
      }
    }
    slopes.push_back(std::atan(maxTangent) * 180.0f / 3.14159265f);
  }
  HoleFlatness result;
  if (slopes.empty()) {
    return result;
  }
  float sum = 0.0f;
  for (float slope : slopes) {
    sum += slope;
  }
  result.averageDegrees = sum / static_cast<float>(slopes.size());
  std::sort(slopes.begin(), slopes.end());
  result.p95Degrees = slopes[slopes.size() * 95 / 100];
  return result;
}

// コース中央線上の高さの幅（遠くから見える起伏の大きさ）
float CenterProfileRange(const game::systems::TerrainData &data) {
  const int resX = data.config.resolutionX;
  const int resZ = data.config.resolutionZ;
  float minimum = 1.0e9f;
  float maximum = -1.0e9f;
  for (int z = 0; z < resZ; ++z) {
    const float h = data.heightMap[z * resX + resX / 2];
    minimum = std::min(minimum, h);
    maximum = std::max(maximum, h);
  }
  return maximum - minimum;
}

float HeightRange(const game::systems::TerrainData &data) {
  const auto [minimum, maximum] =
      std::minmax_element(data.heightMap.begin(), data.heightMap.end());
  return *maximum - *minimum;
}

std::array<int, 8>
CountTerrainMaterials(const game::systems::TerrainData &data) {
  std::array<int, 8> counts{};
  for (const uint8_t material : data.materialMap) {
    if (material < counts.size()) {
      ++counts[material];
    }
  }
  return counts;
}

float VisualColorDistance(const DirectX::XMFLOAT3 &a,
                          const DirectX::XMFLOAT3 &b) {
  float dx = a.x - b.x;
  float dy = a.y - b.y;
  float dz = a.z - b.z;
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

float MaxAdjacentVisualColorDelta(const game::systems::TerrainData &data) {
  const int resX = data.config.resolutionX;
  const int resZ = data.config.resolutionZ;
  float maxDelta = 0.0f;
  for (int z = 0; z < resZ; ++z) {
    for (int x = 0; x < resX; ++x) {
      const auto &color = data.visualMaterialColors[z * resX + x];
      if (x + 1 < resX) {
        maxDelta = std::max(
            maxDelta,
            VisualColorDistance(color,
                                data.visualMaterialColors[z * resX + x + 1]));
      }
      if (z + 1 < resZ) {
        maxDelta = std::max(
            maxDelta,
            VisualColorDistance(
                color, data.visualMaterialColors[(z + 1) * resX + x]));
      }
    }
  }
  return maxDelta;
}

} // namespace

int main() {
  game::systems::TerrainConfig config;
  config.resolutionX = 64;
  config.resolutionZ = 96;
  config.worldWidth = 80.0f;
  config.worldDepth = 120.0f;
  config.heightScale = 1.7f;
  config.biome = 0;

  std::vector<DirectX::XMFLOAT2> holes = {
      {-18.0f, 34.0f}, {14.0f, 5.0f}, {-8.0f, -32.0f}};

  auto data =
      game::systems::TerrainGenerator::GenerateTerrain("Course variety seed",
                                                       holes, config);
  CHECK(data.materialMap.size() ==
            static_cast<size_t>(config.resolutionX * config.resolutionZ),
        "Material map has one entry per terrain vertex");
  CHECK(!data.vertices.empty() && !data.indices.empty(),
        "Terrain mesh is generated");
  CHECK(data.vertices.size() == data.materialMap.size(),
        "Terrain mesh keeps one material value per vertex");
  CHECK(data.visualMaterialColors.size() == data.materialMap.size(),
        "Terrain keeps one visual material color per physics material");
  CHECK(MaxAdjacentVisualColorDelta(data) < 0.35f,
        "Visual material colors transition smoothly between adjacent cells");

  bool vertexMaterialsMatch = true;
  for (size_t i = 0; i < data.vertices.size(); ++i) {
    int encodedMaterial =
        static_cast<int>(std::floor(data.vertices[i].color.w * 255.0f));
    if (encodedMaterial != data.materialMap[i]) {
      vertexMaterialsMatch = false;
      break;
    }
  }
  CHECK(vertexMaterialsMatch,
        "Terrain vertex alpha consistently encodes the material layer");

  auto repeated =
      game::systems::TerrainGenerator::GenerateTerrain("Course variety seed",
                                                       holes, config);
  bool repeatedVisualColorsMatch =
      repeated.visualMaterialColors.size() == data.visualMaterialColors.size();
  if (repeatedVisualColorsMatch) {
    for (size_t i = 0; i < data.visualMaterialColors.size(); ++i) {
      const auto &a = data.visualMaterialColors[i];
      const auto &b = repeated.visualMaterialColors[i];
      if (a.x != b.x || a.y != b.y || a.z != b.z) {
        repeatedVisualColorsMatch = false;
        break;
      }
    }
  }
  CHECK(repeated.materialMap == data.materialMap &&
            repeated.heightMap == data.heightMap && repeatedVisualColorsMatch,
        "Terrain generation is deterministic for the same article and config");
  CHECK(CountIsolatedHazardCells(data) == 0,
        "Generated terrain contains no one-cell hazard speckles");
  CHECK(MaxAdjacentHeightDelta(data) < config.heightScale,
        "Generated terrain contains no abrupt one-cell height steps");

  std::array<int, 8> counts{};
  for (uint8_t mat : data.materialMap) {
    if (mat < counts.size()) {
      ++counts[mat];
    }
  }

  CHECK(counts[0] > 0, "Fairway is present");
  CHECK(counts[1] > 0, "Rough is present");
  CHECK(counts[2] > 0, "Bunker is present");
  CHECK(counts[3] > 0, "Green is present");

  for (const auto &hole : holes) {
    int gx = ToGridX(data, hole.x);
    int gz = ToGridZ(data, hole.y);
    uint8_t mat = data.materialMap[gz * config.resolutionX + gx];
    CHECK(mat == 3, "Hole center is converted to green");
  }

  int variedSeedsWithHazards = 0;
  for (int biome = 0; biome < 4; ++biome) {
    config.biome = biome;
    auto themed = game::systems::TerrainGenerator::GenerateTerrain(
        "Course variety biome " + std::to_string(biome), holes, config);
    std::array<int, 8> themedCounts{};
    for (uint8_t mat : themed.materialMap) {
      if (mat < themedCounts.size()) {
        ++themedCounts[mat];
      }
    }
    int unique = 0;
    for (int count : themedCounts) {
      if (count > 0) {
        ++unique;
      }
    }
    if (themedCounts[2] + themedCounts[4] + themedCounts[5] +
            themedCounts[6] + themedCounts[7] >
        0) {
      ++variedSeedsWithHazards;
    }
    int obCount = themedCounts[kWater] + themedCounts[kLava];
    if (biome == 0 || biome == 1) {
      CHECK(obCount == 0,
            "Safe or dry biomes do not generate water/lava OB hazards");
    } else {
      CHECK(obCount > 0,
            "Ice and rocky biomes generate natural OB hazard terrain");
    }
    if (biome == 1) {
      CHECK(themedCounts[kBunker] + themedCounts[7] > themedCounts[kRough],
            "Desert terrain is dominated by sand and stone outside the course");
    }
    CHECK(CountIsolatedHazardCells(themed) == 0,
          "Biome terrain contains no one-cell hazard speckles");
    CHECK(unique >= 4, "Biome course keeps at least four terrain types");
  }

  CHECK(variedSeedsWithHazards >= 3,
        "Most biome variants include hazard or gimmick terrain");

  game::systems::TerrainConfig htmlConfig = config;
  htmlConfig.biome = 3;
  htmlConfig.htmlCourse = true;
  htmlConfig.htmlRegions = {
      {0.12f, 0.18f, 0.32f, 0.10f,
       game::systems::HtmlRegionKind::Heading},
      {0.58f, 0.38f, 0.24f, 0.18f,
       game::systems::HtmlRegionKind::Body},
      {0.20f, 0.62f, 0.28f, 0.16f,
       game::systems::HtmlRegionKind::Hazard}};

  auto htmlData = game::systems::TerrainGenerator::GenerateTerrain(
      "HTML course variety seed", holes, htmlConfig);
  const auto htmlCounts = CountTerrainMaterials(htmlData);
  int htmlMaterialTypes = 0;
  for (const int count : htmlCounts) {
    if (count > 0) {
      ++htmlMaterialTypes;
    }
  }

  CHECK(HeightRange(htmlData) > htmlConfig.heightScale,
        "HTML course preserves substantial terrain elevation changes");
  CHECK(htmlMaterialTypes >= 4,
        "HTML course keeps at least four terrain types");
  CHECK(htmlCounts[kLava] + htmlCounts[7] > 0,
        "HTML course preserves biome-specific hazards");
  CHECK(CountIsolatedHazardCells(htmlData) == 0,
        "HTML course contains no one-cell hazard speckles");
  CHECK(MaxAdjacentHeightDelta(htmlData) < htmlConfig.heightScale,
        "HTML course contains no abrupt one-cell height steps");
  for (const auto &hole : holes) {
    CHECK(MaterialAt(htmlData, hole.x, hole.y) == kGreen,
          "HTML link hole center is converted to green");
  }

  game::systems::TerrainConfig baseConfig = htmlConfig;
  baseConfig.htmlCourse = false;
  baseConfig.htmlRegions.clear();
  auto baseData = game::systems::TerrainGenerator::GenerateTerrain(
      "HTML course variety seed", holes, baseConfig);
  CHECK(htmlData.heightMap != baseData.heightMap,
        "HTML regions add article-specific relief to the terrain");

  // 「平成」相当の長い記事: 縦長フィールドに段落・見出し・画像の並びでリンクが密集する。
  {
    game::systems::TerrainConfig longConfig;
    longConfig.worldWidth = 80.0f;
    longConfig.worldDepth = 2162.0f;
    longConfig.resolutionX = 101;
    longConfig.resolutionZ = 2048; // TerrainLayoutRules::CalculateResolution 相当
    longConfig.htmlCourse = true;
    longConfig.generateExtension = true;

    uint32_t state = 12345u;
    auto next01 = [&state]() {
      state = state * 1664525u + 1013904223u;
      return static_cast<float>(state >> 8) / static_cast<float>(1u << 24);
    };
    std::vector<DirectX::XMFLOAT2> articleHoles;
    float cursor = 20.0f; // 記事先頭からの距離
    while (cursor < 2130.0f) {
      // 見出し
      longConfig.htmlRegions.push_back(
          {0.05f, cursor / longConfig.worldDepth, 0.5f,
           3.0f / longConfig.worldDepth,
           game::systems::HtmlRegionKind::Heading});
      cursor += 8.0f;
      const float sectionEnd = cursor + 60.0f + next01() * 120.0f;
      while (cursor < sectionEnd && cursor < 2130.0f) {
        const bool image = next01() < 0.15f;
        const float paragraph = 10.0f + next01() * 25.0f;
        for (float line = 0.0f; line < paragraph; line += 1.2f) {
          const int linksOnLine = static_cast<int>(next01() * 3.0f);
          for (int k = 0; k < linksOnLine; ++k) {
            float x = (next01() - 0.5f) * 72.0f;
            if (image) {
              x = -36.0f + next01() * 36.0f; // 右半分は画像
            }
            articleHoles.push_back(
                {x, longConfig.worldDepth * 0.5f - (cursor + line)});
          }
        }
        cursor += paragraph + 3.0f + next01() * 5.0f;
      }
      cursor += 6.0f;
    }

    float worstSteepRatio = 0.0f;
    float worstSlope = 0.0f;
    for (int biome = 0; biome < 4; ++biome) {
      longConfig.biome = biome;
      longConfig.heightScale = biome == 1 ? 2.5f : biome == 2 ? 1.0f
                               : biome == 3 ? 3.0f : 1.5f;
      auto longData = game::systems::TerrainGenerator::GenerateTerrain(
          "平成", articleHoles, longConfig);
      const SlopeStats slopes = MeasureSlopes(longData);
      const HoleFlatness flatness = MeasureHoleFlatness(longData, articleHoles);
      const float profile = CenterProfileRange(longData);
      std::cout << "  long article biome=" << biome
                << " holes=" << articleHoles.size()
                << " maxSlope=" << slopes.maxDegrees
                << "deg steep=" << slopes.steepRatio * 100.0f
                << "% range=" << HeightRange(longData)
                << "m profile=" << profile
                << "m holeSlopeAvg=" << flatness.averageDegrees
                << "deg holeSlopeP95=" << flatness.p95Degrees << "deg" << std::endl;
      worstSteepRatio = std::max(worstSteepRatio, slopes.steepRatio);
      worstSlope = std::max(worstSlope, slopes.maxDegrees);
      CHECK(flatness.p95Degrees < 6.0f,
            "Link holes sit on flat ground rather than slopes");
      CHECK(profile > (biome == 2 ? 12.0f : 18.0f),
            "Long article course shows large-scale elevation variety");

      // 地表の模様: グリーンがコースを覆い尽くさず、バイオームの地表が見えている
      const auto surface = CountTerrainMaterials(longData);
      const float cells = static_cast<float>(longData.materialMap.size());
      const float greenShare = surface[kGreen] / cells;
      float biomeShare = 0.0f;
      switch (biome) {
      case 1: biomeShare = (surface[kBunker] + surface[7]) / cells; break;
      case 2: biomeShare = (surface[4] + surface[kWater]) / cells; break;
      case 3: biomeShare = (surface[7] + surface[kLava]) / cells; break;
      default: biomeShare = (surface[kRough] + surface[kBunker]) / cells; break;
      }
      // 別の記事（同じホール配置）では模様が変わる
      auto otherArticle = game::systems::TerrainGenerator::GenerateTerrain(
          "神奈川県", articleHoles, longConfig);
      size_t differing = 0;
      for (size_t i = 0; i < longData.materialMap.size(); ++i) {
        differing += longData.materialMap[i] != otherArticle.materialMap[i] ? 1 : 0;
      }
      const float differingShare = differing / cells;
      std::cout << "  surface biome=" << biome << " green=" << greenShare * 100.0f
                << "% biomeSurface=" << biomeShare * 100.0f
                << "% otherArticleDiff=" << differingShare * 100.0f << "%" << std::endl;
      CHECK(greenShare < 0.2f, "Greens stay around cups instead of covering the course");
      int obNearCup = 0;
      for (const auto &hole : articleHoles) {
        for (float ox = -3.0f; ox <= 3.0f; ox += 1.0f) {
          for (float oz = -3.0f; oz <= 3.0f; oz += 1.0f) {
            if (ox * ox + oz * oz > 9.0f) continue;
            const uint8_t m = MaterialAt(longData, hole.x + ox, hole.y + oz);
            obNearCup += (m == kWater || m == kLava) ? 1 : 0;
          }
        }
      }
      CHECK(obNearCup == 0, "No water or lava OB right next to a cup");
      CHECK(biomeShare > (biome == 2 ? 0.05f : 0.2f),
            "Biome surface remains visible on a dense link course");
      CHECK(differingShare > 0.3f, "Different articles get different surface patterns");

      // コース外の山並み
      const uint32_t backdropSeed = 0x5eedu;
      float seamError = 0.0f;
      float highestPeak = -1.0e9f;
      for (float z = -1000.0f; z <= 1000.0f; z += 37.0f) {
        const int gz = ToGridZ(longData, z);
        const float leftEdge = longData.heightMap[gz * longConfig.resolutionX];
        const float rightEdge =
            longData.heightMap[gz * longConfig.resolutionX + longConfig.resolutionX - 1];
        seamError = std::max(
            seamError,
            std::abs(game::systems::SampleTerrainBackdrop(
                         longData, -longConfig.worldWidth * 0.5f,
                         longData.config.worldDepth * 0.5f -
                             static_cast<float>(gz) /
                                 (longConfig.resolutionZ - 1) *
                                 longData.config.worldDepth,
                         backdropSeed)
                         .height -
                     leftEdge));
        seamError = std::max(
            seamError,
            std::abs(game::systems::SampleTerrainBackdrop(
                         longData, longConfig.worldWidth * 0.5f,
                         longData.config.worldDepth * 0.5f -
                             static_cast<float>(gz) /
                                 (longConfig.resolutionZ - 1) *
                                 longData.config.worldDepth,
                         backdropSeed)
                         .height -
                     rightEdge));
        for (float d = 20.0f; d <= 150.0f; d += 10.0f) {
          highestPeak = std::max(
              highestPeak, game::systems::SampleTerrainBackdrop(
                               longData, longConfig.worldWidth * 0.5f + d, z,
                               backdropSeed)
                                   .height -
                               rightEdge);
        }
      }
      std::cout << "  backdrop biome=" << biome << " seam=" << seamError
                << "m peak=" << highestPeak << "m" << std::endl;
      CHECK(seamError < 0.05f,
            "Backdrop mountains meet the course edge without a gap");
      CHECK(highestPeak > 25.0f,
            "Backdrop mountains rise well above the course");
      const auto backdrop =
          game::systems::BuildTerrainBackdrop(longData, backdropSeed);
      bool normalsUp = !backdrop.empty();
      for (const auto &chunk : backdrop) {
        normalsUp = normalsUp && !chunk.vertices.empty() && !chunk.indices.empty();
        for (const auto &vertex : chunk.vertices) {
          normalsUp = normalsUp && vertex.normal.y > 0.0f;
        }
      }
      CHECK(normalsUp, "Backdrop mesh is generated with upward normals");

      // 空に浮かぶ地球儀
      if (biome == 0) {
        auto ground = [&](float x, float z) {
          return game::systems::SampleTerrainBackdrop(longData, x, z, backdropSeed).height;
        };
        const auto globes = game::systems::BuildSkyGlobeLayout(
            longConfig.worldWidth, longConfig.worldDepth, 0x610bu, ground);
        bool aboveGround = true;
        bool spread = true;
        int overCourse = 0;
        for (size_t i = 0; i < globes.size(); ++i) {
          const auto &g = globes[i];
          const float clearance = g.position.y - g.bobHeight - g.scale * 0.57f -
                                  ground(g.position.x, g.position.z);
          aboveGround = aboveGround && clearance > 15.0f;
          overCourse += std::abs(g.position.x) <= longConfig.worldWidth * 0.5f ? 1 : 0;
          for (size_t j = i + 1; j < globes.size(); ++j) {
            const float dx = g.position.x - globes[j].position.x;
            const float dz = g.position.z - globes[j].position.z;
            spread = spread && (dx * dx + dz * dz > 4.0f);
          }
        }
        std::cout << "  sky globes=" << globes.size() << " overCourse=" << overCourse << std::endl;
        CHECK(globes.size() >= 8 && globes.size() <= 20,
              "A moderate number of globes float in the sky");
        CHECK(aboveGround, "Sky globes float well above the terrain");
        CHECK(spread, "Sky globes do not overlap each other");
        CHECK(overCourse > 0 && overCourse < static_cast<int>(globes.size()),
              "Sky globes are spread over the course and the surrounding terrain");
        const auto shortCourseGlobes = game::systems::BuildSkyGlobeLayout(80.0f, 120.0f, 1u, ground);
        CHECK(shortCourseGlobes.size() == 8, "Short courses still get a handful of globes");

        // LOD: 近い大きな地球儀ほど細かいモデル、遠いほど軽いモデル
        using game::systems::SelectSkyGlobeLod;
        CHECK(SelectSkyGlobeLod(40.0f, 10.0f, 2) == 0, "Nearby globes use the detailed LOD");
        CHECK(SelectSkyGlobeLod(250.0f, 10.0f, 0) == 1, "Mid-distance globes use the middle LOD");
        CHECK(SelectSkyGlobeLod(600.0f, 5.0f, 0) == 2, "Far globes use the lightest LOD");
        // 境目の近くでは今の LOD を保ち、切り替えがちらつかない
        const float edge = 25.0f * 10.0f * 0.57f;
        CHECK(SelectSkyGlobeLod(edge * 1.05f, 10.0f, 0) == 0 &&
                  SelectSkyGlobeLod(edge * 0.95f, 10.0f, 1) == 1,
              "LOD switching has hysteresis around the threshold");
        for (const char *path : game::systems::kSkyGlobeLodMeshes) {
          FILE *file = std::fopen(path, "rb");
          CHECK(file != nullptr, "Sky globe LOD model exists");
          if (file) std::fclose(file);
        }
      }

      // 手前・奥の縁と角もコースと隙間なくつながる
      const float halfW = longConfig.worldWidth * 0.5f;
      const float halfD = longConfig.worldDepth * 0.5f;
      float endSeam = 0.0f;
      for (float x = -halfW; x <= halfW; x += 0.8f) {
        for (float z : {-halfD, halfD}) {
          const float course =
              game::systems::SampleTerrainBackdrop(longData, x, z, backdropSeed)
                  .height;
          const float outside = game::systems::SampleTerrainBackdrop(
                                    longData, x, z + (z > 0 ? 0.001f : -0.001f),
                                    backdropSeed)
                                    .height;
          endSeam = std::max(endSeam, std::abs(course - outside));
        }
      }
      CHECK(endSeam < 0.02f, "Backdrop meets the front and back course edges");

      // 境界のすぐ外側でコースの地表と色が続き、バイオームの模様が途切れない
      int continued = 0;
      int compared = 0;
      float colorJump = 0.0f;
      for (float z = -halfD + 5.0f; z < halfD; z += 3.0f) {
        const int gx = longConfig.resolutionX - 1;
        const int gz = ToGridZ(longData, z);
        uint8_t inside = longData.materialMap[gz * longConfig.resolutionX + gx];
        if (inside == kGreen) inside = kFairway;
        const auto outside =
            game::systems::SampleTerrainBackdrop(longData, halfW + 1.0f, z, backdropSeed);
        continued += outside.material == inside ? 1 : 0;
        ++compared;
        // 縁のすぐ内外で色が連続する
        const auto edge =
            game::systems::SampleTerrainBackdrop(longData, halfW, z, backdropSeed);
        const auto justOutside = game::systems::SampleTerrainBackdrop(
            longData, halfW + 0.1f, z, backdropSeed);
        colorJump = std::max(colorJump,
                             VisualColorDistance(justOutside.color, edge.color));
      }
      std::cout << "  backdrop continuity=" << continued * 100 / compared
                << "% colorJump=" << colorJump << std::endl;
      CHECK(continued * 10 >= compared * 7,
            "Course surface continues just outside the boundary");
      CHECK(colorJump < 0.2f, "Terrain color does not jump at the boundary");

      // 外側はコースの鏡映（繰り返し）ではなく、同じ規則の続きになっている
      int mirrored = 0;
      int mirrorSamples = 0;
      for (float z = -halfD + 40.0f; z < halfD - 40.0f; z += 7.0f) {
        for (float d = 8.0f; d <= 30.0f; d += 4.0f) {
          const auto out = game::systems::SampleTerrainBackdrop(longData, halfW + d, z, backdropSeed);
          const int gz = ToGridZ(longData, z);
          const int gx = ToGridX(longData, halfW - d);
          uint8_t in = longData.materialMap[gz * longConfig.resolutionX + gx];
          if (in == kGreen) in = kFairway;
          mirrored += out.material == in ? 1 : 0;
          ++mirrorSamples;
        }
      }
      const float mirrorShare = static_cast<float>(mirrored) / mirrorSamples;
      std::cout << "  backdrop mirrorShare=" << mirrorShare * 100.0f << "%" << std::endl;
      CHECK(longData.extension != nullptr,
            "Extension terrain is generated alongside the course");
      CHECK(mirrorShare < 0.75f, "Outside terrain is a continuation, not a mirror copy");

      // 延長地形はコースの内側と同じ規則で作られている（コース内の同じ地点では章の段がそろう）
      {
        float bandError = 0.0f;
        int bandSamples = 0;
        for (float z = -halfD + 60.0f; z < halfD - 60.0f; z += 53.0f) {
          const auto &ext = *longData.extension;
          const float extW = ext.config.worldWidth;
          const float extD = ext.config.worldDepth;
          const int ex = static_cast<int>((0.0f / extW + 0.5f) * (ext.config.resolutionX - 1));
          const int ez = static_cast<int>((0.5f - z / extD) * (ext.config.resolutionZ - 1));
          const float extensionHeight = ext.heightMap[ez * ext.config.resolutionX + ex];
          // 延長地形は穴の平坦化を含まないので、コース中央線の広い範囲の平均と比べる
          float courseSum = 0.0f;
          int courseCount = 0;
          for (float ox = -30.0f; ox <= 30.0f; ox += 3.0f) {
            for (float oz = -6.0f; oz <= 6.0f; oz += 3.0f) {
              courseSum += longData.heightMap[ToGridZ(longData, z + oz) * longConfig.resolutionX +
                                              ToGridX(longData, ox)];
              ++courseCount;
            }
          }
          bandError += std::abs(extensionHeight - courseSum / courseCount);
          ++bandSamples;
        }
        std::cout << "  extension vs course avg height error=" << bandError / bandSamples << "m" << std::endl;
        CHECK(bandError / bandSamples < 3.0f,
              "Extension follows the same chapter heights as the course");
      }

      // 四方すべてに延長地形がある
      float minX = 1.0e9f, maxX = -1.0e9f, minZ = 1.0e9f, maxZ = -1.0e9f;
      for (const auto &chunk : backdrop) {
        for (const auto &vertex : chunk.vertices) {
          minX = std::min(minX, vertex.position.x);
          maxX = std::max(maxX, vertex.position.x);
          minZ = std::min(minZ, vertex.position.z);
          maxZ = std::max(maxZ, vertex.position.z);
        }
      }
      CHECK(minX < -halfW - 100.0f && maxX > halfW + 100.0f &&
                minZ < -halfD - 100.0f && maxZ > halfD + 100.0f,
            "Backdrop surrounds the course on all four sides");
    }
    CHECK(worstSteepRatio < 0.005f,
          "Long article terrain has almost no cliff-like slopes");
    CHECK(worstSlope < 50.0f, "Long article terrain has no wall-like steps");
  }

  game::systems::TerrainConfig tutorialConfig;
  tutorialConfig.resolutionX = 64;
  tutorialConfig.resolutionZ = 96;
  tutorialConfig.worldWidth = 96.0f;
  tutorialConfig.worldDepth = 144.0f;
  tutorialConfig.heightScale = 1.0f;
  tutorialConfig.biome = 0;

  std::vector<DirectX::XMFLOAT2> tutorialHoles = {
      {0.0f, -28.0f}, {-30.0f, -12.0f}, {24.0f, -3.0f},
      {-24.0f, 20.0f}, {12.0f, 42.0f}, {0.0f, 56.0f}};

  auto tutorialData =
      game::systems::TerrainGenerator::GenerateTutorialTerrain(
          tutorialConfig, tutorialHoles);

  CHECK(tutorialData.materialMap.size() ==
            static_cast<size_t>(tutorialConfig.resolutionX *
                                tutorialConfig.resolutionZ),
        "Tutorial material map has one entry per terrain vertex");
  CHECK(!tutorialData.vertices.empty() && !tutorialData.indices.empty(),
        "Tutorial terrain mesh is generated");
  CHECK(tutorialData.visualMaterialColors.size() ==
            tutorialData.materialMap.size(),
        "Tutorial terrain generates visual material colors");

  std::array<int, 8> tutorialCounts{};
  for (uint8_t mat : tutorialData.materialMap) {
    if (mat < tutorialCounts.size()) {
      ++tutorialCounts[mat];
    }
  }

  CHECK(tutorialCounts[kFairway] > 0, "Tutorial fairway is present");
  CHECK(tutorialCounts[kRough] > 0, "Tutorial rough is present");
  CHECK(tutorialCounts[kBunker] > 0, "Tutorial bunker is present");
  CHECK(tutorialCounts[kGreen] > 0, "Tutorial green is present");
  CHECK(tutorialCounts[kWater] > 0, "Tutorial water hazard is present");
  CHECK(MaterialAt(tutorialData, 0.0f, -28.0f) == kFairway,
        "Tutorial fairway lesson is on fairway");
  CHECK(MaterialAt(tutorialData, 24.0f, -3.0f) == kBunker,
        "Tutorial bunker lesson is in the bunker");
  CHECK(MaterialAt(tutorialData, -24.0f, 20.0f) == kWater,
        "Tutorial water lesson is in the water hazard");
  CHECK(MaterialAt(tutorialData, 0.0f, 56.0f) == kGreen,
        "Tutorial goal is on the green");

  std::cout << "All terrain generation tests passed!\n";
  return 0;
}
