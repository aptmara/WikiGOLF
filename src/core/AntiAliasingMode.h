#pragma once
/**
 * @file AntiAliasingMode.h
 * @brief アンチエイリアス方式（OFF/FXAA/TAA/DLSS/MSAA）の排他選択ルール
 * @details 設定ファイルには FXAA/MSAA/TAA/DLSS の個別キーで保存するが、
 *          同時に有効にすると効果が重複・競合するため、UI上は1項目の
 *          排他選択として扱う。
*/

#include <cstddef>

namespace core {

/** @brief 設定画面で選べるアンチエイリアス方式（◀▶の並び順）*/
enum class AntiAliasingMode {
  Off,
  Fxaa,
  Taa,
  Dlss,
  Msaa2,
  Msaa4,
  Msaa8,
};

/** @brief DisplaySettingsData / QualitySettings が持つ個別フラグ表現*/
struct AntiAliasingFlags {
  bool fxaaEnabled = false;
  int msaaSamples = 1;
  bool taaEnabled = false;
  bool dlssEnabled = false;
};

/**
 * @brief 個別フラグから方式を1つに決める。
 * @details 旧設定ファイルでは複数が同時に有効なことがあるため、以前から
 *          描画に反映されていたMSAA、FXAAを優先し、DLSS、TAAの順に採用する。
*/
constexpr AntiAliasingMode ResolveAntiAliasingMode(bool fxaaEnabled,
                                                   int msaaSamples,
                                                   bool taaEnabled,
                                                   bool dlssEnabled = false) {
  if (msaaSamples >= 8) {
    return AntiAliasingMode::Msaa8;
  }
  if (msaaSamples >= 4) {
    return AntiAliasingMode::Msaa4;
  }
  if (msaaSamples >= 2) {
    return AntiAliasingMode::Msaa2;
  }
  if (fxaaEnabled) {
    return AntiAliasingMode::Fxaa;
  }
  if (dlssEnabled) {
    return AntiAliasingMode::Dlss;
  }
  if (taaEnabled) {
    return AntiAliasingMode::Taa;
  }
  return AntiAliasingMode::Off;
}

/** @brief 方式を個別フラグへ展開する（該当方式以外はすべて無効）*/
constexpr AntiAliasingFlags GetAntiAliasingFlags(AntiAliasingMode mode) {
  AntiAliasingFlags flags;
  switch (mode) {
  case AntiAliasingMode::Fxaa:
    flags.fxaaEnabled = true;
    break;
  case AntiAliasingMode::Taa:
    flags.taaEnabled = true;
    break;
  case AntiAliasingMode::Dlss:
    flags.dlssEnabled = true;
    break;
  case AntiAliasingMode::Msaa2:
    flags.msaaSamples = 2;
    break;
  case AntiAliasingMode::Msaa4:
    flags.msaaSamples = 4;
    break;
  case AntiAliasingMode::Msaa8:
    flags.msaaSamples = 8;
    break;
  case AntiAliasingMode::Off:
  default:
    break;
  }
  return flags;
}

/**
 * @brief 設定画面の◀▶用。端で反対側へ循環する。
 * @param dlssAvailable falseならDLSSを飛ばす（非NVIDIA環境などでは選ばせない）
*/
constexpr AntiAliasingMode StepAntiAliasingMode(AntiAliasingMode mode,
                                                int direction,
                                                bool dlssAvailable = false) {
  constexpr size_t kCount = static_cast<size_t>(AntiAliasingMode::Msaa8) + 1;
  size_t index = static_cast<size_t>(mode) % kCount;
  for (size_t attempt = 0; attempt < kCount; ++attempt) {
    if (direction >= 0) {
      index = (index + 1) % kCount;
    } else {
      index = (index + kCount - 1) % kCount;
    }
    if (dlssAvailable ||
        static_cast<AntiAliasingMode>(index) != AntiAliasingMode::Dlss) {
      break;
    }
  }
  return static_cast<AntiAliasingMode>(index);
}

/** @brief 時間方向に蓄積する方式（ジッター・速度バッファを使う）か*/
constexpr bool IsTemporalAntiAliasing(AntiAliasingMode mode) {
  return mode == AntiAliasingMode::Taa || mode == AntiAliasingMode::Dlss;
}

/** @brief DLSSの画質モード（描画解像度の倍率から決める）*/
enum class DlssQuality {
  Performance, ///< 描画解像度 50%
  Balanced,    ///< 約58%
  Quality,     ///< 約67%
  Dlaa,        ///< 100%（アップスケールせずAAのみ）
};

/** @brief DLSS選択中に「描画解像度」の◀▶で巡回する倍率（DlssQualityの順）*/
inline constexpr float kDlssRenderScalePresets[] = {0.5f, 0.58f, 0.67f, 1.0f};

/** @brief 描画解像度の倍率に最も近いDLSS画質モード*/
constexpr DlssQuality DlssQualityFromRenderScale(float renderScale) {
  if (renderScale >= 0.835f) {
    return DlssQuality::Dlaa;
  }
  if (renderScale >= 0.625f) {
    return DlssQuality::Quality;
  }
  if (renderScale >= 0.54f) {
    return DlssQuality::Balanced;
  }
  return DlssQuality::Performance;
}

} // namespace core
