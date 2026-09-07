/**
 * @file DisplaySettings.cpp
 * @brief 表示設定の初期化処理。
 */

#include "DisplaySettings.h"
#include "DisplaySettingsInternals.h"
#include "../graphics/GraphicsDevice.h"

namespace core {

void DisplaySettings::Initialize(HWND hwnd,
                                 graphics::GraphicsDevice *graphicsDevice) {
  m_hwnd = hwnd;
  m_graphics = graphicsDevice;
  m_resolutions = EnumerateResolutions();

  m_gpuNames.clear();
  for (const auto &adapter : graphics::GraphicsDevice::EnumerateAdapters()) {
    m_gpuNames.push_back(display_settings_detail::WideToUtf8(adapter.name));
  }

  ApplySelectedGraphicsPreset();
  ApplyQualityToGraphics();
  if (m_graphics) {
    m_graphics->SetVSync(m_data.vsync);
  }

  RefreshCurrentResolution();
}

} // namespace core
