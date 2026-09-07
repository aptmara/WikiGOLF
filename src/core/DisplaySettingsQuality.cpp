/**
 * @file DisplaySettingsQuality.cpp
 * @brief 画質プリセット、描画品質、FPS/GPU 設定の適用。
 */

#include "DisplaySettings.h"
#include "DisplaySettingsInternals.h"
#include "Logger.h"
#include "../graphics/GraphicsDevice.h"

#include <algorithm>
#include <iterator>

namespace core {

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
    uint64_t videoMemory = 0;
    if (m_graphics) {
      videoMemory = m_graphics->GetDedicatedVideoMemoryBytes();
    }
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

  double videoMemoryMiB = 0.0;
  if (m_graphics) {
    videoMemoryMiB = static_cast<double>(
                         m_graphics->GetDedicatedVideoMemoryBytes()) /
                     (1024.0 * 1024.0);
  }
  LOG_INFO("DisplaySettings",
           "Graphics preset selected={} effective={} DedicatedVRAM={:.0f}MB "
           "scale={:.2f} MSAA={}x FXAA={} VSync={} FPSLimit={}",
           display_settings_detail::GraphicsPresetToString(m_data.graphicsPreset),
           display_settings_detail::GraphicsPresetToString(
               m_effectiveGraphicsPreset),
           videoMemoryMiB, m_data.renderScale, m_data.msaaSamples,
           m_data.fxaaEnabled, m_data.vsync, m_data.fpsLimit);
}

void DisplaySettings::MarkGraphicsPresetCustom() {
  m_data.graphicsPreset = GraphicsPreset::Custom;
  m_effectiveGraphicsPreset = InferCustomVegetationPreset(
      m_data.renderScale, m_data.msaaSamples);
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
  SetRenderScale(display_settings_detail::StepOption(
      display_settings_detail::kRenderScalePresets, m_data.renderScale,
      direction));
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

  index = display_settings_detail::AdvanceIndex(
      index, std::size(kPresets), direction);
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
  SetFpsLimit(display_settings_detail::StepOption(
      display_settings_detail::kFpsLimitPresets, m_data.fpsLimit, direction));
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
  SetMsaaSamples(display_settings_detail::StepOption(
      display_settings_detail::kMsaaPresets, m_data.msaaSamples, direction));
}

void DisplaySettings::SetTaaEnabled(bool enabled) {
  if (m_data.taaEnabled == enabled) {
    return;
  }
  m_data.taaEnabled = enabled;
  // TAA は設定値だけ保存し、未実装の描画処理へは反映しない。
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
  // GPU 切り替えは次回起動時のデバイス再生成で反映する。
  SaveToFile();
}

void DisplaySettings::CycleGpu(int direction) {
  std::vector<std::string> options;
  options.reserve(m_gpuNames.size() + 1);
  options.push_back("");
  options.insert(options.end(), m_gpuNames.begin(), m_gpuNames.end());

  if (options.size() <= 1) {
    return;
  }

  size_t index = 0;
  for (size_t i = 0; i < options.size(); ++i) {
    if (options[i] == m_data.gpuAdapterName) {
      index = i;
      break;
    }
  }

  index = display_settings_detail::AdvanceIndex(index, options.size(),
                                                 direction);
  SetGpuAdapter(options[index]);
}

} // namespace core
