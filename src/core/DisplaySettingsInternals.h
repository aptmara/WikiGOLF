#pragma once

/**
 * @file DisplaySettingsInternals.h
 * @brief DisplaySettings の分割実装で共有する内部ヘルパー。
 */

#include "DisplaySettings.h"

#include <cmath>
#include <limits>
#include <string>

namespace core::display_settings_detail {

constexpr int kMinResolutionWidth = 1024;
constexpr int kMinResolutionHeight = 576;

inline constexpr std::pair<int, int> kResolutionPresets[] = {
    {1280, 720},
    {1920, 1080},
    {2560, 1440},
};

inline constexpr int kFpsLimitPresets[] = {0, 30, 60, 120, 144};
inline constexpr int kMsaaPresets[] = {1, 2, 4, 8};
inline constexpr float kRenderScalePresets[] = {0.5f, 0.6f, 0.7f,
                                                 0.8f, 0.9f, 1.0f};

std::string WideToUtf8(const std::wstring &value);
std::wstring Utf8ToWide(const std::string &value);
void TrimInPlace(std::string &value);
bool ParseBool(const std::string &value, bool defaultValue);
GraphicsPreset ParseGraphicsPreset(const std::string &value);
const char *GraphicsPresetToString(GraphicsPreset preset);
const char *WindowModeToString(WindowMode mode);

template <typename T, size_t N>
size_t FindClosestIndex(const T (&options)[N], T value) {
  size_t bestIndex = 0;
  double bestDiff = std::numeric_limits<double>::max();
  for (size_t i = 0; i < N; ++i) {
    const double diff =
        std::abs(static_cast<double>(options[i]) - static_cast<double>(value));
    if (diff < bestDiff) {
      bestDiff = diff;
      bestIndex = i;
    }
  }
  return bestIndex;
}

template <typename T, size_t N>
T StepOption(const T (&options)[N], T current, int direction) {
  const size_t count = N;
  size_t index = FindClosestIndex(options, current);
  if (direction >= 0) {
    index = (index + 1) % count;
  } else {
    index = (index + count - 1) % count;
  }
  return options[index];
}

size_t AdvanceIndex(size_t index, size_t count, int direction);
int BoolToInt(bool value);

} // namespace core::display_settings_detail
