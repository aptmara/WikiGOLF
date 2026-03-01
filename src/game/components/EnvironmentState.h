/**
 * @file EnvironmentState.h
 * @brief 環境状態コンポーネント - テーマに応じた環境パラメータを管理
 */

#pragma once

#include <DirectXMath.h>

namespace game::components {

/**
 * @brief ライティングムード - 環境光の雰囲気
 */
enum class LightingMood {
  // Natural
  SunriseDawn,
  GoldenAfternoon,
  DramaticSunset,
  MoonlitNight,
  StarlitCosmos,

  // Artificial
  NeonCyberpunk,
  CandlelitAntique,
  StudioNeutral,

  // Special
  UnderwaterCaustics,
  VolcanicGlow,
  AuroralDance,
  FoggyMystery,
  StormyDarkness,
};

/**
 * @brief パーティクルプリセット - 大気効果
 */
enum class ParticlePreset {
  None,
  Dust,
  Snow,
  Rain,
  Leaves,
  Fireflies,
  Embers,
  Bubbles,
  Stars,
  Pollen,
  Ash,
  Smoke,
  Sparkles,
  NebulaGas,
};

/**
 * @brief 環境状態 - テーマに応じた全環境パラメータ
 */
struct EnvironmentState {
  // === ライティング ===
  LightingMood lightingMood = LightingMood::GoldenAfternoon;
  DirectX::XMFLOAT3 sunDirection = {0.5f, -0.8f, 0.3f};
  DirectX::XMFLOAT3 sunColor = {1.0f, 0.95f, 0.9f};
  float sunIntensity = 1.0f;
  DirectX::XMFLOAT3 ambientColor = {0.3f, 0.35f, 0.4f};
  float ambientIntensity = 0.5f;

  // === 大気 ===
  DirectX::XMFLOAT3 fogColor = {0.7f, 0.75f, 0.8f};
  float fogDensity = 0.0f; // 0 = no fog, 1 = dense
  float fogStart = 100.0f;
  float fogEnd = 500.0f;

  // === 色調 ===
  DirectX::XMFLOAT3 colorTint = {1.0f, 1.0f, 1.0f};
  float saturation = 1.0f;
  float brightness = 1.0f;
  float contrast = 1.0f;

  // === パーティクル ===
  ParticlePreset particlePreset = ParticlePreset::Dust;
  float particleDensity = 0.3f;
  float particleSpeed = 1.0f;

  // === 時間 ===
  float timeOfDay = 12.0f; // 0.0-24.0 (12=noon)
  float timeScale = 0.0f;  // 0 = static, 1 = realtime

  // === トランジション ===
  float transitionProgress = 1.0f; // 0 = transitioning, 1 = complete
  EnvironmentState *transitionTarget = nullptr;
};

/**
 * @brief テーマ別ライティングパラメータを取得
 */
inline void GetLightingForMood(LightingMood mood,
                               DirectX::XMFLOAT3 &outSunColor,
                               DirectX::XMFLOAT3 &outAmbientColor,
                               float &outSunIntensity) {
  using namespace DirectX;

  switch (mood) {
  case LightingMood::SunriseDawn:
    outSunColor = {1.0f, 0.6f, 0.3f};
    outAmbientColor = {0.4f, 0.3f, 0.5f};
    outSunIntensity = 0.7f;
    break;

  case LightingMood::GoldenAfternoon:
    outSunColor = {1.0f, 0.95f, 0.8f};
    outAmbientColor = {0.35f, 0.4f, 0.5f};
    outSunIntensity = 1.0f;
    break;

  case LightingMood::DramaticSunset:
    outSunColor = {1.0f, 0.4f, 0.2f};
    outAmbientColor = {0.5f, 0.3f, 0.4f};
    outSunIntensity = 0.8f;
    break;

  case LightingMood::MoonlitNight:
    outSunColor = {0.6f, 0.7f, 1.0f};
    outAmbientColor = {0.1f, 0.1f, 0.2f};
    outSunIntensity = 0.3f;
    break;

  case LightingMood::StarlitCosmos:
    outSunColor = {0.3f, 0.3f, 0.5f};
    outAmbientColor = {0.05f, 0.05f, 0.1f};
    outSunIntensity = 0.1f;
    break;

  case LightingMood::NeonCyberpunk:
    outSunColor = {0.8f, 0.2f, 0.9f};
    outAmbientColor = {0.1f, 0.3f, 0.4f};
    outSunIntensity = 0.5f;
    break;

  case LightingMood::CandlelitAntique:
    outSunColor = {1.0f, 0.7f, 0.4f};
    outAmbientColor = {0.2f, 0.15f, 0.1f};
    outSunIntensity = 0.4f;
    break;

  case LightingMood::StudioNeutral:
    outSunColor = {1.0f, 1.0f, 1.0f};
    outAmbientColor = {0.4f, 0.4f, 0.4f};
    outSunIntensity = 1.0f;
    break;

  case LightingMood::UnderwaterCaustics:
    outSunColor = {0.3f, 0.7f, 0.9f};
    outAmbientColor = {0.1f, 0.3f, 0.4f};
    outSunIntensity = 0.5f;
    break;

  case LightingMood::VolcanicGlow:
    outSunColor = {1.0f, 0.3f, 0.1f};
    outAmbientColor = {0.3f, 0.1f, 0.05f};
    outSunIntensity = 0.8f;
    break;

  case LightingMood::AuroralDance:
    outSunColor = {0.2f, 1.0f, 0.5f};
    outAmbientColor = {0.1f, 0.15f, 0.2f};
    outSunIntensity = 0.3f;
    break;

  case LightingMood::FoggyMystery:
    outSunColor = {0.7f, 0.7f, 0.75f};
    outAmbientColor = {0.5f, 0.5f, 0.55f};
    outSunIntensity = 0.4f;
    break;

  case LightingMood::StormyDarkness:
    outSunColor = {0.4f, 0.4f, 0.5f};
    outAmbientColor = {0.15f, 0.15f, 0.2f};
    outSunIntensity = 0.2f;
    break;

  default:
    outSunColor = {1.0f, 1.0f, 1.0f};
    outAmbientColor = {0.3f, 0.3f, 0.3f};
    outSunIntensity = 1.0f;
    break;
  }
}

} // namespace game::components
