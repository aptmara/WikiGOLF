/**
 * @file test_display_settings_rules.cpp
 * @brief DisplaySettings の設定値と永続化の現状挙動を検証します。
 */

#include "src/core/DisplaySettings.h"

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
  file << "Height=9999\n";
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
  assert(data.windowedHeight == 9999);
  assert(data.graphicsPreset == core::GraphicsPreset::High);
  assert(data.renderScale == 1.0f);
  assert(!data.vsync);
  assert(data.fpsLimit == 0);
  assert(data.fxaaEnabled);
  assert(data.msaaSamples == 4);
  assert(data.taaEnabled);
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

  settings.SetMsaaSamples(4);
  settings.CycleMsaa(1);
  assert(settings.GetData().msaaSamples == 8);

  settings.SetFpsLimit(60);
  settings.CycleFpsLimit(-1);
  assert(settings.GetData().fpsLimit == 30);
}

} // namespace

int main() {
  VerifyLoadedValues();
  VerifyRoundTrip();
  VerifyCyclesWithoutInitialization();
  std::remove(kSettingsPath.c_str());
  return 0;
}
