#pragma once
/**
 * @file GrassFieldRules.h
 * @brief 手続き的に生やす芝の層ごとの密度・LOD・距離の規則
 * @details GrassFieldCommon.hlsli と同じ式を使う。葉はワールドに固定したタイル内に
 *          低食い違い列で並び、LOD k のタイルは先頭から (LOD0 の本数 >> k) 本だけ描く。
 *          全密度を保つ距離 r0 より遠くでは、目標の密度を (r0 / 距離)^2 で下げる。
 */

#include "../../core/GraphicsPreset.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace game::systems {

/** @brief 芝の層 */
enum class GrassLayer : uint32_t {
  Rough = 0, /**< ラフ・セミラフ（丈のある葉） */
  Turf = 1,  /**< フェアウェイ・グリーン（刈り込んだ短い葉） */
};

inline constexpr uint32_t kGrassLayerCount = 2;
inline constexpr uint32_t kGrassMaxLodCount = 8;
/** @brief フィールドテクスチャ（芝の種類と生えてよい度合い）の1テクセルの大きさ[m] */
inline constexpr float kGrassFieldCellSize = 0.4f;
/** @brief 1本のシュート（茎）から出る葉の枚数 */
inline constexpr uint32_t kGrassLeavesPerShoot = 3;
/** @brief 遠くで葉幅を広げて被覆を補うときの上限倍率 */
inline constexpr float kGrassMaxWidthScale = 6.0f;

/** @brief 層ごとの描画パラメータ */
struct GrassLayerParams {
  bool enabled = false;
  float tileSize = 1.0f;            /**< タイルの一辺[m] */
  float baseDensity = 0.0f;         /**< LOD0 の密度[葉/m^2] */
  float fullDensityDistance = 1.0f; /**< 全密度を保つ距離 r0[m] */
  float maxDistance = 0.0f;         /**< 葉が消えきる距離[m] */
  float fadeWidth = 1.0f;           /**< maxDistance の手前で消えていく幅[m] */
  float maxBladeHeight = 0.3f;      /**< 判定用の葉の最大の高さ[m] */
};

/**
 * @brief プリセットと層から描画パラメータを返します。
 * @details Low は芝を描かない。見える距離は従来の芝と同じにする。
 */
inline GrassLayerParams GetGrassLayerParams(core::GraphicsPreset preset,
                                            GrassLayer layer) {
  GrassLayerParams params;
  if (preset == core::GraphicsPreset::Low ||
      preset == core::GraphicsPreset::Auto ||
      preset == core::GraphicsPreset::Custom) {
    return params;
  }
  params.enabled = true;
  if (layer == GrassLayer::Rough) {
    params.tileSize = 2.0f;
    params.fadeWidth = 7.0f;
    params.maxBladeHeight = 0.32f;
    switch (preset) {
    case core::GraphicsPreset::Medium:
      params.baseDensity = 900.0f;
      params.fullDensityDistance = 6.0f;
      params.maxDistance = 35.0f;
      break;
    case core::GraphicsPreset::High:
      params.baseDensity = 1800.0f;
      params.fullDensityDistance = 8.0f;
      params.maxDistance = 40.0f;
      break;
    case core::GraphicsPreset::ExHigh:
      params.baseDensity = 2600.0f;
      params.fullDensityDistance = 10.0f;
      params.maxDistance = 44.0f;
      break;
    case core::GraphicsPreset::Ultra:
    default:
      params.baseDensity = 4000.0f;
      params.fullDensityDistance = 12.0f;
      params.maxDistance = 52.0f;
      break;
    }
  } else {
    params.tileSize = 1.0f;
    params.fadeWidth = 4.0f;
    params.maxBladeHeight = 0.06f;
    switch (preset) {
    case core::GraphicsPreset::Medium:
      params.baseDensity = 4000.0f;
      params.fullDensityDistance = 3.0f;
      params.maxDistance = 19.0f;
      break;
    case core::GraphicsPreset::High:
      params.baseDensity = 10000.0f;
      params.fullDensityDistance = 4.0f;
      params.maxDistance = 21.0f;
      break;
    case core::GraphicsPreset::ExHigh:
      params.baseDensity = 16000.0f;
      params.fullDensityDistance = 5.5f;
      params.maxDistance = 21.0f;
      break;
    case core::GraphicsPreset::Ultra:
    default:
      params.baseDensity = 20000.0f;
      params.fullDensityDistance = 7.0f;
      params.maxDistance = 21.0f;
      break;
    }
  }
  return params;
}

/** @brief LOD0 で1タイルに生やす葉の枚数（シュート単位に切り上げる） */
inline uint32_t GrassBaseBladesPerTile(const GrassLayerParams &params) {
  const float blades = params.baseDensity * params.tileSize * params.tileSize;
  const uint32_t shoots = static_cast<uint32_t>(
      std::ceil(std::max(blades, 0.0f) / kGrassLeavesPerShoot));
  return shoots * kGrassLeavesPerShoot;
}

/** @brief LOD k のタイルで描く葉の枚数（先頭からの枚数、シュート単位） */
inline uint32_t GrassBladesForLod(const GrassLayerParams &params, uint32_t lod) {
  const uint32_t shoots = GrassBaseBladesPerTile(params) / kGrassLeavesPerShoot;
  return std::max<uint32_t>(1u, shoots >> lod) * kGrassLeavesPerShoot;
}

/** @brief 層で使う LOD の段数（最大距離で必要な段まで） */
inline uint32_t GrassLodCount(const GrassLayerParams &params) {
  if (!params.enabled || params.fullDensityDistance <= 0.0f) {
    return 0;
  }
  const float ratio =
      std::max(params.maxDistance / params.fullDensityDistance, 1.0f);
  const uint32_t lastLod =
      static_cast<uint32_t>(std::floor(2.0f * std::log2(ratio)));
  return std::min(lastLod + 1u, kGrassMaxLodCount);
}

/**
 * @brief 距離に対する目標密度の割合（1 = LOD0 の密度）
 * @param fullDensityDistance 負荷による倍率を掛けた r0
 */
inline float GrassDensityFraction(float distance, float fullDensityDistance) {
  if (distance <= fullDensityDistance) {
    return 1.0f;
  }
  const float ratio = fullDensityDistance / distance;
  return ratio * ratio;
}

/**
 * @brief タイル内でカメラに最も近い点までの距離から LOD を選びます。
 * @details 2^k <= (d / r0)^2 となる最大の k。LOD k は目標密度以上の葉を必ず含む。
 */
inline uint32_t GrassLodForDistance(float nearestDistance,
                                    float fullDensityDistance,
                                    uint32_t lodCount) {
  if (lodCount == 0 || nearestDistance <= fullDensityDistance) {
    return 0;
  }
  const float ratio = nearestDistance / fullDensityDistance;
  const int lod = static_cast<int>(std::floor(2.0f * std::log2(ratio)));
  return static_cast<uint32_t>(
      std::clamp(lod, 0, static_cast<int>(lodCount) - 1));
}

/** @brief LOD ごとの1枚の頂点数（近いほど曲線の分割を増やす） */
inline uint32_t GrassVerticesPerBlade(uint32_t lod) {
  if (lod == 0) {
    return 7;
  }
  if (lod == 1) {
    return 5;
  }
  return 3;
}

/** @brief LOD ごとの1枚のインデックス数 */
inline uint32_t GrassIndicesPerBlade(uint32_t lod) {
  return (GrassVerticesPerBlade(lod) - 2u) * 3u;
}

/**
 * @brief 層と LOD から、葉の形の段（GrassVerticesPerBlade に渡す 0〜2）を返します。
 * @details 短い芝は数 cm しかなく曲がりが見えないので、どの距離でも三角形1枚にする。
 */
inline uint32_t GrassShapeLod(GrassLayer layer, uint32_t lod) {
  if (layer == GrassLayer::Turf) {
    return 2;
  }
  return std::min<uint32_t>(lod, 2);
}

/**
 * @brief 遠くで本数が減った分の葉幅の倍率（GrassFieldVS.hlsl と同じ）
 * @details 1/√t では被覆が t^0.5 で減り遠景が地面だけになるため、t^-0.75 まで広げる。
 */
inline float GrassWidthScaleForDensity(float densityFraction) {
  return std::clamp(std::pow(std::max(densityFraction, 1.0e-4f), -0.75f), 1.0f,
                    kGrassMaxWidthScale);
}

/** @brief GPU 負荷の倍率（0〜1）を r0 の倍率へ変える。本数はおおむね r0^2 に比例する */
inline float GrassDistanceScaleForBudget(float budgetScale) {
  return std::sqrt(std::clamp(budgetScale, 0.0f, 1.0f));
}

/** @brief GPUフレーム時間から芝の負荷倍率を自動調整する状態 */
struct GrassGpuLoadController {
  float smoothedGpuMs = 0.0f;
  /** 1 で通常、小さいほど全密度の範囲を狭める */
  float budgetScale = 1.0f;
};

/** @brief 自動調整の目標値 */
struct GrassGpuLoadTarget {
  /** 60fps維持のため、CPU処理とPresentの余裕を残したGPU時間の目標 */
  float targetGpuMs = 14.0f;
  float minBudgetScale = 0.15f;
};

/**
 * @brief 直近のGPUフレーム時間で芝の負荷倍率を更新します。
 *        目標を超えたら素早く下げ、十分な余裕が続いたときだけゆっくり戻す。
 *        VRAM退避などによる一時的な極端な値は目標の2倍で頭打ちにする。
 * @param gpuFrameMs 0以下なら未計測として何もしない
 */
inline void UpdateGrassGpuLoad(GrassGpuLoadController &controller,
                               float gpuFrameMs,
                               const GrassGpuLoadTarget &target = {}) {
  if (gpuFrameMs <= 0.0f || target.targetGpuMs <= 0.0f) {
    return;
  }
  const float sample = std::min(gpuFrameMs, target.targetGpuMs * 2.0f);
  controller.smoothedGpuMs =
      controller.smoothedGpuMs <= 0.0f
          ? sample
          : controller.smoothedGpuMs * 0.85f + sample * 0.15f;
  if (controller.smoothedGpuMs > target.targetGpuMs) {
    controller.budgetScale =
        std::max(target.minBudgetScale, controller.budgetScale * 0.96f);
  } else if (controller.smoothedGpuMs < target.targetGpuMs * 0.8f) {
    controller.budgetScale = std::min(1.0f, controller.budgetScale + 0.004f);
  }
}

} // namespace game::systems
