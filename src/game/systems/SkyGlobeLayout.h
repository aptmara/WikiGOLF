#pragma once
/**
 * @file SkyGlobeLayout.h
 * @brief コースの外の空に浮かべる Wikipedia パズル地球儀の配置を決めます。
 */

#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace game::systems {

/** @brief 空に浮かぶ地球儀 1 つ分の配置と動き。 */
struct SkyGlobePlacement {
  DirectX::XMFLOAT3 position = {0.0f, 0.0f, 0.0f}; // 基準位置（上下に揺れる中心）
  float scale = 1.0f;
  float tilt = 0.0f;       // 自転軸の傾き（ラジアン）
  float spinSpeed = 0.0f;  // 自転の速さ（ラジアン/秒、負なら逆回り）
  float bobPhase = 0.0f;   // 上下の揺れの位相
  float bobHeight = 0.0f;  // 上下の揺れ幅（メートル）
};

/** @brief 地球儀モデル（Wikipedia_puzzle_globe_3D_render.stl）の半径（スケール 1 のとき）。 */
inline constexpr float kSkyGlobeModelRadius = 0.57f;

/**
 * @brief 段階的 LOD のモデル（LOD1 以降は tools/build_globe_lods.py で作成）。
 * @details LOD0 はタイトル画面と同じ元のモデル。文字が画面上でつぶれて見えなくなる距離まで
 *          LOD0 を使い、そこから先で軽いモデルへ切り替える。
 */
inline constexpr const char *kSkyGlobeLodMeshes[] = {
    "Assets/models/Wikipedia_puzzle_globe_3D_render.stl", // 約 31 万三角形
    "Assets/models/wiki_globe_lod1.stl", // 約 13 万三角形（文字・溝を多めに残す）
    "Assets/models/wiki_globe_lod2.stl", // 約 3 万三角形（一律に削る）
};
inline constexpr int kSkyGlobeLodCount = 3;

/**
 * @brief 「距離 ÷ 地球儀の半径」から LOD を選びます。
 * @details 1080p・縦画角 60 度で、文字の線は この値が約 33 を超えると 1 ピクセル未満になる。
 *          見比べて差が出ないよう、LOD1 へは 55、LOD2 へは 110 で切り替える。
 *          current は今の LOD。境目でちらつかないよう、切り替えに約 1 割の幅を持たせる。
 */
inline int SelectSkyGlobeLod(float distance, float radius, int current) {
  constexpr float kToLod1 = 55.0f;
  constexpr float kToLod2 = 110.0f;
  constexpr float kHysteresis = 1.1f;
  const float ratio = distance / std::max(radius, 0.01f);
  // 細かいほうから粗いほうへ移るときは少し遠くで、戻るときは少し近くで切り替える。
  const float toLod1 = current >= 1 ? kToLod1 / kHysteresis : kToLod1 * kHysteresis;
  const float toLod2 = current >= 2 ? kToLod2 / kHysteresis : kToLod2 * kHysteresis;
  if (ratio > toLod2) return 2;
  if (ratio > toLod1) return 1;
  return 0;
}

/** @brief 最小と最大のスケール。 */
inline constexpr float kSkyGlobeMinScale = 5.0f;   // 半径 約 3m
inline constexpr float kSkyGlobeMaxScale = 180.0f; // 半径 約 100m

/** @brief コース外周の壁（TerrainObstacleLayout::BuildWalls）の厚み。 */
inline constexpr float kSkyGlobeBarrierThickness = 6.0f;

/** @brief 壁・地形・ほかの地球儀との間に空ける余白（メートル）。 */
inline constexpr float kSkyGlobeClearance = 6.0f;

/** @brief 点 (x, z) からコースの矩形までの水平距離（コース内なら 0）。 */
inline float DistanceOutsideCourse(float x, float z, float halfWidth, float halfDepth) {
  const float dx = std::max(std::abs(x) - halfWidth, 0.0f);
  const float dz = std::max(std::abs(z) - halfDepth, 0.0f);
  return std::sqrt(dx * dx + dz * dz);
}

/**
 * @brief 空に浮かべる地球儀の配置を決めます。
 * @param fieldWidth, fieldDepth コースの寸法
 * @param seed 記事ごとに配置を変える種
 * @param groundHeight (x, z) の地面の高さ（延長地形を含む）を返す関数
 * @details
 *  - コースの上には置かず、外周の壁の外側（四方）だけに置く。
 *  - 大きさは半径 約 3m から 約 100m までばらばらにする（小さいものを多めに）。
 *  - 球が壁に触れないよう、コースの矩形から「半径 + 壁の厚み + 余白」以上離す。
 *  - 球が地形にめり込まないよう、球の真下の範囲で最も高い地面より上に浮かべる。
 *  - 地球儀どうしも重ならないようにする。
 */
template <typename GroundHeight>
std::vector<SkyGlobePlacement> BuildSkyGlobeLayout(float fieldWidth, float fieldDepth,
                                                   uint32_t seed,
                                                   GroundHeight &&groundHeight) {
  std::vector<SkyGlobePlacement> globes;
  if (fieldWidth <= 0.0f || fieldDepth <= 0.0f) {
    return globes;
  }
  std::mt19937 rng(seed);
  std::uniform_real_distribution<float> dist01(0.0f, 1.0f);

  const int count = std::clamp(static_cast<int>(fieldDepth / 130.0f), 8, 16);
  const float halfW = fieldWidth * 0.5f;
  const float halfD = fieldDepth * 0.5f;
  const float logMin = std::log(kSkyGlobeMinScale);
  const float logMax = std::log(kSkyGlobeMaxScale);

  globes.reserve(count);
  for (int i = 0; i < count; ++i) {
    for (int attempt = 0; attempt < 24; ++attempt) {
      // 大きさ: 対数で散らし、小さいものを多めにする。
      const float sizeRoll = std::pow(dist01(rng), 1.7f);
      const float scale = std::exp(logMin + (logMax - logMin) * sizeRoll);
      const float radius = scale * kSkyGlobeModelRadius;

      // 壁の外側で、球が壁に触れない距離から先に置く。大きいものほど遠くへ。
      const float minGap = radius + kSkyGlobeBarrierThickness + kSkyGlobeClearance;
      const float gap = minGap + dist01(rng) * (40.0f + radius * 1.5f);
      float x = 0.0f;
      float z = 0.0f;
      if (dist01(rng) < 0.72f) {
        // 左右
        const float side = dist01(rng) < 0.5f ? -1.0f : 1.0f;
        x = side * (halfW + gap);
        z = (dist01(rng) - 0.5f) * (fieldDepth + 200.0f);
      } else {
        // 手前と奥
        const float side = dist01(rng) < 0.5f ? -1.0f : 1.0f;
        z = side * (halfD + gap);
        x = (dist01(rng) - 0.5f) * (fieldWidth + 300.0f);
      }
      if (DistanceOutsideCourse(x, z, halfW, halfD) < minGap) {
        continue; // 角の近くなどで壁に近すぎる
      }

      // 球の真下の範囲で最も高い地面を探す。大きな球ほど同心円を増やし、
      // 間の尾根を見落とさないよう、およそ 8m 以下の間隔で調べる。
      float highestGround = groundHeight(x, z);
      const int rings = std::clamp(static_cast<int>(std::ceil(radius / 8.0f)), 2, 14);
      for (int ringIndex = 1; ringIndex <= rings; ++ringIndex) {
        const float ringRadius = radius * static_cast<float>(ringIndex) / rings;
        const int samples = std::max(8, static_cast<int>(std::ceil(6.28318f * ringRadius / 8.0f)));
        for (int k = 0; k < samples; ++k) {
          const float angle = 6.28318f * static_cast<float>(k) / samples;
          highestGround = std::max(highestGround,
                                   groundHeight(x + std::cos(angle) * ringRadius,
                                                z + std::sin(angle) * ringRadius));
        }
      }
      const float bobHeight = 0.6f + dist01(rng) * 1.8f;
      const float lowest = highestGround + radius + bobHeight + kSkyGlobeClearance;
      const float y = lowest + dist01(rng) * (25.0f + radius * 0.8f);

      // ほかの地球儀と重ならない
      bool overlaps = false;
      for (const auto &other : globes) {
        const float otherRadius = other.scale * kSkyGlobeModelRadius;
        const float dx = other.position.x - x;
        const float dy = other.position.y - y;
        const float dz = other.position.z - z;
        const float minDistance = otherRadius + radius + kSkyGlobeClearance +
                                  other.bobHeight + bobHeight;
        if (dx * dx + dy * dy + dz * dz < minDistance * minDistance) {
          overlaps = true;
          break;
        }
      }
      if (overlaps) {
        continue;
      }

      SkyGlobePlacement globe;
      globe.position = {x, y, z};
      globe.scale = scale;
      globe.tilt = (dist01(rng) - 0.5f) * 0.8f;
      // 大きいものほどゆっくり回す
      globe.spinSpeed = (0.05f + dist01(rng) * 0.25f) * std::sqrt(kSkyGlobeMinScale / scale) *
                        (dist01(rng) < 0.25f ? -1.0f : 1.0f);
      globe.bobPhase = dist01(rng) * 6.28318f;
      globe.bobHeight = bobHeight;
      globes.push_back(globe);
      break;
    }
  }
  return globes;
}

} // namespace game::systems
