/**
 * @file TerrainGeneratorLandforms.cpp
 * @brief 記事の章ごとに地形の個性（高さの段と地形テーマ）を与えます。
 */

#include "TerrainGeneratorInternals.h"
#include "TerrainGenerator.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace game::systems {
namespace {

using DirectX::XMFLOAT2;

// 地形マテリアル ID（TerrainMaterialAssets と同じ並び）
constexpr uint8_t kFairway = 0;
constexpr uint8_t kSand = 2;
constexpr uint8_t kGreen = 3;
constexpr uint8_t kWater = 5;
constexpr uint8_t kLava = 6;
constexpr uint8_t kStone = 7;

// 章の境目を左右方向に曲げる最大量（メートル）
constexpr float kBoundaryBend = 4.0f;
// 斜面の端とホールの間に残す余白（グリーンの半径程度）
constexpr float kBankHoleMargin = 3.0f;

/** @brief 章ごとの地表の模様。 */
enum class SurfacePattern {
  Corridor, // 基本のフェアウェイの通り道をそのまま使う
  Islands,  // ラフ（砂漠は砂）の中にフェアウェイの島が点在する
  Stripes,  // 斜めの刈り込みの縞
  Outcrops, // バイオームの地表（岩・砂・氷）が露頭のように顔を出す
  Wasteland,// バイオームの地表が広がり、蛇行するフェアウェイの小道が通る
  Mosaic,   // ラフ・フェアウェイ・バイオームの地表が入り組む
};

/** @brief 章の模様の形（大きさ・向き・伸び）。 */
struct PatternShape {
  SurfacePattern pattern = SurfacePattern::Corridor;
  float scale = 1.0f;
  float cosAngle = 1.0f;
  float sinAngle = 0.0f;
  float stretch = 1.0f;
  float threshold = 0.0f;
  uint32_t seed = 0;
};

enum class LandformTheme {
  Plateau,      // 平らな台地。わずかにうねる
  RollingHills, // なだらかな丘が連なる
  Dunes,        // 斜めに走る砂丘
  Valley,       // 蛇行する谷。バイオームによって水や溶岩が溜まる
  Mesa,         // 平らな頂を持つ台状の丘
  Ridges,       // 岩の尾根
};

struct Section {
  float start = 0.0f; // wz（0 = 記事の先頭側）
  float end = 0.0f;
  float height = 0.0f; // 章の中央での高さ
  float grade = 0.0f;  // 奥行き方向の勾配（上り・下り）
  LandformTheme theme = LandformTheme::Plateau;
  PatternShape surface;
  uint32_t seed = 0;
};

struct Boundary {
  float center = 0.0f;
  float halfLength = 0.0f;
  float bend = 0.0f;
  uint32_t seed = 0;
};

/** @brief 章の区切り位置と、そこで使えるホールの無い帯の長さ。 */
struct Cut {
  float center = 0.0f;
  float gap = 0.0f;
};

/** @brief 最寄りのホールまでの距離（メートル）を保持する粗い格子。 */
class ClearanceField {
public:
  /**
   * @param originX, originZ 格子の左奥の座標（コースの左奥を原点としたメートル）
   * @param width, depth 格子が覆う範囲
   */
  ClearanceField(float originX, float originZ, float width, float depth,
                 const std::vector<XMFLOAT2> &points)
      : m_originX(originX), m_originZ(originZ),
        m_width(std::max(2, static_cast<int>(std::ceil(width / kCell)) + 1)),
        m_depth(std::max(2, static_cast<int>(std::ceil(depth / kCell)) + 1)),
        m_distance(static_cast<size_t>(m_width) * m_depth, kFar) {
    for (const auto &p : points) {
      const int gx = static_cast<int>(std::lround((p.x - m_originX) / kCell));
      const int gz = static_cast<int>(std::lround((p.y - m_originZ) / kCell));
      if (gx < 0 || gx >= m_width || gz < 0 || gz >= m_depth) continue;
      m_distance[Index(gx, gz)] = 0.0f;
    }
    // 2 パスのチャンファー距離変換
    constexpr float kDiagonal = 1.41421356f;
    for (int z = 0; z < m_depth; ++z) {
      for (int x = 0; x < m_width; ++x) {
        float &d = m_distance[Index(x, z)];
        if (x > 0) d = std::min(d, m_distance[Index(x - 1, z)] + 1.0f);
        if (z > 0) {
          d = std::min(d, m_distance[Index(x, z - 1)] + 1.0f);
          if (x > 0) d = std::min(d, m_distance[Index(x - 1, z - 1)] + kDiagonal);
          if (x + 1 < m_width)
            d = std::min(d, m_distance[Index(x + 1, z - 1)] + kDiagonal);
        }
      }
    }
    for (int z = m_depth - 1; z >= 0; --z) {
      for (int x = m_width - 1; x >= 0; --x) {
        float &d = m_distance[Index(x, z)];
        if (x + 1 < m_width) d = std::min(d, m_distance[Index(x + 1, z)] + 1.0f);
        if (z + 1 < m_depth) {
          d = std::min(d, m_distance[Index(x, z + 1)] + 1.0f);
          if (x + 1 < m_width)
            d = std::min(d, m_distance[Index(x + 1, z + 1)] + kDiagonal);
          if (x > 0) d = std::min(d, m_distance[Index(x - 1, z + 1)] + kDiagonal);
        }
      }
    }
  }

  /** @brief (wx, wz) での最寄りホールまでの距離をメートルで返します。 */
  float Sample(float wx, float wz) const {
    const float fx = std::clamp((wx - m_originX) / kCell, 0.0f,
                                static_cast<float>(m_width - 1));
    const float fz = std::clamp((wz - m_originZ) / kCell, 0.0f,
                                static_cast<float>(m_depth - 1));
    const int x0 = std::min(static_cast<int>(fx), m_width - 2);
    const int z0 = std::min(static_cast<int>(fz), m_depth - 2);
    const float tx = fx - x0;
    const float tz = fz - z0;
    const float top = Lerp(m_distance[Index(x0, z0)], m_distance[Index(x0 + 1, z0)], tx);
    const float bottom =
        Lerp(m_distance[Index(x0, z0 + 1)], m_distance[Index(x0 + 1, z0 + 1)], tx);
    return Lerp(top, bottom, tz) * kCell;
  }

private:
  static constexpr float kCell = 2.0f;
  static constexpr float kFar = 1.0e5f;

  size_t Index(int x, int z) const {
    return static_cast<size_t>(z) * m_width + x;
  }

  float m_originX;
  float m_originZ;
  int m_width;
  int m_depth;
  std::vector<float> m_distance;
};

/** @brief 奥行き方向 1m ごとのホール数の累積和。 */
class RowDensity {
public:
  RowDensity(float worldD, const std::vector<XMFLOAT2> &points)
      : m_prefix(static_cast<size_t>(std::ceil(worldD)) + 2, 0) {
    std::vector<int> counts(m_prefix.size() - 1, 0);
    for (const auto &p : points) {
      const int row = std::clamp(static_cast<int>(p.y), 0,
                                 static_cast<int>(counts.size()) - 1);
      ++counts[row];
    }
    for (size_t i = 0; i < counts.size(); ++i) {
      m_prefix[i + 1] = m_prefix[i] + counts[i];
    }
  }

  /** @brief 1m 幅の行 row にホールがあるかを返します。 */
  bool Occupied(int row) const {
    if (row < 0 || row + 1 >= static_cast<int>(m_prefix.size())) return true;
    return m_prefix[row + 1] != m_prefix[row];
  }

  /** @brief [center - half, center + half] に含まれるホール数を返します。 */
  int Count(float center, float half) const {
    const int last = static_cast<int>(m_prefix.size()) - 1;
    const int a = std::clamp(static_cast<int>(std::floor(center - half)), 0, last);
    const int b = std::clamp(static_cast<int>(std::ceil(center + half)), 0, last);
    return m_prefix[b] - m_prefix[a];
  }

private:
  std::vector<int> m_prefix;
};

/** @brief 2 値のなめらかな最小値。境目で折れ目を作らない。 */
float SoftMin(float a, float b, float k) {
  const float h = std::max(k - std::abs(a - b), 0.0f) / k;
  return std::min(a, b) - h * h * k * 0.25f;
}

float SectionHeightAt(const Section &section, float wz) {
  return section.height + section.grade * (wz - (section.start + section.end) * 0.5f);
}

/** @brief 山の最大の高さ（メートル） */
float MountainScale(int biome) {
  switch (biome) {
  case 1: return 11.0f;
  case 2: return 9.0f;
  case 3: return 18.0f;
  default: return 13.0f;
  }
}

/** @brief 山腹の傾き（tan）。ホールから離れるほどこの傾きで高くなる。 */
float MountainFlank(int biome) {
  switch (biome) {
  case 2: return 0.42f;
  case 3: return 0.56f;
  default: return 0.52f;
  }
}

float TierScale(int biome) {
  switch (biome) {
  case 1: return 0.85f; // 砂漠
  case 2: return 0.55f; // 氷原は穏やかに
  case 3: return 1.35f; // 岩場は起伏を大きく
  default: return 1.0f;
  }
}

LandformTheme PickTheme(int biome, std::mt19937 &rng, LandformTheme previous) {
  // バイオームごとに出やすいテーマを変える（重複可で重み付け）。
  static constexpr LandformTheme kMeadow[] = {
      LandformTheme::RollingHills, LandformTheme::RollingHills,
      LandformTheme::Plateau, LandformTheme::Valley, LandformTheme::Mesa};
  static constexpr LandformTheme kDesert[] = {
      LandformTheme::Dunes, LandformTheme::Dunes, LandformTheme::Mesa,
      LandformTheme::Plateau, LandformTheme::Valley};
  static constexpr LandformTheme kIce[] = {
      LandformTheme::Plateau, LandformTheme::Valley, LandformTheme::Valley,
      LandformTheme::RollingHills};
  static constexpr LandformTheme kRock[] = {
      LandformTheme::Ridges, LandformTheme::Ridges, LandformTheme::Mesa,
      LandformTheme::Valley, LandformTheme::RollingHills};
  const LandformTheme *table = kMeadow;
  size_t count = std::size(kMeadow);
  switch (biome) {
  case 1: table = kDesert; count = std::size(kDesert); break;
  case 2: table = kIce; count = std::size(kIce); break;
  case 3: table = kRock; count = std::size(kRock); break;
  default: break;
  }
  std::uniform_int_distribution<size_t> pick(0, count - 1);
  LandformTheme theme = table[pick(rng)];
  for (int retry = 0; retry < 4 && theme == previous; ++retry) {
    theme = table[pick(rng)];
  }
  return theme;
}

struct ThemeSample {
  float height = 0.0f;
  float amplitude = 0.0f; // なじませ帯の幅を決める最大の高さ
  bool basin = false;     // 谷底（液体を溜められる場所）
  bool rocky = false;     // 岩肌を見せる場所
  bool sandy = false;     // 砂を見せる場所
};

ThemeSample EvaluateTheme(const Section &section, float wx, float wz,
                          float worldW) {
  ThemeSample sample;
  const uint32_t seed = section.seed;
  switch (section.theme) {
  case LandformTheme::Plateau:
    sample.amplitude = 0.8f;
    sample.height = (ValueNoise(wx / 45.0f, wz / 45.0f, seed) * 0.5f + 0.5f) * 0.8f;
    break;
  case LandformTheme::RollingHills: {
    sample.amplitude = 4.0f;
    const float n = ValueNoise(wx / 42.0f, wz / 46.0f, seed) * 0.72f +
                    ValueNoise(wx / 19.0f, wz / 21.0f, seed ^ 0x51u) * 0.28f;
    sample.height = std::max(0.0f, n + 0.15f) * 4.0f;
    break;
  }
  case LandformTheme::Dunes: {
    sample.amplitude = 2.8f;
    const float warp = ValueNoise(wx / 60.0f, wz / 60.0f, seed) * 2.2f;
    const float crest = std::sin((wx * 0.75f + wz * 0.45f) / 8.5f + warp);
    const float dune = 0.5f + 0.5f * crest;
    sample.height = dune * dune * 2.8f;
    sample.sandy = dune > 0.55f;
    break;
  }
  case LandformTheme::Valley: {
    sample.amplitude = 4.5f;
    const float phase = static_cast<float>(seed % 628u) * 0.01f;
    const float centerX =
        worldW * (0.5f + 0.3f * std::sin(wz / 55.0f + phase) +
                  0.08f * ValueNoise(0.0f, wz / 30.0f, seed));
    const float d = std::abs(wx - centerX);
    const float bowl = std::exp(-(d / 11.0f) * (d / 11.0f));
    const float rim = std::exp(-((d - 19.0f) / 8.0f) * ((d - 19.0f) / 8.0f));
    sample.height = -4.5f * bowl + 1.2f * rim;
    sample.basin = bowl > 0.72f;
    break;
  }
  case LandformTheme::Mesa: {
    sample.amplitude = 4.5f;
    const float n = ValueNoise(wx / 48.0f, wz / 52.0f, seed);
    sample.height = SmoothStep(-0.2f, 0.45f, n) * 4.5f;
    sample.rocky = n > 0.05f && n < 0.3f;
    break;
  }
  case LandformTheme::Ridges: {
    sample.amplitude = 5.0f;
    const float n = ValueNoise(wx / 30.0f, wz / 34.0f, seed) * 0.8f +
                    ValueNoise(wx / 13.0f, wz / 15.0f, seed ^ 0x9eu) * 0.2f;
    const float ridge = 1.0f - std::min(std::abs(n) * 1.6f, 1.0f);
    sample.height = ridge * ridge * 5.0f;
    sample.rocky = ridge > 0.7f;
    break;
  }
  }
  return sample;
}

/** @brief バイオームの主な地面と、アクセントの地表（どちらも OB ではない）。 */
uint8_t GroundMaterial(int biome) { return biome == 1 ? kSand : 1; }
uint8_t AccentMaterial(int biome) {
  switch (biome) {
  case 1: return kStone;
  case 2: return 4; // 氷
  case 3: return kStone;
  default: return kSand;
  }
}

PatternShape MakePatternShape(int biome, uint32_t seed) {
  std::mt19937 rng(seed ^ 0x9a7eu);
  std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
  static constexpr SurfacePattern kPatterns[] = {
      SurfacePattern::Corridor, SurfacePattern::Islands, SurfacePattern::Stripes,
      SurfacePattern::Outcrops, SurfacePattern::Wasteland, SurfacePattern::Mosaic};
  PatternShape shape;
  shape.pattern = kPatterns[static_cast<size_t>(dist01(rng) * 5.999f)];
  // 氷原は氷の上でボールが止まりにくいので、氷が広がる模様を避ける。
  if (biome == 2 && shape.pattern == SurfacePattern::Wasteland) {
    shape.pattern = SurfacePattern::Islands;
  }
  shape.scale = 0.6f + dist01(rng) * 1.4f;
  const float angle = dist01(rng) * 3.14159f;
  shape.cosAngle = std::cos(angle);
  shape.sinAngle = std::sin(angle);
  shape.stretch = 1.0f + dist01(rng) * 2.0f;
  shape.threshold = (dist01(rng) - 0.5f) * 0.3f;
  shape.seed = rng();
  return shape;
}

/**
 * @brief 章の模様から地表を決めます。
 * @return 変更しない場合は current をそのまま返す。
 */
uint8_t EvaluatePattern(const PatternShape &shape, int biome, float wx, float wz,
                        float worldW, uint8_t current) {
  // 回転・伸長した座標
  const float u = (shape.cosAngle * wx - shape.sinAngle * wz) / (shape.scale * shape.stretch);
  const float v = (shape.sinAngle * wx + shape.cosAngle * wz) / shape.scale;
  auto noise = [&](float size, uint32_t salt) {
    return ValueNoise(u / size, v / size, shape.seed ^ salt) * 0.7f +
           ValueNoise(u / (size * 0.45f), v / (size * 0.45f), shape.seed ^ (salt * 7u)) * 0.3f;
  };
  const uint8_t ground = GroundMaterial(biome);
  const uint8_t accent = AccentMaterial(biome);
  switch (shape.pattern) {
  case SurfacePattern::Corridor:
    return current;
  case SurfacePattern::Islands:
    return noise(14.0f, 0x11u) > 0.15f + shape.threshold ? kFairway : ground;
  case SurfacePattern::Stripes: {
    const float stripe = std::sin(u / 2.2f + noise(30.0f, 0x21u) * 1.5f);
    return stripe > shape.threshold ? kFairway : 1;
  }
  case SurfacePattern::Outcrops: {
    const float n = noise(10.0f, 0x31u);
    if (n > 0.38f + shape.threshold) return accent;
    if (n < -0.35f) return kFairway;
    return current;
  }
  case SurfacePattern::Wasteland: {
    const float center =
        worldW * (0.5f + 0.32f * std::sin(wz / (38.0f * shape.scale) +
                                          static_cast<float>(shape.seed % 97u)));
    if (std::abs(wx - center) < 5.0f + 3.0f * noise(20.0f, 0x41u)) return kFairway;
    return noise(12.0f, 0x42u) > 0.35f + shape.threshold ? accent : ground;
  }
  case SurfacePattern::Mosaic: {
    const float n = noise(9.0f, 0x51u);
    if (n < -0.18f + shape.threshold) return 1;
    if (n < 0.28f + shape.threshold) return kFairway;
    return accent;
  }
  }
  return current;
}

/**
 * @brief 章の境目を決めます。
 * @details 見出し位置へ寄せたうえで、近くで最も長い「ホールの無い帯」の中央に置く。
 *          斜面はこの帯の中だけに作るので、ホールが坂に乗らない。
 */
std::vector<Cut> PlanBoundaries(float worldD, float teeZ,
                                const std::vector<float> &headingZ,
                                const RowDensity &density, std::mt19937 &rng) {
  std::uniform_real_distribution<float> firstLength(40.0f, 75.0f);
  std::uniform_real_distribution<float> length(70.0f, 165.0f);
  constexpr float kMinSection = 32.0f;
  constexpr int kSearch = 40;

  auto refine = [&](float desired, float low, float high) {
    // 見出しが近くにあればそこを章の区切りにする。
    float anchor = desired;
    float bestHeading = 36.0f;
    for (float h : headingZ) {
      const float offset = std::abs(h - desired);
      if (offset < bestHeading && h > low && h < high) {
        bestHeading = offset;
        anchor = h;
      }
    }
    Cut best{anchor, 0.0f};
    float bestScore = -1.0e9f;
    const int first = std::max(static_cast<int>(low), static_cast<int>(anchor) - kSearch);
    const int last = std::min(static_cast<int>(high), static_cast<int>(anchor) + kSearch);
    int runStart = -1;
    for (int row = first; row <= last + 1; ++row) {
      const bool empty = row <= last && !density.Occupied(row);
      if (empty && runStart < 0) {
        runStart = row;
      } else if (!empty && runStart >= 0) {
        const float gap = static_cast<float>(row - runStart);
        const float mid = static_cast<float>(runStart + row) * 0.5f;
        const float score = gap - std::abs(mid - anchor) * 0.12f;
        if (score > bestScore) {
          bestScore = score;
          best = {mid, gap};
        }
        runStart = -1;
      }
    }
    return best;
  };

  std::vector<Cut> cuts;
  // スタート地点から記事の先頭側へ。最初の区切りは近くに置き、変化がすぐ見えるようにする。
  float cursor = teeZ;
  float span = firstLength(rng);
  while (true) {
    const float desired = cursor - span;
    if (desired < kMinSection) break;
    const Cut c = refine(desired, kMinSection, cursor - kMinSection);
    if (c.center >= cursor - kMinSection * 0.5f) break;
    cuts.push_back(c);
    cursor = c.center;
    span = length(rng);
  }
  // スタート地点より後ろ側
  const float behind = worldD - teeZ;
  if (behind > kMinSection * 2.5f) {
    cuts.push_back(refine(teeZ + behind * 0.5f, teeZ + kMinSection * 0.5f,
                          worldD - kMinSection));
  }
  std::sort(cuts.begin(), cuts.end(),
            [](const Cut &a, const Cut &b) { return a.center < b.center; });
  return cuts;
}

} // namespace

void ApplyLandforms(TerrainData &data, const std::string &seedText,
                    const std::vector<XMFLOAT2> &holePositions) {
  const int resX = data.config.resolutionX;
  const int resZ = data.config.resolutionZ;
  const float worldW = data.config.worldWidth;
  const float worldD = data.config.worldDepth;
  if (resX < 2 || resZ < 2 || worldW <= 0.0f || worldD <= 0.0f ||
      data.heightMap.size() < static_cast<size_t>(resX) * resZ) {
    return;
  }
  const int biome = data.config.biome;
  // 章の配置やテーマはコースを基準に決め、延長地形でも同じ章がコースの外へ続くようにする。
  const CourseFrame frame = CourseFrameOf(data.config);
  const float courseW = frame.width;
  const float courseD = frame.depth;

  std::string salted = seedText + "#landforms";
  std::seed_seq seq(salted.begin(), salted.end());
  std::mt19937 rng(seq);
  std::uniform_real_distribution<float> dist01(0.0f, 1.0f);

  // ボールの初期位置（WikiPageLoader: z = -depth * 0.4）
  const float teeZ = courseD * 0.9f;

  // ホールをフィールド左上原点のメートル座標に変換。ティーも平らに保つ対象に含める。
  std::vector<XMFLOAT2> keepFlat;
  keepFlat.reserve(holePositions.size() + 9);
  for (const auto &p : holePositions) {
    keepFlat.push_back({p.x + courseW * 0.5f, courseD * 0.5f - p.y});
  }
  for (int i = -1; i <= 1; ++i) {
    for (int j = -1; j <= 1; ++j) {
      keepFlat.push_back({courseW * 0.5f + i * 5.0f, teeZ + j * 5.0f});
    }
  }
  const ClearanceField clearance(-frame.marginX, -frame.marginZ, worldW, worldD,
                                 keepFlat);
  const RowDensity density(courseD, keepFlat);

  std::vector<float> headingZ;
  for (const auto &region : data.config.htmlRegions) {
    if (region.kind == HtmlRegionKind::Heading) {
      headingZ.push_back((region.v + region.height * 0.5f) * courseD);
    }
  }

  const std::vector<Cut> cuts =
      PlanBoundaries(courseD, teeZ, headingZ, density, rng);

  // 章を作る
  std::vector<Section> sections(cuts.size() + 1);
  for (size_t i = 0; i < sections.size(); ++i) {
    sections[i].start = i == 0 ? 0.0f : cuts[i - 1].center;
    sections[i].end = i < cuts.size() ? cuts[i].center : courseD;
    sections[i].seed = rng();
  }
  size_t teeSection = 0;
  for (size_t i = 0; i < sections.size(); ++i) {
    if (teeZ >= sections[i].start && teeZ <= sections[i].end) {
      teeSection = i;
    }
  }

  // テーマ。スタート地点の章は落ち着いた地形にする。
  LandformTheme previous = LandformTheme::Plateau;
  for (size_t i = 0; i < sections.size(); ++i) {
    sections[i].theme = PickTheme(biome, rng, previous);
    previous = sections[i].theme;
  }
  sections[teeSection].theme =
      dist01(rng) < 0.5f ? LandformTheme::Plateau : LandformTheme::RollingHills;
  // 章ごとの地表の模様。形の乱数は章の種から作り、高さの乱数列を変えない。
  for (auto &section : sections) {
    section.surface = MakePatternShape(biome, section.seed);
  }
  sections[teeSection].surface.pattern = SurfacePattern::Corridor;

  // 章の勾配と境目の斜面。上り・下りが続きやすくして、山を登り下りする流れを作る。
  // 勾配はホールが坂に乗ったと感じない程度（最大 2 度）に抑え、
  // 境目の斜面はホールの無い帯に収まる長さと、崖にならない高さの差にする。
  std::vector<Boundary> boundaries(cuts.size());
  const float tierScale = TierScale(biome);
  const float maxTier = 42.0f * tierScale;
  const float minTier = -14.0f * tierScale;
  constexpr float kBankSlopeTan = 0.5f; // 最も急な所で約 27 度
  constexpr float kMaxGrade = 0.035f;
  float climb = dist01(rng) < 0.65f ? 1.0f : -1.0f; // 登り基調で始める
  auto planStep = [&](size_t boundaryIndex, size_t fromIndex, size_t toIndex,
                      float direction) {
    const Cut &cut = cuts[boundaryIndex];
    const Section &from = sections[fromIndex];
    Section &to = sections[toIndex];
    const float fromHeight = SectionHeightAt(from, cut.center);

    // 次の章の勾配。70% で同じ向きを続ける。
    if (dist01(rng) > 0.7f) climb = -climb;
    const float toLength = to.end - to.start;
    float grade = std::min(kMaxGrade, (0.012f + dist01(rng) * 0.023f) * tierScale) * climb;
    // 奥行きの進む向き（direction）に沿って climb が効くようにする。
    grade *= -direction;

    float step = (3.0f + dist01(rng) * 7.0f) * tierScale;
    const float stepSign = dist01(rng) < 0.7f ? climb : -climb;
    float delta = stepSign * step;

    // 使える半幅（ホールから余白を取った残り）
    const float neighborLength = std::min(from.end - from.start, toLength);
    const float usableHalf = std::min(cut.gap * 0.5f - kBankHoleMargin,
                                      std::min(neighborLength * 0.4f, 30.0f));
    Boundary &boundary = boundaries[boundaryIndex];
    boundary.center = cut.center;
    boundary.seed = rng();
    if (usableHalf < 1.0f) {
      // 帯が狭すぎるので段差を付けず、勾配だけでつなぐ。
      boundary.halfLength = 1.0f;
      boundary.bend = 0.0f;
      delta = 0.0f;
    } else {
      float half = std::abs(delta) * 1.5f / kBankSlopeTan * 0.5f;
      half = std::max(half, std::min(6.0f, usableHalf));
      if (half > usableHalf) {
        half = usableHalf;
        delta = std::copysign(half * 2.0f * kBankSlopeTan / 1.5f, delta);
      }
      boundary.halfLength = half;
      boundary.bend = std::clamp(usableHalf - half, 0.0f, kBoundaryBend);
    }

    // 章の遠い端が高さの範囲を超えるなら向きを反転する。
    const float farEnd = direction < 0.0f ? to.start : to.end;
    const float toMid = (to.start + to.end) * 0.5f;
    auto farHeight = [&](float g, float d) {
      const float midHeight = fromHeight + d - g * (cut.center - toMid);
      return midHeight + g * (farEnd - toMid);
    };
    float endHeight = farHeight(grade, delta);
    if (endHeight > maxTier || endHeight < minTier) {
      climb = -climb;
      grade = -grade;
      delta = -delta;
      endHeight = farHeight(grade, delta);
      if (endHeight > maxTier || endHeight < minTier) {
        grade = 0.0f;
      }
    }
    to.grade = grade;
    to.height = fromHeight + delta - grade * (cut.center - toMid);
  };

  // スタート地点の章はティー位置で高さ 0。前方（記事の先頭側 = wz が減る向き）へ登り下りを並べる。
  {
    Section &tee = sections[teeSection];
    tee.grade = (dist01(rng) - 0.5f) * 0.03f;
    tee.height = -tee.grade * (teeZ - (tee.start + tee.end) * 0.5f);
  }
  for (size_t i = teeSection; i > 0; --i) {
    planStep(i - 1, i, i - 1, -1.0f);
  }
  climb = -climb;
  for (size_t i = teeSection + 1; i < sections.size(); ++i) {
    planStep(i - 1, i - 1, i, 1.0f);
  }

  // 横方向の傾き。奥行きに沿ってゆっくり変え、山腹を歩くような片側の高まりを作る。
  const uint32_t crossSeed = rng();
  const uint32_t mountainSeed = rng();
  const float mountainScale = MountainScale(biome);
  const float mountainFlank = MountainFlank(biome);
  // グリーン（最大半径 1.2 倍）とその縁の外から起伏を始める。
  const float flatMargin =
      BaseGreenRadius(courseW, courseD, holePositions.size()) * 1.2f + 2.0f;

  const float cellX = worldW / static_cast<float>(resX - 1);
  const float cellZ = worldD / static_cast<float>(resZ - 1);
  // 章の境目の曲がり。コース内はホールを避けて小さく、コースの外（延長地形）では
  // 離れるほど大きく曲げ、境目が一直線に伸びないようにする。
  auto boundaryBend = [&](const Boundary &boundary, float wx) {
    const float outside = std::max({0.0f, -wx, wx - courseW});
    const float wideBend = std::min(outside * 0.45f, 20.0f);
    return ValueNoise(wx / 28.0f, 0.0f, boundary.seed) * boundary.bend +
           ValueNoise(wx / 47.0f, 3.7f, boundary.seed ^ 0x5bu) * wideBend;
  };

  size_t sectionIndex = 0;
  for (int z = 0; z < resZ; ++z) {
    const float wz = static_cast<float>(z) * cellZ - frame.marginZ;
    while (sectionIndex + 1 < sections.size() && wz > sections[sectionIndex].end) {
      ++sectionIndex;
    }
    for (int x = 0; x < resX; ++x) {
      const float wx = static_cast<float>(x) * cellX - frame.marginX;
      const size_t idx = static_cast<size_t>(z) * resX + x;

      // 近い方の境目を探し、その前後の章をなめらかにつなぐ。
      size_t a = sectionIndex;
      size_t b = sectionIndex;
      float blend = 0.0f;
      auto tryBoundary = [&](size_t bi) {
        const Boundary &boundary = boundaries[bi];
        // 境目は直線にせず、ゆるく曲げて自然な地形の縁にする。
        const float bend = boundaryBend(boundary, wx);
        const float center = boundary.center + bend;
        if (std::abs(wz - center) < boundary.halfLength) {
          a = bi;
          b = bi + 1;
          blend = SmoothStep(center - boundary.halfLength,
                             center + boundary.halfLength, wz);
          return true;
        }
        return false;
      };
      if (sectionIndex < boundaries.size() && tryBoundary(sectionIndex)) {
      } else if (sectionIndex > 0) {
        tryBoundary(sectionIndex - 1);
      }
      if (a == b) {
        // 曲げた境目の外側では、どちらの章にいるかを曲げた位置で判定する。
        if (sectionIndex < boundaries.size()) {
          const Boundary &next = boundaries[sectionIndex];
          const float bend = boundaryBend(next, wx);
          if (wz > next.center + bend) {
            a = b = sectionIndex + 1;
          }
        }
        if (a == b && sectionIndex > 0) {
          const Boundary &prev = boundaries[sectionIndex - 1];
          const float bend = boundaryBend(prev, wx);
          if (wz < prev.center + bend) {
            a = b = sectionIndex - 1;
          }
        }
      }

      const float clear = clearance.Sample(wx, wz);
      // 章の境目の斜面からの距離。境目の近くではテーマの起伏や山を低くし、
      // 隣の章と急に切り替わったり、斜面と山が重なって崖になったりしないようにする。
      float boundaryDistance = 1.0e6f;
      for (size_t bi = sectionIndex > 0 ? sectionIndex - 1 : 0;
           bi < std::min(boundaries.size(), sectionIndex + 1); ++bi) {
        const Boundary &boundary = boundaries[bi];
        const float center =
            boundary.center +
            boundaryBend(boundary, wx);
        boundaryDistance = std::min(
            boundaryDistance,
            std::max(0.0f, std::abs(wz - center) - boundary.halfLength));
      }
      auto contribution = [&](const Section &section, ThemeSample &out) {
        out = EvaluateTheme(section, wx, wz, courseW);
        // ホールから離れるほど起伏を強くする。帯の幅は高さに比例させ、崖にしない。
        const float fadeWidth = out.amplitude * 3.2f + 2.0f;
        const float weight =
            SmoothStep(flatMargin, flatMargin + fadeWidth, clear) *
            SmoothStep(0.0f, fadeWidth + 3.0f, boundaryDistance);
        out.basin = out.basin && weight > 0.8f;
        out.rocky = out.rocky && weight > 0.8f;
        out.sandy = out.sandy && weight > 0.6f;
        return SectionHeightAt(section, wz) + out.height * weight;
      };
      ThemeSample sampleA;
      ThemeSample sampleB;
      const float heightA = contribution(sections[a], sampleA);
      float height = heightA;
      ThemeSample dominant = sampleA;
      if (a != b) {
        const float heightB = contribution(sections[b], sampleB);
        height = Lerp(heightA, heightB, blend);
        if (blend > 0.5f) dominant = sampleB;
        dominant.basin = false; // 斜面には液体を置かない
      }
      // 横方向の傾き（最大 1 度、フィールド幅に対して ±0.7m 程度）
      const float cross = ValueNoise(0.0f, wz / 160.0f, crossSeed) * 0.018f;
      height += cross * (wx - courseW * 0.5f);

      // 山。ホールから離れるほど一定の傾きで高くなり、隙間の中央に尾根ができる。
      // 峰の高さはゆっくり変わるノイズで決め、場所ごとに高い山・低い山を作る。
      const float peakNoise = ValueNoise(wx / 75.0f, wz / 90.0f, mountainSeed) * 0.6f +
                              ValueNoise(wx / 23.0f, wz / 27.0f, mountainSeed ^ 0x3cu) * 0.25f;
      const float peak = std::max(0.0f, 0.55f + peakNoise) * mountainScale;
      const float room =
          std::min(std::max(0.0f, clear - flatMargin), boundaryDistance) *
          mountainFlank;
      const float mountain = std::max(0.0f, SoftMin(peak, room, 2.5f));
      height += mountain;
      data.heightMap[idx] += height;

      // テーマに合わせた地表。ホール周辺の緑は触らない。
      uint8_t &material = data.materialMap[idx];
      if (material == kGreen) continue;

      // 章の地表の模様。境目では前後の章の模様をノイズで散らしながら入れ替える。
      // 水や溶岩（OB）はそのまま残す。
      if (material != kWater && material != kLava) {
        size_t patternSection = a;
        if (a != b) {
          const float scatter =
              ValueNoise(wx / 4.0f, wz / 4.0f, boundaries[a].seed ^ 0x77u) * 0.35f;
          patternSection = blend + scatter > 0.5f ? b : a;
        }
        material = EvaluatePattern(sections[patternSection].surface, biome, wx, wz,
                                   courseW, material);
      }
      const bool summit = mountain > std::max(4.0f, mountainScale * 0.45f);
      if (summit && biome == 2) {
        material = 4; // 山頂の氷
      } else if (summit && biome != 0) {
        material = kStone;
      } else if (dominant.basin && mountain < 0.5f) {
        if (biome == 2) material = kWater;
        else if (biome == 3) material = kLava;
        else if (biome == 1) material = kSand;
      } else if (dominant.rocky && (biome == 1 || biome == 3)) {
        material = kStone;
      } else if (dominant.sandy && biome == 1) {
        material = kSand;
      }
    }
  }
}

} // namespace game::systems
