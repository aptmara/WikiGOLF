/**
 * @file test_anti_aliasing_mode.cpp
 * @brief アンチエイリアス方式の排他選択ルールとDLSS画質モードの対応を検証します。
 */

#include "src/core/AntiAliasingMode.h"

#include <cassert>

int main() {
  using core::AntiAliasingMode;
  using core::DlssQuality;

  // 旧設定の複数同時有効は MSAA > FXAA > DLSS > TAA の順で1つに決まる
  static_assert(core::ResolveAntiAliasingMode(false, 1, false) ==
                AntiAliasingMode::Off);
  static_assert(core::ResolveAntiAliasingMode(true, 1, true) ==
                AntiAliasingMode::Fxaa);
  static_assert(core::ResolveAntiAliasingMode(false, 1, true) ==
                AntiAliasingMode::Taa);
  static_assert(core::ResolveAntiAliasingMode(false, 1, true, true) ==
                AntiAliasingMode::Dlss);
  static_assert(core::ResolveAntiAliasingMode(true, 1, false, true) ==
                AntiAliasingMode::Fxaa);
  static_assert(core::ResolveAntiAliasingMode(true, 2, true, true) ==
                AntiAliasingMode::Msaa2);
  static_assert(core::ResolveAntiAliasingMode(false, 4, false) ==
                AntiAliasingMode::Msaa4);
  static_assert(core::ResolveAntiAliasingMode(false, 16, false) ==
                AntiAliasingMode::Msaa8);

  // 展開したフラグは常に1方式だけが有効
  for (int i = 0; i <= static_cast<int>(AntiAliasingMode::Msaa8); ++i) {
    const auto mode = static_cast<AntiAliasingMode>(i);
    const auto flags = core::GetAntiAliasingFlags(mode);
    const int enabledCount = (flags.fxaaEnabled ? 1 : 0) +
                             (flags.msaaSamples > 1 ? 1 : 0) +
                             (flags.taaEnabled ? 1 : 0) +
                             (flags.dlssEnabled ? 1 : 0);
    assert(enabledCount == (mode == AntiAliasingMode::Off ? 0 : 1));
    assert(core::ResolveAntiAliasingMode(flags.fxaaEnabled, flags.msaaSamples,
                                         flags.taaEnabled, flags.dlssEnabled) ==
           mode);
  }

  // ◀▶は端で循環する。DLSSは使える環境でだけ候補になる
  static_assert(core::StepAntiAliasingMode(AntiAliasingMode::Off, 1) ==
                AntiAliasingMode::Fxaa);
  static_assert(core::StepAntiAliasingMode(AntiAliasingMode::Fxaa, 1) ==
                AntiAliasingMode::Taa);
  static_assert(core::StepAntiAliasingMode(AntiAliasingMode::Taa, 1, true) ==
                AntiAliasingMode::Dlss);
  static_assert(core::StepAntiAliasingMode(AntiAliasingMode::Taa, 1, false) ==
                AntiAliasingMode::Msaa2);
  static_assert(core::StepAntiAliasingMode(AntiAliasingMode::Msaa2, -1, false) ==
                AntiAliasingMode::Taa);
  static_assert(core::StepAntiAliasingMode(AntiAliasingMode::Msaa8, 1) ==
                AntiAliasingMode::Off);
  static_assert(core::StepAntiAliasingMode(AntiAliasingMode::Off, -1) ==
                AntiAliasingMode::Msaa8);

  static_assert(core::IsTemporalAntiAliasing(AntiAliasingMode::Taa));
  static_assert(core::IsTemporalAntiAliasing(AntiAliasingMode::Dlss));
  static_assert(!core::IsTemporalAntiAliasing(AntiAliasingMode::Msaa4));

  // DLSS画質モードは巡回用の倍率とそれぞれ一致する
  for (int i = 0; i < 4; ++i) {
    assert(core::DlssQualityFromRenderScale(core::kDlssRenderScalePresets[i]) ==
           static_cast<DlssQuality>(i));
  }
  static_assert(core::DlssQualityFromRenderScale(0.9f) == DlssQuality::Dlaa);
  static_assert(core::DlssQualityFromRenderScale(0.7f) == DlssQuality::Quality);
  static_assert(core::DlssQualityFromRenderScale(0.6f) == DlssQuality::Balanced);
  static_assert(core::DlssQualityFromRenderScale(0.5f) == DlssQuality::Performance);
  return 0;
}
