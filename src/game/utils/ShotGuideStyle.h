#pragma once
/**
 * @file ShotGuideStyle.h
 * @brief ショット予測表示の見た目の決まりごと（色・大きさ・動きの速さ）
 *
 * 調整する数値はすべてここに集める。描画側はここから値を読むだけで、
 * 自分では数値を持たない。色や動きの計算は副作用のない関数にしてある。
*/

#include "../components/WikiComponents.h"
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>

namespace game::guide {

/** @brief ShotGuide シェーダーの描き分け番号（ShotGuideVS/PS.hlsl と揃える）*/
namespace shape {
constexpr float kRibbonFlight = 1.0f; ///< 飛行区間の帯（流れる光）
constexpr float kRibbonRoll = 2.0f;   ///< 転がり区間の帯（点線）
constexpr float kOrb = 3.0f;          ///< 光の玉（カメラを向く）
constexpr float kDisc = 7.0f;         ///< 淡い面（止まりそうな範囲の内側）
constexpr float kRipple = 8.0f;       ///< 広がる波紋
constexpr float kAimArrow = 9.0f;     ///< 足元の流れる矢羽根
constexpr float kBezel = 10.0f;       ///< 足元の方位目盛り
constexpr float kBeam = 11.0f;        ///< 立ちのぼる光の柱
} // namespace shape

/** @brief 見た目の調整値*/
struct ShotGuideStyle {
  // --- 予測線 ---
  float ribbonWidth = 0.20f;        ///< 帯の幅[m]（縁のぼかし込み。線そのものは約4割）
  float rollWidth = 0.17f;          ///< 転がり区間の帯の幅[m]
  float ribbonStartFade = 0.9f;     ///< ボールからこの道のりまでは薄くして、ボールを隠さない[m]
  float flowSpeedIdle = 5.0f;       ///< 光が流れる速さ（待機中）[m/秒]
  float flowSpeedCharged = 17.0f;   ///< 光が流れる速さ（フルパワー）[m/秒]
  float flowWrap = 12.0f;           ///< 流れの位相を折り返す長さ（模様の周期の公倍数）[m]
  float xrayAlpha = 0.20f;          ///< 地形や芝に隠れた部分を透かして見せる濃さ
  float xrayStartArc = 1.6f;        ///< 透かし表示を始める道のり（手前のボールや人には重ねない）[m]

  // --- 描き始め・出現 ---
  float revealSeconds = 0.38f; ///< 線が手前から伸びきるまで
  float popSeconds = 0.42f;    ///< 着地点の印が弾んで出るまで

  // --- 道すじを走る光の玉 ---
  float ghostFlightSeconds = 2.6f; ///< これより長い飛びは早送りして、この秒数で見せる
  float ghostMinSpeed = 1.2f;      ///< 短い飛びでも実時間よりこれだけ速く見せる
  float ghostPauseSeconds = 0.9f;  ///< 止まってから次の周回までの間
  float ghostSize = 0.34f;         ///< 玉の大きさ[m]（光のにじみ込み）
  float ghostGlowReach = 2.6f;     ///< 玉が通るとき線が明るくなる範囲[m]

  // --- 止まりそうな範囲 ---
  float zoneRimWidth = 0.16f;     ///< 範囲のふちの点線の幅[m]
  float zoneRimFlow = 0.35f;      ///< ふちの点線が回る速さ（線の流れに対する割合）
  float zoneFillMaxSize = 6.0f;   ///< これより大きい範囲は内側を塗らない[m]（平らな板なので起伏に刺さる）
  float zoneEndFade = 0.30f;      ///< 線が範囲へ溶け込むときの、先端の濃さ
  float markerLift = 0.07f;       ///< 地面から浮かせる高さ[m]
  float rippleSeconds = 0.75f;    ///< 波紋が広がりきるまで
  float landingRippleSize = 2.2f; ///< 最初の着地の波紋の直径[m]
  float beamHeight = 3.2f;        ///< 光の柱の高さ[m]
  float beamWidth = 0.42f;        ///< 光の柱の幅[m]

  // --- 近距離での控えめ表示 ---
  float nearDistance = 3.0f;  ///< これより近いショットは最小の大きさで描く[m]
  float farDistance = 30.0f;  ///< これより遠いショットは通常の大きさで描く[m]
  float nearScale = 0.36f;    ///< 最小の大きさ（通常に対する割合）

  // --- 足元の向き表示 ---
  float aimSize = 3.0f;        ///< 矢羽根を描く板の一辺[m]
  float aimFlowIdle = 0.85f;   ///< 矢羽根が進む速さ（待機中）[周/秒]
  float aimFlowCharged = 2.6f; ///< 矢羽根が進む速さ（フルパワー）[周/秒]
};

struct GuideColor {
  float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
  DirectX::XMFLOAT4 ToFloat4() const { return {r, g, b, a}; }
  GuideColor WithAlpha(float alpha) const { return {r, g, b, alpha}; }
};

inline float Saturate(float v) { return std::clamp(v, 0.0f, 1.0f); }
inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float SmoothStep(float edge0, float edge1, float x) {
  if (edge1 == edge0) return x < edge0 ? 0.0f : 1.0f;
  const float t = Saturate((x - edge0) / (edge1 - edge0));
  return t * t * (3.0f - 2.0f * t);
}
inline GuideColor Mix(const GuideColor &a, const GuideColor &b, float t) {
  t = Saturate(t);
  return {Lerp(a.r, b.r, t), Lerp(a.g, b.g, t), Lerp(a.b, b.b, t),
          Lerp(a.a, b.a, t)};
}

/** @brief 少し行き過ぎてから戻る出現の動き（0→1）*/
inline float EaseOutBack(float t) {
  t = Saturate(t);
  constexpr float c1 = 1.70158f;
  constexpr float c3 = c1 + 1.0f;
  const float u = t - 1.0f;
  return 1.0f + c3 * u * u * u + c1 * u * u;
}
/** @brief ショットの飛距離に応じた表示の大きさ（近いほど小さく控えめに）*/
inline float GuideScaleFor(const ShotGuideStyle &style, float totalDistance) {
  const float t = SmoothStep(style.nearDistance, style.farDistance, totalDistance);
  return Lerp(style.nearScale, 1.0f, t);
}

inline float EaseOutCubic(float t) {
  const float u = 1.0f - Saturate(t);
  return 1.0f - u * u * u;
}

/** @brief 予測線に使う色の組*/
struct GuidePalette {
  GuideColor nearColor;  ///< ボール寄りの色
  GuideColor farColor;   ///< 着地点寄りの色
  GuideColor rollColor;  ///< 転がり区間の色
  GuideColor accent;     ///< 光の玉・矢羽根の色
};

/**
 * @brief パワーに応じた色の組を返す。
 * @param energy 表示中のパワー比率(0〜1)
 * @param charging ゲージを動かしている最中か。待機中は落ち着いた寒色、
 *                 ゲージ中は力が上がるほど熱い色へ寄せる。
*/
inline GuidePalette PaletteFor(float energy, bool charging) {
  const GuidePalette calm{{0.42f, 0.90f, 1.00f, 1.0f},
                          {0.94f, 1.00f, 1.00f, 1.0f},
                          {0.62f, 1.00f, 0.80f, 1.0f},
                          {0.70f, 0.96f, 1.00f, 1.0f}};
  if (!charging) {
    return calm;
  }
  const GuidePalette warm{{1.00f, 0.86f, 0.30f, 1.0f},
                          {1.00f, 0.98f, 0.86f, 1.0f},
                          {1.00f, 0.92f, 0.55f, 1.0f},
                          {1.00f, 0.90f, 0.45f, 1.0f}};
  const GuidePalette hot{{1.00f, 0.42f, 0.16f, 1.0f},
                         {1.00f, 0.90f, 0.62f, 1.0f},
                         {1.00f, 0.66f, 0.34f, 1.0f},
                         {1.00f, 0.60f, 0.26f, 1.0f}};
  const float e = Saturate(energy);
  const float toWarm = SmoothStep(0.0f, 0.55f, e);
  const float toHot = SmoothStep(0.55f, 1.0f, e);
  auto blend = [&](const GuideColor &c, const GuideColor &w,
                   const GuideColor &h) {
    return Mix(Mix(c, w, toWarm), h, toHot);
  };
  return {blend(calm.nearColor, warm.nearColor, hot.nearColor),
          blend(calm.farColor, warm.farColor, hot.farColor),
          blend(calm.rollColor, warm.rollColor, hot.rollColor),
          blend(calm.accent, warm.accent, hot.accent)};
}

/** @brief 止まる場所の地面の種類*/
enum class SurfaceMood {
  Safe,    ///< フェアウェイなど
  Great,   ///< グリーン
  Caution, ///< ラフ・バンカーなど、次が打ちにくい
  Danger,  ///< 水・溶岩など、打ち直しになる
};

inline SurfaceMood MoodFor(game::components::TerrainMaterial material) {
  using game::components::TerrainMaterial;
  switch (material) {
  case TerrainMaterial::Green:
    return SurfaceMood::Great;
  case TerrainMaterial::Rough:
  case TerrainMaterial::Bunker:
    return SurfaceMood::Caution;
  case TerrainMaterial::Water:
  case TerrainMaterial::Lava:
    return SurfaceMood::Danger;
  default:
    return SurfaceMood::Safe;
  }
}

/** @brief 着地点・静止点の印の色。止まる地面の種類で変える*/
inline GuideColor MarkerColorFor(game::components::TerrainMaterial material,
                                 const GuidePalette &palette) {
  using game::components::TerrainMaterial;
  switch (material) {
  case TerrainMaterial::Green:
    return {0.50f, 1.00f, 0.62f, 1.0f};
  case TerrainMaterial::Rough:
    return {0.96f, 0.92f, 0.42f, 1.0f};
  case TerrainMaterial::Bunker:
    return {1.00f, 0.74f, 0.30f, 1.0f};
  case TerrainMaterial::Water:
    return {1.00f, 0.34f, 0.34f, 1.0f};
  case TerrainMaterial::Lava:
    return {1.00f, 0.30f, 0.22f, 1.0f};
  case TerrainMaterial::Ice:
    return {0.78f, 0.94f, 1.00f, 1.0f};
  default:
    return palette.farColor;
  }
}

/**
 * @brief 印の明るさの脈動(0〜1)。危ない場所ほど速く強く明滅させる。
 * @param time 経過時間[秒]
*/
inline float MarkerPulse(SurfaceMood mood, float time) {
  float speed = 2.2f;
  float depth = 0.14f;
  if (mood == SurfaceMood::Caution) {
    speed = 3.4f;
    depth = 0.22f;
  } else if (mood == SurfaceMood::Danger) {
    speed = 7.5f;
    depth = 0.45f;
  }
  return 1.0f - depth * (0.5f - 0.5f * std::cos(time * speed));
}

} // namespace game::guide
