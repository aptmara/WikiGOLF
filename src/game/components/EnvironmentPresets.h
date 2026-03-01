/**
 * @file EnvironmentPresets.h
 * @brief テーマ別環境プリセット -
 * スカイボックステーマから環境パラメータを自動設定
 */

#pragma once

#include "../../graphics/SkyboxTextureGenerator.h"
#include "EnvironmentState.h"


namespace game::components {

/**
 * @brief スカイボックステーマから環境プリセットを取得
 * @param theme スカイボックステーマ
 * @return 環境状態の初期設定
 */
inline EnvironmentState GetEnvironmentPreset(graphics::SkyboxTheme theme) {
  EnvironmentState env;

  switch (theme) {
  // === 自然系 ===
  case graphics::SkyboxTheme::SpaceAstronomy:
    env.lightingMood = LightingMood::StarlitCosmos;
    env.particlePreset = ParticlePreset::Stars;
    env.particleDensity = 0.8f;
    env.fogDensity = 0.0f;
    env.timeOfDay = 0.0f; // 真夜中
    env.ambientColor = {0.05f, 0.05f, 0.15f};
    env.saturation = 1.2f;
    break;

  case graphics::SkyboxTheme::Ocean:
    env.lightingMood = LightingMood::UnderwaterCaustics;
    env.particlePreset = ParticlePreset::Bubbles;
    env.particleDensity = 0.5f;
    env.fogColor = {0.1f, 0.3f, 0.5f};
    env.fogDensity = 0.4f;
    env.fogStart = 20.0f;
    env.fogEnd = 100.0f;
    env.colorTint = {0.8f, 0.95f, 1.0f};
    break;

  case graphics::SkyboxTheme::Forest:
    env.lightingMood = LightingMood::GoldenAfternoon;
    env.particlePreset = ParticlePreset::Pollen;
    env.particleDensity = 0.4f;
    env.fogColor = {0.4f, 0.5f, 0.3f};
    env.fogDensity = 0.15f;
    env.timeOfDay = 14.0f;
    env.colorTint = {0.95f, 1.0f, 0.9f};
    break;

  case graphics::SkyboxTheme::Desert:
    env.lightingMood = LightingMood::GoldenAfternoon;
    env.particlePreset = ParticlePreset::Dust;
    env.particleDensity = 0.6f;
    env.sunIntensity = 1.3f;
    env.timeOfDay = 13.0f;
    env.colorTint = {1.0f, 0.95f, 0.85f};
    env.saturation = 0.9f;
    break;

  case graphics::SkyboxTheme::Mountain:
    env.lightingMood = LightingMood::SunriseDawn;
    env.particlePreset = ParticlePreset::None;
    env.fogColor = {0.7f, 0.75f, 0.9f};
    env.fogDensity = 0.2f;
    env.fogStart = 50.0f;
    env.fogEnd = 300.0f;
    env.timeOfDay = 7.0f;
    break;

  case graphics::SkyboxTheme::Polar:
    env.lightingMood = LightingMood::AuroralDance;
    env.particlePreset = ParticlePreset::Snow;
    env.particleDensity = 0.7f;
    env.fogColor = {0.85f, 0.9f, 1.0f};
    env.fogDensity = 0.25f;
    env.colorTint = {0.9f, 0.95f, 1.0f};
    env.timeOfDay = 22.0f;
    break;

  case graphics::SkyboxTheme::Volcano:
    env.lightingMood = LightingMood::VolcanicGlow;
    env.particlePreset = ParticlePreset::Embers;
    env.particleDensity = 0.8f;
    env.fogColor = {0.3f, 0.15f, 0.1f};
    env.fogDensity = 0.5f;
    env.colorTint = {1.0f, 0.85f, 0.7f};
    env.brightness = 0.9f;
    break;

  // === 歴史・文化系 ===
  case graphics::SkyboxTheme::HistoryAncient:
    env.lightingMood = LightingMood::CandlelitAntique;
    env.particlePreset = ParticlePreset::Dust;
    env.particleDensity = 0.3f;
    env.colorTint = {1.0f, 0.95f, 0.85f};
    env.saturation = 0.8f;
    env.timeOfDay = 16.0f;
    break;

  case graphics::SkyboxTheme::Medieval:
    env.lightingMood = LightingMood::FoggyMystery;
    env.particlePreset = ParticlePreset::Smoke;
    env.particleDensity = 0.25f;
    env.fogColor = {0.5f, 0.5f, 0.55f};
    env.fogDensity = 0.35f;
    env.timeOfDay = 9.0f;
    break;

  case graphics::SkyboxTheme::Urban:
    env.lightingMood = LightingMood::StudioNeutral;
    env.particlePreset = ParticlePreset::Dust;
    env.particleDensity = 0.2f;
    env.fogColor = {0.6f, 0.6f, 0.65f};
    env.fogDensity = 0.15f;
    env.timeOfDay = 12.0f;
    break;

  // === 特殊系 ===
  case graphics::SkyboxTheme::Horror:
    env.lightingMood = LightingMood::StormyDarkness;
    env.particlePreset = ParticlePreset::Ash;
    env.particleDensity = 0.4f;
    env.fogColor = {0.2f, 0.2f, 0.25f};
    env.fogDensity = 0.6f;
    env.brightness = 0.6f;
    env.saturation = 0.7f;
    env.timeOfDay = 2.0f;
    break;

  case graphics::SkyboxTheme::Fantasy:
    env.lightingMood = LightingMood::AuroralDance;
    env.particlePreset = ParticlePreset::Sparkles;
    env.particleDensity = 0.5f;
    env.colorTint = {0.95f, 0.9f, 1.0f};
    env.saturation = 1.1f;
    env.timeOfDay = 19.0f;
    break;

  case graphics::SkyboxTheme::SciFi:
    env.lightingMood = LightingMood::NeonCyberpunk;
    env.particlePreset = ParticlePreset::NebulaGas;
    env.particleDensity = 0.3f;
    env.colorTint = {0.9f, 0.95f, 1.0f};
    env.brightness = 1.1f;
    env.timeOfDay = 21.0f;
    break;

  case graphics::SkyboxTheme::War:
    env.lightingMood = LightingMood::StormyDarkness;
    env.particlePreset = ParticlePreset::Smoke;
    env.particleDensity = 0.7f;
    env.fogColor = {0.35f, 0.3f, 0.25f};
    env.fogDensity = 0.45f;
    env.brightness = 0.8f;
    env.timeOfDay = 17.0f;
    break;

  case graphics::SkyboxTheme::Sunset:
    env.lightingMood = LightingMood::DramaticSunset;
    env.particlePreset = ParticlePreset::Dust;
    env.particleDensity = 0.2f;
    env.colorTint = {1.0f, 0.9f, 0.85f};
    env.saturation = 1.15f;
    env.timeOfDay = 18.5f;
    break;

  case graphics::SkyboxTheme::Religion:
    env.lightingMood = LightingMood::CandlelitAntique;
    env.particlePreset = ParticlePreset::Sparkles;
    env.particleDensity = 0.15f;
    env.colorTint = {1.0f, 0.98f, 0.9f};
    env.brightness = 0.95f;
    env.timeOfDay = 15.0f;
    break;

  case graphics::SkyboxTheme::Art:
    env.lightingMood = LightingMood::StudioNeutral;
    env.particlePreset = ParticlePreset::None;
    env.saturation = 1.05f;
    env.brightness = 1.0f;
    env.timeOfDay = 11.0f;
    break;

  case graphics::SkyboxTheme::Music:
    env.lightingMood = LightingMood::NeonCyberpunk;
    env.particlePreset = ParticlePreset::Sparkles;
    env.particleDensity = 0.4f;
    env.colorTint = {0.95f, 0.9f, 1.0f};
    env.timeOfDay = 22.0f;
    break;

  case graphics::SkyboxTheme::Sports:
    env.lightingMood = LightingMood::GoldenAfternoon;
    env.particlePreset = ParticlePreset::None;
    env.sunIntensity = 1.2f;
    env.saturation = 1.1f;
    env.timeOfDay = 15.0f;
    break;

  case graphics::SkyboxTheme::Food:
    env.lightingMood = LightingMood::CandlelitAntique;
    env.particlePreset = ParticlePreset::None;
    env.colorTint = {1.0f, 0.98f, 0.95f};
    env.saturation = 1.05f;
    env.timeOfDay = 19.0f;
    break;

  case graphics::SkyboxTheme::Medical:
    env.lightingMood = LightingMood::StudioNeutral;
    env.particlePreset = ParticlePreset::None;
    env.colorTint = {0.95f, 0.98f, 1.0f};
    env.brightness = 1.1f;
    env.timeOfDay = 10.0f;
    break;

  case graphics::SkyboxTheme::Literature:
    env.lightingMood = LightingMood::CandlelitAntique;
    env.particlePreset = ParticlePreset::Dust;
    env.particleDensity = 0.15f;
    env.colorTint = {1.0f, 0.97f, 0.92f};
    env.timeOfDay = 20.0f;
    break;

  case graphics::SkyboxTheme::ScienceTech:
    env.lightingMood = LightingMood::StudioNeutral;
    env.particlePreset = ParticlePreset::None;
    env.colorTint = {0.98f, 0.98f, 1.0f};
    env.brightness = 1.05f;
    env.timeOfDay = 11.0f;
    break;

  case graphics::SkyboxTheme::Retro:
    env.lightingMood = LightingMood::GoldenAfternoon;
    env.particlePreset = ParticlePreset::Dust;
    env.particleDensity = 0.25f;
    env.colorTint = {1.0f, 0.95f, 0.85f};
    env.saturation = 0.85f;
    env.contrast = 1.1f;
    env.timeOfDay = 15.0f;
    break;

  default:
    // デフォルト: 穏やかな午後
    env.lightingMood = LightingMood::GoldenAfternoon;
    env.particlePreset = ParticlePreset::Dust;
    env.particleDensity = 0.2f;
    env.timeOfDay = 14.0f;
    break;
  }

  // ライティングパラメータを適用
  GetLightingForMood(env.lightingMood, env.sunColor, env.ambientColor,
                     env.sunIntensity);

  return env;
}

} // namespace game::components
