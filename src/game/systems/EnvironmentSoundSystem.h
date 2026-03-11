/**
 * @file EnvironmentSoundSystem.h
 * @brief 環境音システム - テーマ別アンビエントサウンド管理（3Dオーディオなし）
 */

#pragma once

#include "../../audio/AudioSystem.h"
#include "../../graphics/SkyboxTextureGenerator.h"
#include "../components/EnvironmentState.h"
#include <string>
#include <unordered_map>


namespace game::systems {

/**
 * @brief 環境音プリセット
 */
struct EnvironmentSoundPreset {
  std::string ambientPath; // 環境音ループパス
  std::string weatherPath; // 天候音パス（オプション）
  float ambientVolume = 0.3f;
  float weatherVolume = 0.2f;
  bool hasWeather = false;
};

/**
 * @brief テーマ別環境音プリセット取得
 */
inline EnvironmentSoundPreset GetSoundPreset(graphics::SkyboxTheme theme) {
  EnvironmentSoundPreset preset;

  switch (theme) {
  case graphics::SkyboxTheme::Forest:
    // preset.ambientPath = "Assets/sounds/ambient_forest.mp3";
    preset.ambientVolume = 0.35f;
    break;

  case graphics::SkyboxTheme::Ocean:
    // preset.ambientPath = "Assets/sounds/ambient_ocean.mp3";
    preset.ambientVolume = 0.4f;
    break;

  case graphics::SkyboxTheme::Desert:
    // preset.ambientPath = "Assets/sounds/ambient_desert.mp3";
    preset.ambientVolume = 0.25f;
    break;

  case graphics::SkyboxTheme::Mountain:
    // preset.ambientPath = "Assets/sounds/ambient_wind.mp3";
    preset.ambientVolume = 0.3f;
    break;

  case graphics::SkyboxTheme::Polar:
    // preset.ambientPath = "Assets/sounds/ambient_wind.mp3";
    // preset.weatherPath = "Assets/sounds/weather_snow.mp3";
    preset.ambientVolume = 0.35f;
    preset.weatherVolume = 0.2f;
    preset.hasWeather = true;
    break;

  case graphics::SkyboxTheme::Volcano:
    // preset.ambientPath = "Assets/sounds/ambient_volcano.mp3";
    preset.ambientVolume = 0.4f;
    break;

  case graphics::SkyboxTheme::SpaceAstronomy:
    // preset.ambientPath = "Assets/sounds/ambient_space.mp3";
    preset.ambientVolume = 0.2f;
    break;

  case graphics::SkyboxTheme::Horror:
    // preset.ambientPath = "Assets/sounds/ambient_horror.mp3";
    preset.ambientVolume = 0.3f;
    break;

  case graphics::SkyboxTheme::Medieval:
    // preset.ambientPath = "Assets/sounds/ambient_medieval.mp3";
    preset.ambientVolume = 0.25f;
    break;

  case graphics::SkyboxTheme::Urban:
    // preset.ambientPath = "Assets/sounds/ambient_city.mp3";
    preset.ambientVolume = 0.3f;
    break;

  case graphics::SkyboxTheme::War:
    // preset.ambientPath = "Assets/sounds/ambient_battle.mp3";
    preset.ambientVolume = 0.35f;
    break;

  case graphics::SkyboxTheme::Fantasy:
    // preset.ambientPath = "Assets/sounds/ambient_mystical.mp3";
    preset.ambientVolume = 0.3f;
    break;

  case graphics::SkyboxTheme::SciFi:
    // preset.ambientPath = "Assets/sounds/ambient_scifi.mp3";
    preset.ambientVolume = 0.25f;
    break;

  default:
    // デフォルト: 自然環境音
    // preset.ambientPath = "Assets/sounds/ambient_nature.mp3";
    preset.ambientVolume = 0.25f;
    break;
  }

  return preset;
}

/**
 * @brief 環境音システム
 * @note 既存のAudioSystemと連携して環境音を管理
 */
class EnvironmentSoundSystem {
public:
  /**
   * @brief 初期化
   */
  void Initialize(AudioSystem *audioSystem) {
    m_audioSystem = audioSystem;
    m_currentTheme = graphics::SkyboxTheme::Default;
    m_transitionProgress = 1.0f;
  }

  /**
   * @brief テーマ変更
   * @param theme 新しいテーマ
   * @param fadeTime フェード時間（秒）
   */
  void ChangeTheme(graphics::SkyboxTheme theme, float fadeTime = 2.0f) {
    if (theme == m_currentTheme)
      return;

    m_previousTheme = m_currentTheme;
    m_currentTheme = theme;
    m_transitionProgress = 0.0f;
    m_transitionDuration = fadeTime;
    m_isTransitioning = true;

    // 新しい環境音をロード（既存システムを使用）
    auto preset = GetSoundPreset(theme);
    // 注: 実際のロード・再生は AudioSystem に委譲
  }

  /**
   * @brief 更新
   * @param dt デルタタイム
   */
  void Update(float dt) {
    if (!m_isTransitioning)
      return;

    m_transitionProgress += dt / m_transitionDuration;
    if (m_transitionProgress >= 1.0f) {
      m_transitionProgress = 1.0f;
      m_isTransitioning = false;
    }

    // クロスフェード処理
    // 注: 実際の音量調整は AudioSystem のボリューム設定に委譲
  }

  /**
   * @brief 現在のテーマ取得
   */
  graphics::SkyboxTheme GetCurrentTheme() const { return m_currentTheme; }

  /**
   * @brief トランジション中かどうか
   */
  bool IsTransitioning() const { return m_isTransitioning; }

  /**
   * @brief トランジション進捗取得
   */
  float GetTransitionProgress() const { return m_transitionProgress; }

private:
  AudioSystem *m_audioSystem = nullptr;
  graphics::SkyboxTheme m_currentTheme = graphics::SkyboxTheme::Default;
  graphics::SkyboxTheme m_previousTheme = graphics::SkyboxTheme::Default;
  float m_transitionProgress = 1.0f;
  float m_transitionDuration = 2.0f;
  bool m_isTransitioning = false;
};

} // namespace game::systems
