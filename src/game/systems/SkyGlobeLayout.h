#pragma once
/**
 * @file SkyGlobeLayout.h
 * @brief コースの空に浮かべる Wikipedia パズル地球儀の配置を決めます。
 */

#include <DirectXMath.h>
#include <algorithm>
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

/** @brief 空の地球儀の LOD モデル（tools/build_globe_lods.py で元の STL から作成）。 */
inline constexpr const char *kSkyGlobeLodMeshes[] = {
    "Assets/models/wiki_globe_lod0.obj", // 約 1.3 万三角形
    "Assets/models/wiki_globe_lod1.obj", // 約 3400 三角形
    "Assets/models/wiki_globe_lod2.obj", // 約 900 三角形
};
inline constexpr int kSkyGlobeLodCount = 3;

/** @brief 地球儀モデルの半径（スケール 1 のとき）。 */
inline constexpr float kSkyGlobeModelRadius = 0.57f;

/**
 * @brief カメラからの距離と大きさから LOD を選びます。
 * @details 見かけの大きさ（距離 ÷ 半径）で決める。近い大きな地球儀ほど細かいモデルを使う。
 *          current は今の LOD。境目でちらつかないよう、切り替えに少し幅（ヒステリシス）を持たせる。
 */
inline int SelectSkyGlobeLod(float distance, float scale, int current) {
  const float radius = std::max(scale * kSkyGlobeModelRadius, 0.01f);
  const float ratio = distance / radius;
  constexpr float kToLod1 = 25.0f;
  constexpr float kToLod2 = 70.0f;
  constexpr float kHysteresis = 1.12f;
  auto threshold = [&](float value, bool goingCoarser) {
    return goingCoarser ? value * kHysteresis : value / kHysteresis;
  };
  int lod = 0;
  if (ratio > threshold(kToLod1, current < 1)) lod = 1;
  if (ratio > threshold(kToLod2, current < 2)) lod = 2;
  return lod;
}

/** @brief コースの外へ地球儀を広げる距離（延長地形の範囲に合わせる）。 */
constexpr float kSkyGlobeSpread = 140.0f;

/**
 * @brief 空に浮かべる地球儀の配置を決めます。
 * @param fieldWidth, fieldDepth コースの寸法
 * @param seed 記事ごとに配置を変える種
 * @param groundHeight (x, z) の地面の高さ（延長地形を含む）を返す関数
 * @details 数はコースの長さに応じて 8〜20 個。
 *          コースの上と四方の延長地形の上に散らし、
 *          地面から 30〜80m の高さに、互いに重ならない間隔で置く。
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

  const int count = std::clamp(static_cast<int>(fieldDepth / 110.0f), 8, 20);
  const float halfW = fieldWidth * 0.5f;
  const float halfD = fieldDepth * 0.5f;
  constexpr float kMinSpacing = 40.0f;

  globes.reserve(count);
  for (int i = 0; i < count; ++i) {
    for (int attempt = 0; attempt < 8; ++attempt) {
      // 4 割はコースの上空、残りは左右と前後の延長地形の上空に置く。
      const float z = -halfD - kSkyGlobeSpread * 0.5f +
                      dist01(rng) * (fieldDepth + kSkyGlobeSpread);
      float x = 0.0f;
      if (dist01(rng) < 0.4f) {
        x = (dist01(rng) - 0.5f) * fieldWidth;
      } else {
        const float side = dist01(rng) < 0.5f ? -1.0f : 1.0f;
        x = side * (halfW + 15.0f + dist01(rng) * (kSkyGlobeSpread - 15.0f));
      }

      bool crowded = false;
      for (const auto &other : globes) {
        const float dx = other.position.x - x;
        const float dz = other.position.z - z;
        if (dx * dx + dz * dz < kMinSpacing * kMinSpacing) {
          crowded = true;
          break;
        }
      }
      if (crowded && attempt < 7) {
        continue;
      }

      SkyGlobePlacement globe;
      const float ground = groundHeight(x, z);
      globe.position = {x, ground + 30.0f + dist01(rng) * 50.0f, z};
      // モデルの半径は約 0.57。空で見えるよう半径 3〜9m 程度にし、小さいものを多めにする。
      globe.scale = 5.0f + dist01(rng) * dist01(rng) * 11.0f;
      globe.tilt = (dist01(rng) - 0.5f) * 0.8f;
      globe.spinSpeed = (0.12f + dist01(rng) * 0.3f) * (dist01(rng) < 0.25f ? -1.0f : 1.0f);
      globe.bobPhase = dist01(rng) * 6.28318f;
      globe.bobHeight = 0.6f + dist01(rng) * 1.8f;
      globes.push_back(globe);
      break;
    }
  }
  return globes;
}

} // namespace game::systems
