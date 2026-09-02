/**
 * @file DisplaySettings.cpp
 * @brief 表示・画質設定の管理と永続化の実装
 */

#include "DisplaySettings.h"
#include "Logger.h"
#include "../graphics/GraphicsDevice.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>

namespace core {

namespace {
constexpr int kMinResolutionWidth = 1024;
constexpr int kMinResolutionHeight = 576;

// 解像度プリセット（要求仕様: 1280x720 / 1920x1080 / 2560x1440）
constexpr std::pair<int, int> kResolutionPresets[] = {
    {1280, 720},
    {1920, 1080},
    {2560, 1440},
};

// FPS上限プリセット（0 = 無制限）
constexpr int kFpsLimitPresets[] = {0, 30, 60, 120, 144};

// MSAAサンプル数プリセット（1 = オフ）
constexpr int kMsaaPresets[] = {1, 2, 4, 8};

// Render Scaleプリセット（50%〜100%）
constexpr float kRenderScalePresets[] = {0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 1.0f};

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
  result.pop_back(); // 末尾のNUL終端分を除く
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

void TrimInPlace(std::string &s) {
  while (!s.empty() &&
         (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' ||
          s.back() == '\t')) {
    s.pop_back();
  }
  size_t start = 0;
  while (start < s.size() &&
         (s[start] == ' ' || s[start] == '\t')) {
    ++start;
  }
  if (start > 0) {
    s.erase(0, start);
  }
}

bool ParseBool(const std::string &value, bool defaultValue) {
  if (value == "1" || value == "true" || value == "True" || value == "TRUE") {
    return true;
  }
  if (value == "0" || value == "false" || value == "False" || value == "FALSE") {
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

template <typename T, size_t N>
size_t FindClosestIndex(const T (&options)[N], T value) {
  size_t bestIndex = 0;
  double bestDiff = std::numeric_limits<double>::max();
  for (size_t i = 0; i < N; ++i) {
    const double diff = std::abs(static_cast<double>(options[i]) - static_cast<double>(value));
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

} // namespace

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
    TrimInPlace(key);
    TrimInPlace(value);

    if (key == "WindowMode") {
      if (value == "Borderless") {
        m_data.mode = WindowMode::Borderless;
      } else if (value == "Fullscreen") {
        m_data.mode = WindowMode::Fullscreen;
      } else {
        m_data.mode = WindowMode::Windowed;
      }
    } else if (key == "Width") {
      m_data.windowedWidth =
          std::max(kMinResolutionWidth, std::atoi(value.c_str()));
    } else if (key == "Height") {
      m_data.windowedHeight =
          std::max(kMinResolutionHeight, std::atoi(value.c_str()));
    } else if (key == "RenderScale") {
      m_data.renderScale = std::clamp(static_cast<float>(std::atof(value.c_str())), 0.5f, 1.0f);
    } else if (key == "GraphicsPreset") {
      m_data.graphicsPreset = ParseGraphicsPreset(value);
    } else if (key == "VSync") {
      m_data.vsync = ParseBool(value, true);
    } else if (key == "FpsLimit") {
      m_data.fpsLimit = std::max(0, std::atoi(value.c_str()));
    } else if (key == "FXAA") {
      m_data.fxaaEnabled = ParseBool(value, false);
    } else if (key == "MSAA") {
      m_data.msaaSamples = std::max(1, std::atoi(value.c_str()));
    } else if (key == "TAA") {
      m_data.taaEnabled = ParseBool(value, false);
    } else if (key == "ShowFps") {
      m_data.showFps = ParseBool(value, false);
    } else if (key == "GPU") {
      m_data.gpuAdapterName = value;
    }
  }

  // 別モニタ環境で保存された解像度がこのPCの画面より大きい場合に、
  // 初回起動時に画面からはみ出た巨大ウィンドウが作られるのを防ぐ
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
           m_data.mode == WindowMode::Borderless   ? "Borderless"
           : m_data.mode == WindowMode::Fullscreen ? "Fullscreen"
                                                    : "Windowed",
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
  const char *modeStr = m_data.mode == WindowMode::Borderless   ? "Borderless"
                       : m_data.mode == WindowMode::Fullscreen ? "Fullscreen"
                                                                : "Windowed";
  file << "WindowMode=" << modeStr << "\n";
  file << "Width=" << m_data.windowedWidth << "\n";
  file << "Height=" << m_data.windowedHeight << "\n";
  file << "GraphicsPreset=" << GraphicsPresetToString(m_data.graphicsPreset)
       << "\n";
  file << "RenderScale=" << m_data.renderScale << "\n";
  file << "VSync=" << (m_data.vsync ? 1 : 0) << "\n";
  file << "FpsLimit=" << m_data.fpsLimit << "\n";
  file << "FXAA=" << (m_data.fxaaEnabled ? 1 : 0) << "\n";
  file << "MSAA=" << m_data.msaaSamples << "\n";
  file << "TAA=" << (m_data.taaEnabled ? 1 : 0) << "\n";
  file << "ShowFps=" << (m_data.showFps ? 1 : 0) << "\n";
  file << "GPU=" << m_data.gpuAdapterName << "\n";
}

std::wstring DisplaySettings::GetGpuAdapterNameWide() const {
  return Utf8ToWide(m_data.gpuAdapterName);
}

void DisplaySettings::Initialize(HWND hwnd, graphics::GraphicsDevice *graphicsDevice) {
  m_hwnd = hwnd;
  m_graphics = graphicsDevice;
  m_resolutions = EnumerateResolutions();

  m_gpuNames.clear();
  for (const auto &adapter : graphics::GraphicsDevice::EnumerateAdapters()) {
    m_gpuNames.push_back(WideToUtf8(adapter.name));
  }

  ApplySelectedGraphicsPreset();

  // 読み込み済みの画質設定・VSyncを、既に初期化済みのGraphicsDeviceへ反映する
  ApplyQualityToGraphics();
  if (m_graphics) {
    m_graphics->SetVSync(m_data.vsync);
  }

  RefreshCurrentResolution();
}

std::vector<std::pair<int, int>> DisplaySettings::EnumerateResolutions() {
  const int screenWidth = GetSystemMetrics(SM_CXSCREEN);
  const int screenHeight = GetSystemMetrics(SM_CYSCREEN);

  std::vector<std::pair<int, int>> result;
  for (const auto &preset : kResolutionPresets) {
    if (screenWidth > 0 && preset.first > screenWidth) {
      continue;
    }
    if (screenHeight > 0 && preset.second > screenHeight) {
      continue;
    }
    result.push_back(preset);
  }
  if (result.empty()) {
    result.push_back(kResolutionPresets[0]); // 最低限のフォールバック
  }
  return result;
}

RECT DisplaySettings::GetMonitorRect(bool workAreaOnly) const {
  HMONITOR monitor = m_hwnd ? MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTOPRIMARY)
                            : nullptr;
  MONITORINFO info = {};
  info.cbSize = sizeof(info);
  if (monitor && GetMonitorInfoW(monitor, &info)) {
    return workAreaOnly ? info.rcWork : info.rcMonitor;
  }
  RECT fallback = {0, 0, GetSystemMetrics(SM_CXSCREEN),
                   GetSystemMetrics(SM_CYSCREEN)};
  return fallback;
}

void DisplaySettings::RefreshCurrentResolution() {
  if (m_data.mode == WindowMode::Borderless) {
    const RECT monitorRect = GetMonitorRect(false);
    m_currentWidth = monitorRect.right - monitorRect.left;
    m_currentHeight = monitorRect.bottom - monitorRect.top;
  } else {
    // Windowed / Fullscreen ともに選択解像度がそのまま反映される
    m_currentWidth = m_data.windowedWidth;
    m_currentHeight = m_data.windowedHeight;
  }
}

void DisplaySettings::ApplyQualityToGraphics() {
  if (!m_graphics) {
    return;
  }
  graphics::QualitySettings quality;
  quality.renderScale = m_data.renderScale;
  quality.msaaSamples = m_data.msaaSamples;
  quality.fxaaEnabled = m_data.fxaaEnabled;
  m_graphics->ApplyQualitySettings(quality);
}

void DisplaySettings::ApplySelectedGraphicsPreset() {
  GraphicsPreset effective = m_data.graphicsPreset;
  if (effective == GraphicsPreset::Auto) {
    const uint64_t videoMemory =
        m_graphics ? m_graphics->GetDedicatedVideoMemoryBytes() : 0;
    effective = InferGraphicsPresetFromVideoMemory(videoMemory);
  } else if (effective == GraphicsPreset::Custom) {
    effective = InferCustomVegetationPreset(m_data.renderScale,
                                            m_data.msaaSamples);
  }

  m_effectiveGraphicsPreset = effective;
  if (m_data.graphicsPreset != GraphicsPreset::Custom) {
    const auto preset = GetGraphicsPresetSettings(effective);
    m_data.renderScale = preset.renderScale;
    m_data.msaaSamples = preset.msaaSamples;
    m_data.fxaaEnabled = preset.fxaaEnabled;
    if (effective == GraphicsPreset::Ultra) {
      m_data.vsync = true;
      m_data.fpsLimit = 0;
      if (m_graphics) {
        m_graphics->SetVSync(true);
      }
    }
  }

  const double videoMemoryMiB =
      m_graphics
          ? static_cast<double>(m_graphics->GetDedicatedVideoMemoryBytes()) /
                (1024.0 * 1024.0)
          : 0.0;
  LOG_INFO("DisplaySettings",
           "Graphics preset selected={} effective={} DedicatedVRAM={:.0f}MB "
           "scale={:.2f} MSAA={}x FXAA={} VSync={} FPSLimit={}",
           GraphicsPresetToString(m_data.graphicsPreset),
           GraphicsPresetToString(m_effectiveGraphicsPreset), videoMemoryMiB,
           m_data.renderScale, m_data.msaaSamples, m_data.fxaaEnabled,
           m_data.vsync, m_data.fpsLimit);
}

void DisplaySettings::MarkGraphicsPresetCustom() {
  m_data.graphicsPreset = GraphicsPreset::Custom;
  m_effectiveGraphicsPreset =
      InferCustomVegetationPreset(m_data.renderScale, m_data.msaaSamples);
}

void DisplaySettings::ApplyToWindow() {
  if (!m_hwnd) {
    return;
  }

  // 排他フルスクリーンから他モードへ抜ける場合は、スタイル変更の前に必ず抜けておく
  if (m_graphics && m_graphics->IsFullscreenExclusive() &&
      m_data.mode != WindowMode::Fullscreen) {
    m_graphics->SetFullscreenExclusive(false, 0, 0);
  }

  switch (m_data.mode) {
  case WindowMode::Fullscreen: {
    SetWindowLongPtrW(m_hwnd, GWL_STYLE,
                      static_cast<LONG_PTR>(kWindowedStyle | WS_VISIBLE));
    if (m_graphics) {
      m_graphics->SetFullscreenExclusive(
          true, static_cast<uint32_t>(m_data.windowedWidth),
          static_cast<uint32_t>(m_data.windowedHeight));
    }
    LOG_INFO("DisplaySettings", "排他フルスクリーンを適用しました ({}x{})",
             m_data.windowedWidth, m_data.windowedHeight);
    break;
  }
  case WindowMode::Borderless: {
    const RECT monitorRect = GetMonitorRect(false);
    SetWindowLongPtrW(m_hwnd, GWL_STYLE,
                      static_cast<LONG_PTR>(WS_POPUP | WS_VISIBLE));
    SetWindowPos(m_hwnd, HWND_TOP, monitorRect.left, monitorRect.top,
                monitorRect.right - monitorRect.left,
                monitorRect.bottom - monitorRect.top,
                SWP_FRAMECHANGED | SWP_NOACTIVATE);
    LOG_INFO("DisplaySettings", "ボーダーレスウィンドウを適用しました ({}x{})",
             monitorRect.right - monitorRect.left,
             monitorRect.bottom - monitorRect.top);
    break;
  }
  case WindowMode::Windowed:
  default: {
    RECT rect = {0, 0, m_data.windowedWidth, m_data.windowedHeight};
    AdjustWindowRect(&rect, kWindowedStyle, FALSE);
    const int windowWidth = rect.right - rect.left;
    const int windowHeight = rect.bottom - rect.top;

    const RECT workRect = GetMonitorRect(true);
    const int workWidth = workRect.right - workRect.left;
    const int workHeight = workRect.bottom - workRect.top;
    const int x = workRect.left + std::max(0, (workWidth - windowWidth) / 2);
    const int y = workRect.top + std::max(0, (workHeight - windowHeight) / 2);

    SetWindowLongPtrW(m_hwnd, GWL_STYLE,
                      static_cast<LONG_PTR>(kWindowedStyle | WS_VISIBLE));
    SetWindowPos(m_hwnd, HWND_TOP, x, y, windowWidth, windowHeight,
                SWP_FRAMECHANGED | SWP_NOACTIVATE);
    LOG_INFO("DisplaySettings", "ウィンドウモードを適用しました ({}x{})",
             m_data.windowedWidth, m_data.windowedHeight);
    break;
  }
  }

  RefreshCurrentResolution();
}

void DisplaySettings::SetWindowMode(WindowMode mode) {
  if (m_data.mode == mode) {
    return;
  }
  m_data.mode = mode;
  ApplyToWindow();
  SaveToFile();
}

void DisplaySettings::CycleWindowMode(int direction) {
  static constexpr WindowMode kOrder[] = {
      WindowMode::Windowed, WindowMode::Borderless, WindowMode::Fullscreen};
  constexpr int count = 3;
  int index = 0;
  for (int i = 0; i < count; ++i) {
    if (kOrder[i] == m_data.mode) {
      index = i;
      break;
    }
  }
  index = (index + count + (direction >= 0 ? 1 : -1)) % count;
  SetWindowMode(kOrder[index]);
}

void DisplaySettings::SetResolution(int width, int height) {
  if (m_data.windowedWidth == width && m_data.windowedHeight == height) {
    return;
  }
  m_data.windowedWidth = width;
  m_data.windowedHeight = height;
  if (m_data.mode != WindowMode::Borderless) {
    ApplyToWindow();
  }
  SaveToFile();
}

void DisplaySettings::CycleResolution(int direction) {
  if (m_resolutions.empty()) {
    return;
  }

  auto it = std::find(m_resolutions.begin(), m_resolutions.end(),
                      std::make_pair(m_data.windowedWidth, m_data.windowedHeight));
  size_t index = (it != m_resolutions.end())
                    ? static_cast<size_t>(std::distance(m_resolutions.begin(), it))
                    : 0;

  const size_t count = m_resolutions.size();
  if (direction >= 0) {
    index = (index + 1) % count;
  } else {
    index = (index + count - 1) % count;
  }

  SetResolution(m_resolutions[index].first, m_resolutions[index].second);
}

void DisplaySettings::SetRenderScale(float scale) {
  const float clamped = std::clamp(scale, 0.5f, 1.0f);
  if (std::abs(m_data.renderScale - clamped) < 0.001f) {
    return;
  }
  m_data.renderScale = clamped;
  MarkGraphicsPresetCustom();
  ApplyQualityToGraphics();
  SaveToFile();
}

void DisplaySettings::CycleRenderScale(int direction) {
  SetRenderScale(StepOption(kRenderScalePresets, m_data.renderScale, direction));
}

void DisplaySettings::SetGraphicsPreset(GraphicsPreset preset) {
  if (preset == GraphicsPreset::Custom) {
    return;
  }
  m_data.graphicsPreset = preset;
  ApplySelectedGraphicsPreset();
  ApplyQualityToGraphics();
  SaveToFile();
}

void DisplaySettings::CycleGraphicsPreset(int direction) {
  static constexpr GraphicsPreset kPresets[] = {
      GraphicsPreset::Auto, GraphicsPreset::Low, GraphicsPreset::Medium,
      GraphicsPreset::High, GraphicsPreset::ExHigh, GraphicsPreset::Ultra};
  size_t index = 0;
  bool found = false;
  for (size_t i = 0; i < std::size(kPresets); ++i) {
    if (kPresets[i] == m_data.graphicsPreset) {
      index = i;
      found = true;
      break;
    }
  }
  if (!found) {
    SetGraphicsPreset(GraphicsPreset::Auto);
    return;
  }
  const size_t count = std::size(kPresets);
  index = direction >= 0 ? (index + 1) % count
                         : (index + count - 1) % count;
  SetGraphicsPreset(kPresets[index]);
}

void DisplaySettings::SetVSync(bool enabled) {
  if (m_data.vsync == enabled) {
    return;
  }
  m_data.vsync = enabled;
  if (m_graphics) {
    m_graphics->SetVSync(enabled);
  }
  SaveToFile();
}

void DisplaySettings::SetFpsLimit(int fps) {
  const int clamped = std::max(0, fps);
  if (m_data.fpsLimit == clamped) {
    return;
  }
  m_data.fpsLimit = clamped;
  SaveToFile();
}

void DisplaySettings::CycleFpsLimit(int direction) {
  SetFpsLimit(StepOption(kFpsLimitPresets, m_data.fpsLimit, direction));
}

void DisplaySettings::SetFxaaEnabled(bool enabled) {
  if (m_data.fxaaEnabled == enabled) {
    return;
  }
  m_data.fxaaEnabled = enabled;
  MarkGraphicsPresetCustom();
  ApplyQualityToGraphics();
  SaveToFile();
}

void DisplaySettings::SetMsaaSamples(int samples) {
  if (samples != 1 && samples != 2 && samples != 4 && samples != 8) {
    samples = 1;
  }
  if (m_data.msaaSamples == samples) {
    return;
  }
  m_data.msaaSamples = samples;
  MarkGraphicsPresetCustom();
  ApplyQualityToGraphics();
  SaveToFile();
}

void DisplaySettings::CycleMsaa(int direction) {
  SetMsaaSamples(StepOption(kMsaaPresets, m_data.msaaSamples, direction));
}

void DisplaySettings::SetTaaEnabled(bool enabled) {
  if (m_data.taaEnabled == enabled) {
    return;
  }
  m_data.taaEnabled = enabled;
  // NOTE: TAAは前フレーム情報・モーションベクトル等の描画基盤が無いため未実装。
  // 設定値の保存のみ行い、描画には反映しない。
  SaveToFile();
}

void DisplaySettings::SetShowFps(bool enabled) {
  if (m_data.showFps == enabled) {
    return;
  }
  m_data.showFps = enabled;
  SaveToFile();
}

void DisplaySettings::SetGpuAdapter(const std::string &adapterName) {
  if (m_data.gpuAdapterName == adapterName) {
    return;
  }
  m_data.gpuAdapterName = adapterName;
  // GPU切り替えはデバイス（スワップチェーン・全リソース）の再生成が必要なため、
  // ここではウィンドウ/GraphicsDeviceには反映せず、次回起動時に適用する。
  SaveToFile();
}

void DisplaySettings::CycleGpu(int direction) {
  // 先頭は「自動」（空文字列）
  std::vector<std::string> options;
  options.reserve(m_gpuNames.size() + 1);
  options.push_back("");
  options.insert(options.end(), m_gpuNames.begin(), m_gpuNames.end());

  if (options.size() <= 1) {
    return; // 物理GPUが1つも列挙できない場合は切り替え不可
  }

  size_t index = 0;
  for (size_t i = 0; i < options.size(); ++i) {
    if (options[i] == m_data.gpuAdapterName) {
      index = i;
      break;
    }
  }

  const size_t count = options.size();
  index = (index + count + (direction >= 0 ? 1 : -1)) % count;
  SetGpuAdapter(options[index]);
}

} // namespace core
