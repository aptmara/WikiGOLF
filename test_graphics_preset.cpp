#include "src/core/GraphicsPreset.h"
#include <cassert>

int main() {
  using core::GraphicsPreset;

  constexpr uint64_t gib = 1024ull * 1024ull * 1024ull;
  static_assert(core::InferGraphicsPresetFromVideoMemory(484ull * 1024ull *
                                                         1024ull) ==
                GraphicsPreset::Low);
  static_assert(core::InferGraphicsPresetFromVideoMemory(1ull * gib) ==
                GraphicsPreset::Low);
  static_assert(core::InferGraphicsPresetFromVideoMemory(2ull * gib) ==
                GraphicsPreset::Medium);
  static_assert(core::InferGraphicsPresetFromVideoMemory(4ull * gib) ==
                GraphicsPreset::High);
  static_assert(core::InferGraphicsPresetFromVideoMemory(8ull * gib) ==
                GraphicsPreset::ExHigh);
  static_assert(core::InferGraphicsPresetFromVideoMemory(12ull * gib) ==
                GraphicsPreset::ExHigh);
  // 16GB 表記の GPU が報告する、16GiB をわずかに下回る値
  static_assert(core::InferGraphicsPresetFromVideoMemory(16376ull * 1024ull *
                                                         1024ull) ==
                GraphicsPreset::Ultra);
  static_assert(core::InferGraphicsPresetFromVideoMemory(16ull * gib) ==
                GraphicsPreset::Ultra);
  static_assert(core::InferGraphicsPresetFromVideoMemory(24ull * gib) ==
                GraphicsPreset::Ultra);

  const auto low = core::GetGraphicsPresetSettings(GraphicsPreset::Low);
  assert(low.renderScale == 0.60f);
  assert(low.msaaSamples == 1);
  assert(low.fxaaEnabled);

  const auto exHigh =
      core::GetGraphicsPresetSettings(GraphicsPreset::ExHigh);
  assert(exHigh.renderScale == 1.0f);
  assert(exHigh.msaaSamples == 4);
  assert(!exHigh.fxaaEnabled);

  const auto ultra = core::GetGraphicsPresetSettings(GraphicsPreset::Ultra);
  assert(ultra.renderScale == 1.0f);
  assert(ultra.msaaSamples == 8);
  assert(!ultra.fxaaEnabled);

  assert(core::InferCustomVegetationPreset(0.60f, 1) ==
         GraphicsPreset::Low);
  assert(core::InferCustomVegetationPreset(0.8f, 1) ==
         GraphicsPreset::Medium);
  assert(core::InferCustomVegetationPreset(1.0f, 1) ==
         GraphicsPreset::High);
  assert(core::InferCustomVegetationPreset(1.0f, 4) ==
         GraphicsPreset::ExHigh);
  assert(core::InferCustomVegetationPreset(1.0f, 8) ==
         GraphicsPreset::Ultra);
  return 0;
}
