#pragma once

#include <cstdint>

namespace core {

enum class GraphicsPreset {
  Auto,
  Low,
  Medium,
  High,
  ExHigh,
  Ultra,
  Custom,
};

struct GraphicsPresetSettings {
  float renderScale;
  int msaaSamples;
  bool fxaaEnabled;
};

constexpr GraphicsPresetSettings GetGraphicsPresetSettings(
    GraphicsPreset preset) {
  switch (preset) {
  case GraphicsPreset::Low:
    return {0.60f, 1, true};
  case GraphicsPreset::Medium:
    return {0.80f, 1, true};
  case GraphicsPreset::High:
    return {1.0f, 1, false};
  case GraphicsPreset::ExHigh:
    return {1.0f, 4, false};
  case GraphicsPreset::Ultra:
    return {1.0f, 8, false};
  case GraphicsPreset::Auto:
  case GraphicsPreset::Custom:
  default:
    return {1.0f, 1, false};
  }
}

constexpr GraphicsPreset InferGraphicsPresetFromVideoMemory(
    uint64_t dedicatedVideoMemoryBytes) {
  constexpr uint64_t kGiB = 1024ull * 1024ull * 1024ull;
  if (dedicatedVideoMemoryBytes <= 1ull * kGiB) {
    return GraphicsPreset::Low;
  }
  if (dedicatedVideoMemoryBytes <= 2ull * kGiB) {
    return GraphicsPreset::Medium;
  }
  if (dedicatedVideoMemoryBytes <= 4ull * kGiB) {
    return GraphicsPreset::High;
  }
  return GraphicsPreset::ExHigh;
}

constexpr GraphicsPreset InferCustomVegetationPreset(float renderScale,
                                                      int msaaSamples) {
  if (renderScale <= 0.70f) {
    return GraphicsPreset::Low;
  }
  if (renderScale <= 0.85f) {
    return GraphicsPreset::Medium;
  }
  if (msaaSamples >= 4) {
    if (msaaSamples >= 8) {
      return GraphicsPreset::Ultra;
    }
    return GraphicsPreset::ExHigh;
  }
  return GraphicsPreset::High;
}

} // namespace core
