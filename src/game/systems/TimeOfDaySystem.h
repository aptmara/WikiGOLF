/**
 * @file TimeOfDaySystem.h
 * @brief 時間変化システム - 昼夜サイクル、太陽位置、ライティング変化
 */

#pragma once

#include "../components/EnvironmentState.h"
#include <DirectXMath.h>
#include <cmath>

namespace game::systems {

using namespace DirectX;

/**
 * @brief 時間帯
 */
enum class TimeOfDay {
  Night,     // 0:00-5:00
  Dawn,      // 5:00-7:00
  Morning,   // 7:00-10:00
  Noon,      // 10:00-14:00
  Afternoon, // 14:00-17:00
  Sunset,    // 17:00-19:00
  Dusk,      // 19:00-21:00
  Evening,   // 21:00-24:00
};

/**
 * @brief 時間から時間帯を取得
 */
inline TimeOfDay GetTimeOfDay(float hour) {
  if (hour < 5.0f)
    return TimeOfDay::Night;
  if (hour < 7.0f)
    return TimeOfDay::Dawn;
  if (hour < 10.0f)
    return TimeOfDay::Morning;
  if (hour < 14.0f)
    return TimeOfDay::Noon;
  if (hour < 17.0f)
    return TimeOfDay::Afternoon;
  if (hour < 19.0f)
    return TimeOfDay::Sunset;
  if (hour < 21.0f)
    return TimeOfDay::Dusk;
  return TimeOfDay::Evening;
}

/**
 * @brief 時間変化システム
 */
class TimeOfDaySystem {
public:
  /**
   * @brief 初期化
   * @param startHour 開始時刻 (0-24)
   * @param timeScale 時間スケール (0=静止, 1=リアルタイム, 60=1分が1秒)
   */
  void Initialize(float startHour = 12.0f, float timeScale = 0.0f) {
    m_currentHour = startHour;
    m_timeScale = timeScale;
    UpdateSunPosition();
    UpdateColors();
  }

  /**
   * @brief 更新（連続的な時間進行が必要な場合のみ使用）
   * @param dt デルタタイム（秒）
   */
  void Update(float dt) {
    if (m_timeScale <= 0.0f)
      return;

    // 時間進行（timeScale = 1で1秒=1時間）
    m_currentHour += dt * m_timeScale / 3600.0f;
    NormalizeTime();
    UpdateSunPosition();
    UpdateColors();
  }

  /**
   * @brief ショット時に時間を進める（ゲーム内時間）
   * @param hoursPerShot ショットあたりの進行時間（デフォルト0.5時間=30分）
   */
  void OnShot(float hoursPerShot = 0.5f) {
    m_currentHour += hoursPerShot;
    NormalizeTime();
    UpdateSunPosition();
    UpdateColors();
  }

  /**
   * @brief ページ遷移時に時間を進める（ゲーム内時間）
   * @param hoursPerPage ページあたりの進行時間（デフォルト2時間）
   */
  void OnPageTransition(float hoursPerPage = 2.0f) {
    m_currentHour += hoursPerPage;
    NormalizeTime();
    UpdateSunPosition();
    UpdateColors();
  }

  /**
   * @brief カップイン時に時間を進める（ゲーム内時間）
   * @param hoursPerCupIn カップインあたりの進行時間（デフォルト1時間）
   */
  void OnCupIn(float hoursPerCupIn = 1.0f) {
    m_currentHour += hoursPerCupIn;
    NormalizeTime();
    UpdateSunPosition();
    UpdateColors();
  }

  /**
   * @brief 時間を正規化（0-24範囲に収める）
   */
  void NormalizeTime() {
    while (m_currentHour >= 24.0f)
      m_currentHour -= 24.0f;
    while (m_currentHour < 0.0f)
      m_currentHour += 24.0f;
  }

  /**
   * @brief 時刻を設定
   */
  void SetTime(float hour) {
    m_currentHour = fmodf(hour, 24.0f);
    if (m_currentHour < 0.0f)
      m_currentHour += 24.0f;
    UpdateSunPosition();
    UpdateColors();
  }

  /**
   * @brief 時間スケール設定
   */
  void SetTimeScale(float scale) { m_timeScale = scale; }

  /**
   * @brief 現在時刻取得
   */
  float GetCurrentHour() const { return m_currentHour; }

  /**
   * @brief 現在の時間帯取得
   */
  TimeOfDay GetCurrentTimeOfDay() const { return GetTimeOfDay(m_currentHour); }

  /**
   * @brief 太陽方向取得（正規化済み）
   */
  XMFLOAT3 GetSunDirection() const { return m_sunDirection; }

  /**
   * @brief 太陽色取得
   */
  XMFLOAT3 GetSunColor() const { return m_sunColor; }

  /**
   * @brief 太陽強度取得
   */
  float GetSunIntensity() const { return m_sunIntensity; }

  /**
   * @brief アンビエント色取得
   */
  XMFLOAT3 GetAmbientColor() const { return m_ambientColor; }

  /**
   * @brief 環境ステートに適用
   */
  void ApplyToEnvironment(components::EnvironmentState &env) const {
    env.sunDirection = m_sunDirection;
    env.sunColor = m_sunColor;
    env.sunIntensity = m_sunIntensity;
    env.ambientColor = m_ambientColor;
    env.timeOfDay = m_currentHour;
  }

  /**
   * @brief 夜かどうか
   */
  bool IsNight() const {
    return m_currentHour < 6.0f || m_currentHour >= 20.0f;
  }

  /**
   * @brief ゴールデンアワーかどうか
   */
  bool IsGoldenHour() const {
    return (m_currentHour >= 6.0f && m_currentHour < 8.0f) ||
           (m_currentHour >= 17.0f && m_currentHour < 19.0f);
  }

private:
  void UpdateSunPosition() {
    // 太陽の軌道（単純化: 東から昇り西に沈む）
    // 6:00 = 東, 12:00 = 真上, 18:00 = 西
    float sunAngle = (m_currentHour - 6.0f) / 12.0f * XM_PI; // 0=東, PI=西
    float elevation = sinf(sunAngle) * XM_PIDIV2;            // 最大90度

    // 太陽が地平線下の場合
    if (elevation < 0.0f) {
      elevation = 0.0f; // 月として扱う場合は反転
    }

    m_sunDirection.x = cosf(sunAngle);
    m_sunDirection.y = -sinf(elevation); // 下向きが正
    m_sunDirection.z = 0.3f;             // 少し北寄り

    // 正規化
    XMVECTOR dir = XMLoadFloat3(&m_sunDirection);
    dir = XMVector3Normalize(dir);
    XMStoreFloat3(&m_sunDirection, dir);
  }

  void UpdateColors() {
    TimeOfDay tod = GetTimeOfDay(m_currentHour);

    switch (tod) {
    case TimeOfDay::Night:
      m_sunColor = {0.2f, 0.25f, 0.4f}; // 月光
      m_sunIntensity = 0.15f;
      m_ambientColor = {0.05f, 0.05f, 0.1f};
      break;

    case TimeOfDay::Dawn:
      m_sunColor = {1.0f, 0.6f, 0.4f}; // 朝焼け
      m_sunIntensity = 0.5f;
      m_ambientColor = {0.2f, 0.15f, 0.2f};
      break;

    case TimeOfDay::Morning:
      m_sunColor = {1.0f, 0.95f, 0.85f}; // 午前
      m_sunIntensity = 0.9f;
      m_ambientColor = {0.3f, 0.35f, 0.4f};
      break;

    case TimeOfDay::Noon:
      m_sunColor = {1.0f, 1.0f, 0.95f}; // 正午
      m_sunIntensity = 1.0f;
      m_ambientColor = {0.35f, 0.4f, 0.45f};
      break;

    case TimeOfDay::Afternoon:
      m_sunColor = {1.0f, 0.95f, 0.8f}; // 午後
      m_sunIntensity = 0.95f;
      m_ambientColor = {0.35f, 0.38f, 0.4f};
      break;

    case TimeOfDay::Sunset:
      m_sunColor = {1.0f, 0.5f, 0.2f}; // 夕焼け
      m_sunIntensity = 0.7f;
      m_ambientColor = {0.3f, 0.2f, 0.25f};
      break;

    case TimeOfDay::Dusk:
      m_sunColor = {0.6f, 0.4f, 0.5f}; // 薄暮
      m_sunIntensity = 0.3f;
      m_ambientColor = {0.15f, 0.1f, 0.2f};
      break;

    case TimeOfDay::Evening:
      m_sunColor = {0.3f, 0.3f, 0.5f}; // 夜
      m_sunIntensity = 0.2f;
      m_ambientColor = {0.08f, 0.08f, 0.12f};
      break;
    }
  }

  float m_currentHour = 12.0f;
  float m_timeScale = 0.0f;
  XMFLOAT3 m_sunDirection = {0.5f, -0.8f, 0.3f};
  XMFLOAT3 m_sunColor = {1.0f, 1.0f, 0.95f};
  float m_sunIntensity = 1.0f;
  XMFLOAT3 m_ambientColor = {0.35f, 0.4f, 0.45f};
};

} // namespace game::systems
