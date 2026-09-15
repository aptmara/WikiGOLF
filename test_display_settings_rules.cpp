/**
 * @file test_display_settings_rules.cpp
 * @brief DisplaySettings の設定値と永続化の現状挙動を検証します。
 */

#include "src/core/DisplaySettings.h"
#include "src/core/DisplaySettingsInternals.h"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>

namespace {

const std::string kSettingsPath = "display_settings_rules_test.ini";

void WriteInputFile() {
  std::ofstream file(kSettingsPath, std::ios::trunc);
  assert(file.is_open());
  file << "WindowMode=Borderless\n";
  file << "Width=640\n";
  file << "Height=720\n";
  file << "GraphicsPreset=HIGH\n";
  file << "RenderScale=2.0\n";
  file << "VSync=0\n";
  file << "FpsLimit=-20\n";
  file << "FXAA=1\n";
  file << "MSAA=4\n";
  file << "TAA=true\n";
  file << "ShowFps=1\n";
  file << "GPU=Test Adapter\n";
}

void VerifyLoadedValues() {
  WriteInputFile();

  core::DisplaySettings settings;
  settings.LoadFromFile(kSettingsPath);
  const auto &data = settings.GetData();

  assert(data.mode == core::WindowMode::Borderless);
  assert(data.windowedWidth == 1024);
  assert(data.windowedHeight == 720);
  assert(data.graphicsPreset == core::GraphicsPreset::High);
  assert(data.renderScale == 1.0f);
  assert(!data.vsync);
  assert(data.fpsLimit == 0);
  // FXAA/MSAA/TAAが同時に有効な旧設定は、MSAAを優先して1方式へ正規化される
  assert(!data.fxaaEnabled);
  assert(data.msaaSamples == 4);
  assert(!data.taaEnabled);
  assert(settings.GetAntiAliasingMode() == core::AntiAliasingMode::Msaa4);
  assert(data.showFps);
  assert(data.gpuAdapterName == "Test Adapter");
}

void VerifyRoundTrip() {
  core::DisplaySettings source;
  source.LoadFromFile(kSettingsPath);
  source.SaveToFile(kSettingsPath);

  core::DisplaySettings loaded;
  loaded.LoadFromFile(kSettingsPath);
  const auto &data = loaded.GetData();
  assert(data.mode == core::WindowMode::Borderless);
  assert(data.graphicsPreset == core::GraphicsPreset::High);
  assert(data.renderScale == 1.0f);
  assert(data.gpuAdapterName == "Test Adapter");
}

void VerifyCyclesWithoutInitialization() {
  core::DisplaySettings settings;
  settings.SetRenderScale(0.5f);
  assert(settings.GetData().renderScale == 0.5f);

  settings.CycleRenderScale(1);
  assert(settings.GetData().renderScale == 0.6f);

  settings.SetAntiAliasingMode(core::AntiAliasingMode::Msaa4);
  settings.CycleAntiAliasing(1);
  assert(settings.GetData().msaaSamples == 8);

  // TAAへ切り替えるとMSAA/FXAAは無効になる
  settings.SetAntiAliasingMode(core::AntiAliasingMode::Taa);
  assert(settings.GetData().taaEnabled);
  assert(settings.GetData().msaaSamples == 1);
  assert(!settings.GetData().fxaaEnabled);
  settings.CycleAntiAliasing(-1);
  assert(settings.GetAntiAliasingMode() == core::AntiAliasingMode::Fxaa);
  assert(!settings.GetData().taaEnabled);

  // GraphicsDevice未登録ではDLSSは使えないため、TAAの次はMSAAになる
  assert(!settings.IsDlssAvailable());
  settings.SetAntiAliasingMode(core::AntiAliasingMode::Taa);
  settings.CycleAntiAliasing(1);
  assert(settings.GetAntiAliasingMode() == core::AntiAliasingMode::Msaa2);

  // DLSSを選ぶと描画解像度は画質モードの倍率へ揃い、◀▶もその4段階を巡回する
  settings.SetRenderScale(0.8f);
  settings.SetAntiAliasingMode(core::AntiAliasingMode::Dlss);
  assert(settings.GetData().dlssEnabled);
  assert(settings.GetData().msaaSamples == 1);
  assert(settings.GetData().renderScale == 0.67f);
  settings.CycleRenderScale(1);
  assert(settings.GetData().renderScale == 1.0f);
  settings.CycleRenderScale(1);
  assert(settings.GetData().renderScale == 0.5f);

  settings.SetFpsLimit(60);
  settings.CycleFpsLimit(-1);
  assert(settings.GetData().fpsLimit == 30);
}

void VerifyResolutionFitKeepsAspectRatio() {
  const auto fitted =
      core::display_settings_detail::FitResolutionWithinBounds(1920, 1080,
                                                                1440, 1080);
  assert(fitted.first == 1440);
  assert(fitted.second == 810);

  const auto unchanged =
      core::display_settings_detail::FitResolutionWithinBounds(1280, 720,
                                                                1440, 1080);
  assert(unchanged.first == 1280);
  assert(unchanged.second == 720);
}

} // namespace

int main() {
  VerifyLoadedValues();
  VerifyRoundTrip();
  VerifyCyclesWithoutInitialization();
  VerifyResolutionFitKeepsAspectRatio();
  std::remove(kSettingsPath.c_str());
  return 0;
}
