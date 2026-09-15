#pragma once
/**
 * @file GrassRenderRules.h
 * @brief 芝描画の距離フェードと、画面内ポリゴン上限に収める縮退計算
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace game::systems {

/**
 * @brief GrassVS.hlslと同じ判定で、ラフ／セミラフ扱いの度合いを返します。
 * @param materialClass インスタンスColor.a（0=グリーン寄り、1=ラフ寄り）
 */
inline float CalculateGrassRoughClass(float materialClass) {
  const float t =
      std::clamp((std::abs(materialClass - 0.73f) - 0.15f) / 0.10f, 0.0f,
                 1.0f);
  return 1.0f - t * t * (3.0f - 2.0f * t);
}

/**
 * @brief GrassVS.hlslのディザーフェードが完了する距離を返します。
 * @param materialClass インスタンスColor.a
 * @param roughFadeScale ラフ系だけに掛けるプリセット別の距離倍率
 * @param drawScale 描画上限による全体の距離倍率
 */
inline float CalculateGrassShaderFadeEnd(float materialClass,
                                         float roughFadeScale,
                                         float drawScale = 1.0f) {
  const float roughClass = CalculateGrassRoughClass(materialClass);
  const float baseFadeEnd = 14.0f + std::clamp(materialClass, 0.0f, 1.0f) * 50.0f;
  const float classScale = 1.0f + (roughFadeScale - 1.0f) * roughClass;
  return baseFadeEnd * classScale * drawScale;
}

/** @brief GPUフレーム時間から芝のポリゴン上限倍率を自動調整する状態 */
struct GrassGpuLoadController {
  float smoothedGpuMs = 0.0f;
  /** プリセットのポリゴン上限に掛ける倍率 */
  float budgetScale = 1.0f;
};

/** @brief 自動調整の目標値 */
struct GrassGpuLoadTarget {
  /** 60fps維持のため、CPU処理とPresentの余裕を残したGPU時間の目標 */
  float targetGpuMs = 14.0f;
  float minBudgetScale = 0.15f;
};

/**
 * @brief 直近のGPUフレーム時間で芝のポリゴン上限倍率を更新します。
 *        目標を超えたら素早く下げ、十分な余裕が続いたときだけゆっくり戻す。
 *        VRAM退避などによる一時的な極端な値は目標の2倍で頭打ちにし、
 *        数フレームの外れ値で芝が消え切らないようにする。
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

/** @brief 描画上限の計算に使う芝インスタンス1個分の情報 */
struct GrassBudgetSample {
  float distance = 0.0f;
  /** 0以下ならLODなし */
  float lodSwitchDistance = 0.0f;
  float maxDrawDistance = 0.0f;
  uint32_t nearTriangles = 0;
  uint32_t lodTriangles = 0;
};

/** @brief 描画上限へ収めるための距離倍率 */
struct GrassBudgetScales {
  /** LOD切り替え距離の倍率。小さいほど近くから低密度メッシュになる */
  float lodScale = 1.0f;
  /** 描画距離の倍率。小さいほど遠くの芝から消える */
  float drawScale = 1.0f;
  uint64_t estimatedTriangles = 0;
};

/** @brief 描画上限の縮退段階ごとの下限 */
struct GrassBudgetLimits {
  /** 最初に密度だけを落とすときのLOD倍率の下限 */
  float firstLodScale = 0.4f;
  /** 遠くから削るときの描画距離倍率の下限 */
  float minDrawScale = 0.5f;
};

/**
 * @brief 画面内の芝ポリゴン数が上限に収まる距離倍率を求めます。
 *
 * 1. LOD切り替え距離を firstLodScale まで縮めて中距離の密度を落とす
 * 2. 描画距離を minDrawScale まで縮めて遠くの芝から消す
 * 3. それでも超える場合はLOD切り替え距離を0まで縮める
 *
 * 各段階は距離比のヒストグラムを遠い側から削るため、インスタンス数に
 * 対して線形時間で求まる。上限0は無制限を表す。
 */
inline GrassBudgetScales SolveGrassDrawBudget(
    const std::vector<GrassBudgetSample> &samples, uint64_t triangleBudget,
    const GrassBudgetLimits &limits = {}) {
  constexpr int kBins = 64;
  GrassBudgetScales result;

  const auto binOf = [](float ratio) {
    return std::clamp(static_cast<int>(ratio * kBins), 0, kBins - 1);
  };
  const auto lodRatio = [](const GrassBudgetSample &sample) {
    return sample.lodSwitchDistance > 0.0f
               ? sample.distance / sample.lodSwitchDistance
               : 0.0f;
  };
  const auto drawRatio = [](const GrassBudgetSample &sample) {
    return sample.maxDrawDistance > 0.0f
               ? sample.distance / sample.maxDrawDistance
               : 0.0f;
  };
  const auto sampleCost = [&](const GrassBudgetSample &sample, float lodScale,
                              float drawScale) -> uint64_t {
    if (drawRatio(sample) >= drawScale) {
      return 0;
    }
    const bool usesNear = sample.lodSwitchDistance <= 0.0f ||
                          lodRatio(sample) < lodScale;
    return usesNear ? sample.nearTriangles : sample.lodTriangles;
  };
  const auto totalCost = [&](float lodScale, float drawScale) {
    uint64_t total = 0;
    for (const auto &sample : samples) {
      total += sampleCost(sample, lodScale, drawScale);
    }
    return total;
  };

  // 遠いビンから順に削減量を差し引き、上限へ収まった位置の倍率を返す。
  const auto sweep = [&](std::array<uint64_t, kBins> &gains, uint64_t &cost,
                         float currentScale, float minScale) {
    const int floorBin =
        std::clamp(static_cast<int>(std::ceil(minScale * kBins)), 0, kBins);
    const int startBin =
        std::clamp(static_cast<int>(std::ceil(currentScale * kBins)) - 1, -1,
                   kBins - 1);
    float scale = currentScale;
    for (int bin = startBin; bin >= floorBin && cost > triangleBudget; --bin) {
      cost -= std::min(cost, gains[bin]);
      scale = static_cast<float>(bin) / kBins;
    }
    return std::min(scale, currentScale);
  };

  uint64_t cost = totalCost(result.lodScale, result.drawScale);
  if (triangleBudget == 0 || cost <= triangleBudget) {
    result.estimatedTriangles = cost;
    return result;
  }

  const auto lodGains = [&](float lodScale, float drawScale) {
    std::array<uint64_t, kBins> gains{};
    for (const auto &sample : samples) {
      if (sample.lodSwitchDistance <= 0.0f ||
          drawRatio(sample) >= drawScale || lodRatio(sample) >= lodScale ||
          sample.nearTriangles <= sample.lodTriangles) {
        continue;
      }
      gains[binOf(lodRatio(sample))] +=
          sample.nearTriangles - sample.lodTriangles;
    }
    return gains;
  };

  // 段階1: 中距離の密度を落とす
  auto gains = lodGains(result.lodScale, result.drawScale);
  result.lodScale = sweep(gains, cost, result.lodScale, limits.firstLodScale);

  // 段階2: 遠くの芝から消す
  if (cost > triangleBudget) {
    std::array<uint64_t, kBins> drawGains{};
    for (const auto &sample : samples) {
      const float ratio = drawRatio(sample);
      if (ratio >= 1.0f) {
        continue;
      }
      drawGains[binOf(ratio)] += sampleCost(sample, result.lodScale, 1.0f);
    }
    result.drawScale =
        sweep(drawGains, cost, result.drawScale, limits.minDrawScale);
  }

  // 段階3: 近距離まで低密度メッシュにする
  if (cost > triangleBudget) {
    gains = lodGains(result.lodScale, result.drawScale);
    result.lodScale = sweep(gains, cost, result.lodScale, 0.0f);
  }

  result.estimatedTriangles = totalCost(result.lodScale, result.drawScale);
  return result;
}

} // namespace game::systems
