/**
 * @file DisplaySettingsWindow.cpp
 * @brief ウィンドウモード、解像度、モニタ領域の適用。
*/

#include "DisplaySettings.h"
#include "DisplaySettingsInternals.h"
#include "Logger.h"
#include "../graphics/GraphicsDevice.h"

#include <algorithm>
#include <utility>

namespace core {

std::vector<std::pair<int, int>> DisplaySettings::EnumerateResolutions() {
  const int screenWidth = GetSystemMetrics(SM_CXSCREEN);
  const int screenHeight = GetSystemMetrics(SM_CYSCREEN);

  std::vector<std::pair<int, int>> result;
  for (const auto &preset : display_settings_detail::kResolutionPresets) {
    if (screenWidth > 0 && preset.first > screenWidth) {
      continue;
    }
    if (screenHeight > 0 && preset.second > screenHeight) {
      continue;
    }
    result.push_back(preset);
  }
  if (result.empty()) {
    result.push_back(display_settings_detail::kResolutionPresets[0]);
  }
  return result;
}

RECT DisplaySettings::GetMonitorRect(bool workAreaOnly) const {
  HMONITOR monitor = nullptr;
  if (m_hwnd) {
    monitor = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTOPRIMARY);
  }

  MONITORINFO info = {};
  info.cbSize = sizeof(info);
  if (monitor && GetMonitorInfoW(monitor, &info)) {
    if (workAreaOnly) {
      return info.rcWork;
    }
    return info.rcMonitor;
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
    return;
  }

  m_currentWidth = m_data.windowedWidth;
  m_currentHeight = m_data.windowedHeight;
}

void DisplaySettings::ApplyToWindow() {
  if (!m_hwnd) {
    return;
  }

  if (m_graphics && m_graphics->IsFullscreenExclusive() &&
      m_data.mode != WindowMode::Fullscreen) {
    m_graphics->SetFullscreenExclusive(false, 0, 0);
  }

  switch (m_data.mode) {
  case WindowMode::Fullscreen:
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
  constexpr size_t count = std::size(kOrder);
  size_t index = 0;
  for (size_t i = 0; i < count; ++i) {
    if (kOrder[i] == m_data.mode) {
      index = i;
      break;
    }
  }
  index = display_settings_detail::AdvanceIndex(index, count, direction);
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

  const auto it = std::find(m_resolutions.begin(), m_resolutions.end(),
                            std::make_pair(m_data.windowedWidth,
                                           m_data.windowedHeight));
  size_t index = 0;
  if (it != m_resolutions.end()) {
    index = static_cast<size_t>(std::distance(m_resolutions.begin(), it));
  }

  index = display_settings_detail::AdvanceIndex(index, m_resolutions.size(),
                                                 direction);
  SetResolution(m_resolutions[index].first, m_resolutions[index].second);
}

} // namespace core
