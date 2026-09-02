/**
 * @file PostProcessSystem.h
 * @brief ポストプロセスパラメータ（霧・色調補正・ビネット・ブルーム）の算出
 * @details 実際のGPU描画（フルスクリーンパス）はgraphics::GraphicsDeviceが担う。
 *          このクラスはEnvironmentState（記事テーマ由来の環境プリセット等）から
 *          描画に渡す定数を計算するだけの、GPUリソースを持たない値オブジェクト。
 */

#pragma once

#include "../components/EnvironmentState.h"
#include <algorithm>
#include <DirectXMath.h>

namespace game::systems {

using namespace DirectX;

/**
 * @brief ポストプロセス定数（GraphicsDevice::PostProcessParamsへコピーされる）
 */
struct PostProcessConstants {
  // 霧
  XMFLOAT4 fogColor;  // RGB + density
  XMFLOAT4 fogParams; // start, end, 0, 0

  // 色調補正
  XMFLOAT4 colorTint;   // RGB tint + brightness
  XMFLOAT4 colorParams; // saturation, contrast, 0, 0

  // ビネット
  XMFLOAT4 vignetteParams; // intensity, radius, softness, 0

  // 時間・ブルーム
  XMFLOAT4 timeParams; // time, bloomIntensity, bloomThreshold, bloomSpread
};

/**
 * @brief ポストプロセスパラメータ算出システム
 */
class PostProcessSystem {
public:
  /**
   * @brief 環境状態から定数を更新
   */
  void UpdateFromEnvironment(const components::EnvironmentState &env,
                             float time) {
    using LightingMood = game::components::LightingMood;

    constexpr float kMinBrightnessForReadability = 0.85f;
    float brightness =
        std::max(env.brightness, kMinBrightnessForReadability); // 暗いテーマでの視認性確保
    bool brightnessClamped = brightness > env.brightness;

    m_constants.fogColor = {env.fogColor.x, env.fogColor.y, env.fogColor.z,
                            env.fogDensity};
    m_constants.fogParams = {env.fogStart, env.fogEnd, 0, 0};

    m_constants.colorTint = {env.colorTint.x, env.colorTint.y, env.colorTint.z,
                             brightness};
    m_constants.colorParams = {env.saturation, env.contrast, 0, 0};

    // ビネットはライティングムードに応じて調整
    float vignetteIntensity = 0.3f;

    int currentMood = static_cast<int>(env.lightingMood);

    // Horrorは定義されていないため削除、StormyDarknessのみ
    if (currentMood == static_cast<int>(LightingMood::StormyDarkness)) {
      vignetteIntensity = 0.6f;
    } else if (currentMood ==
                   static_cast<int>(LightingMood::CandlelitAntique) ||
               currentMood == static_cast<int>(LightingMood::MoonlitNight)) {
      vignetteIntensity = 0.4f;
    } else if (currentMood == static_cast<int>(LightingMood::StarlitCosmos) ||
               currentMood == static_cast<int>(LightingMood::NeonCyberpunk)) {
      vignetteIntensity = 0.2f;
    } else {
      vignetteIntensity = 0.25f;
    }
    if (brightnessClamped) {
      vignetteIntensity = std::max(0.2f, vignetteIntensity * 0.7f);
    }
    m_constants.vignetteParams = {vignetteIntensity, 0.7f, 0.5f, 0};
    float bloomIntensity = m_constants.timeParams.y;
    float bloomThreshold = m_constants.timeParams.z;
    float bloomSpread = m_constants.timeParams.w;
    m_constants.timeParams = {time, bloomIntensity, bloomThreshold,
                              bloomSpread};
  }

  /**
   * @brief デフォルト設定でリセット
   */
  void ResetToDefaults() {
    m_constants.fogColor = {0.7f, 0.75f, 0.8f, 0.0f};
    m_constants.fogParams = {100.0f, 500.0f, 0, 0};
    m_constants.colorTint = {1.0f, 1.0f, 1.0f, 1.0f};
    m_constants.colorParams = {1.0f, 1.0f, 0, 0};
    m_constants.vignetteParams = {0.25f, 0.7f, 0.5f, 0};
    m_constants.timeParams = {0, 0, 0.72f, 1.0f};
  }

  /**
   * @brief 霧パラメータ設定
   */
  void SetFog(const XMFLOAT3 &color, float density, float start, float end) {
    m_constants.fogColor = {color.x, color.y, color.z, density};
    m_constants.fogParams = {start, end, 0, 0};
  }

  /**
   * @brief 色調補正設定
   */
  void SetColorGrading(const XMFLOAT3 &tint, float brightness, float saturation,
                       float contrast) {
    m_constants.colorTint = {tint.x, tint.y, tint.z, brightness};
    m_constants.colorParams = {saturation, contrast, 0, 0};
  }

  /**
   * @brief ビネット設定
   */
  void SetVignette(float intensity, float radius = 0.7f,
                   float softness = 0.5f) {
    m_constants.vignetteParams = {intensity, radius, softness, 0};
  }

  /**
   * @brief ブルーム風の発光にじみを設定します。
   * @param intensity 発光にじみの強さ
   * @param threshold 抽出しきい値
   * @param spread サンプル範囲倍率
   */
  void SetBloom(float intensity, float threshold = 0.72f,
                float spread = 1.0f) {
    m_constants.timeParams.y = std::max(0.0f, intensity);
    m_constants.timeParams.z = std::clamp(threshold, 0.0f, 1.0f);
    m_constants.timeParams.w = std::max(0.1f, spread);
  }

  /**
   * @brief 現在の定数を取得
   */
  const PostProcessConstants &GetConstants() const { return m_constants; }

private:
  PostProcessConstants m_constants = {};
};

} // namespace game::systems
