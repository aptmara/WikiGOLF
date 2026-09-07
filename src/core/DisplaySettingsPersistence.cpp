/**
 * @file DisplaySettingsPersistence.cpp
 * @brief 表示設定のテキスト変換と永続化。
*/

#include "DisplaySettings.h"
#include "DisplaySettingsInternals.h"
#include "Logger.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>

namespace core::display_settings_detail {

std::string WideToUtf8(const std::wstring &value) {
  if (value.empty()) {
    return {};
  }
  const int required = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1,
                                           nullptr, 0, nullptr, nullptr);
  if (required <= 1) {
    return {};
  }
  std::string result(static_cast<size_t>(required), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), required,
                      nullptr, nullptr);
  result.pop_back();
  return result;
}

std::wstring Utf8ToWide(const std::string &value) {
  if (value.empty()) {
    return {};
  }
  const int required =
      MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
  if (required <= 1) {
    return {};
  }
  std::wstring result(static_cast<size_t>(required), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, result.data(), required);
  result.pop_back();
  return result;
}

void TrimInPlace(std::string &value) {
  while (!value.empty() &&
         (value.back() == '\r' || value.back() == '\n' ||
          value.back() == ' ' || value.back() == '\t')) {
    value.pop_back();
  }
  size_t start = 0;
  while (start < value.size() &&
         (value[start] == ' ' || value[start] == '\t')) {
    ++start;
  }
  if (start > 0) {
    value.erase(0, start);
  }
}

bool ParseBool(const std::string &value, bool defaultValue) {
  if (value == "1" || value == "true" || value == "True" ||
      value == "TRUE") {
    return true;
  }
  if (value == "0" || value == "false" || value == "False" ||
      value == "FALSE") {
    return false;
  }
  return defaultValue;
}

GraphicsPreset ParseGraphicsPreset(const std::string &value) {
  if (value == "LOW") {
    return GraphicsPreset::Low;
  }
  if (value == "MEDIUM") {
    return GraphicsPreset::Medium;
  }
  if (value == "HIGH") {
    return GraphicsPreset::High;
  }
  if (value == "EXHIGH") {
    return GraphicsPreset::ExHigh;
  }
  if (value == "ULTRA") {
    return GraphicsPreset::Ultra;
  }
  if (value == "CUSTOM") {
    return GraphicsPreset::Custom;
  }
  return GraphicsPreset::Auto;
}

const char *GraphicsPresetToString(GraphicsPreset preset) {
  switch (preset) {
  case GraphicsPreset::Low:
    return "LOW";
  case GraphicsPreset::Medium:
    return "MEDIUM";
  case GraphicsPreset::High:
    return "HIGH";
  case GraphicsPreset::ExHigh:
    return "EXHIGH";
  case GraphicsPreset::Ultra:
    return "ULTRA";
  case GraphicsPreset::Custom:
    return "CUSTOM";
  case GraphicsPreset::Auto:
  default:
    return "AUTO";
  }
}

const char *WindowModeToString(WindowMode mode) {
  switch (mode) {
  case WindowMode::Borderless:
    return "Borderless";
  case WindowMode::Fullscreen:
    return "Fullscreen";
  case WindowMode::Windowed:
  default:
    return "Windowed";
  }
}

size_t AdvanceIndex(size_t index, size_t count, int direction) {
  if (direction >= 0) {
    return (index + 1) % count;
  }
  return (index + count - 1) % count;
}

int BoolToInt(bool value) {
  if (value) {
    return 1;
  }
  return 0;
}

} // namespace core::display_settings_detail

namespace core {

void DisplaySettings::LoadFromFile(const std::string &path) {
  m_data = DisplaySettingsData{};

  std::ifstream file(path);
  if (!file.is_open()) {
    LOG_INFO("DisplaySettings",
             "設定ファイルが見つからないため既定値を使用します: {}", path);
    return;
  }

  std::string line;
  while (std::getline(file, line)) {
    const auto eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    std::string key = line.substr(0, eq);
    std::string value = line.substr(eq + 1);
    display_settings_detail::TrimInPlace(key);
    display_settings_detail::TrimInPlace(value);

    if (key == "WindowMode") {
      if (value == "Borderless") {
        m_data.mode = WindowMode::Borderless;
      } else if (value == "Fullscreen") {
        m_data.mode = WindowMode::Fullscreen;
      } else {
        m_data.mode = WindowMode::Windowed;
      }
    } else if (key == "Width") {
      m_data.windowedWidth = std::max(
          display_settings_detail::kMinResolutionWidth, std::atoi(value.c_str()));
    } else if (key == "Height") {
      m_data.windowedHeight = std::max(
          display_settings_detail::kMinResolutionHeight, std::atoi(value.c_str()));
    } else if (key == "RenderScale") {
      m_data.renderScale = std::clamp(
          static_cast<float>(std::atof(value.c_str())), 0.5f, 1.0f);
    } else if (key == "GraphicsPreset") {
      m_data.graphicsPreset = display_settings_detail::ParseGraphicsPreset(value);
    } else if (key == "VSync") {
      m_data.vsync = display_settings_detail::ParseBool(value, true);
    } else if (key == "FpsLimit") {
      m_data.fpsLimit = std::max(0, std::atoi(value.c_str()));
    } else if (key == "FXAA") {
      m_data.fxaaEnabled = display_settings_detail::ParseBool(value, false);
    } else if (key == "MSAA") {
      m_data.msaaSamples = std::max(1, std::atoi(value.c_str()));
    } else if (key == "TAA") {
      m_data.taaEnabled = display_settings_detail::ParseBool(value, false);
    } else if (key == "ShowFps") {
      m_data.showFps = display_settings_detail::ParseBool(value, false);
    } else if (key == "GPU") {
      m_data.gpuAdapterName = value;
    }
  }

  const int screenWidth = GetSystemMetrics(SM_CXSCREEN);
  const int screenHeight = GetSystemMetrics(SM_CYSCREEN);
  if (screenWidth > 0) {
    m_data.windowedWidth = std::min(m_data.windowedWidth, screenWidth);
  }
  if (screenHeight > 0) {
    m_data.windowedHeight = std::min(m_data.windowedHeight, screenHeight);
  }

  LOG_INFO("DisplaySettings",
           "設定を読み込みました: mode={} {}x{} scale={:.2f} vsync={} fps={} "
           "fxaa={} msaa={}x taa={}",
           display_settings_detail::WindowModeToString(m_data.mode),
           m_data.windowedWidth, m_data.windowedHeight, m_data.renderScale,
           m_data.vsync, m_data.fpsLimit, m_data.fxaaEnabled,
           m_data.msaaSamples, m_data.taaEnabled);
}

void DisplaySettings::SaveToFile(const std::string &path) const {
  std::ofstream file(path, std::ios::trunc);
  if (!file.is_open()) {
    LOG_WARN("DisplaySettings", "設定ファイルを書き込めませんでした: {}", path);
    return;
  }

  file << "WindowMode="
       << display_settings_detail::WindowModeToString(m_data.mode) << "\n";
  file << "Width=" << m_data.windowedWidth << "\n";
  file << "Height=" << m_data.windowedHeight << "\n";
  file << "GraphicsPreset="
       << display_settings_detail::GraphicsPresetToString(m_data.graphicsPreset)
       << "\n";
  file << "RenderScale=" << m_data.renderScale << "\n";
  file << "VSync=" << display_settings_detail::BoolToInt(m_data.vsync) << "\n";
  file << "FpsLimit=" << m_data.fpsLimit << "\n";
  file << "FXAA=" << display_settings_detail::BoolToInt(m_data.fxaaEnabled)
       << "\n";
  file << "MSAA=" << m_data.msaaSamples << "\n";
  file << "TAA=" << display_settings_detail::BoolToInt(m_data.taaEnabled)
       << "\n";
  file << "ShowFps=" << display_settings_detail::BoolToInt(m_data.showFps)
       << "\n";
  file << "GPU=" << m_data.gpuAdapterName << "\n";
}

std::wstring DisplaySettings::GetGpuAdapterNameWide() const {
  return display_settings_detail::Utf8ToWide(m_data.gpuAdapterName);
}

} // namespace core
