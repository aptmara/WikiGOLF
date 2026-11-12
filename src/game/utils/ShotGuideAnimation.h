#pragma once
/**
 * @file ShotGuideAnimation.h
 * @brief ショット予測表示の「動き」の状態と、その進め方
 *
 * 流れる光の位相、線の描き始め、印の出現、道すじを走る光の玉の位置、
 * 着地・静止の瞬間に出す波紋。どれも時間だけで決まるので、描画から
 * 切り離してここで進める。描画側は結果の数値を読むだけ。
*/

#include "ShotGuidePath.h"
#include "ShotGuideStyle.h"
#include <algorithm>
#include <cmath>

namespace game::guide {

struct ShotGuideAnimation {
  float time = 0.0f;      ///< 表示を始めてからの時間[秒]
  float fade = 0.0f;      ///< 全体の濃さ(0〜1)。出始めにふわっと現れる
  float flowPhase = 0.0f; ///< 線の光が流れた道のり[m]（style.flowWrap で折り返す）
  float aimPhase = 0.0f;  ///< 足元の矢羽根の位相(0〜1)
  float spin = 0.0f;      ///< 切れ目のある輪の回転[rad]
  float reveal = 0.0f;    ///< 線の描き始め(0〜1)
  float pop = 0.0f;       ///< 印の出現(0〜1)
  float ghostTime = 0.0f; ///< 光の玉が今いる、打ち出しからの時間[秒]
  float landingRipple = -1.0f; ///< 着地の波紋(0〜1)。負なら出ていない
  float restRipple = -1.0f;    ///< 静止の波紋(0〜1)。負なら出ていない

  /** @brief クラブを替えた・打つ場所が変わったときなど、線を描き直すところから始める*/
  void Restart() {
    reveal = 0.0f;
    pop = 0.0f;
    ghostTime = 0.0f;
    landingRipple = -1.0f;
    restRipple = -1.0f;
  }

  /** @brief 非表示になったとき。次に出るときは最初から*/
  void Reset() {
    *this = ShotGuideAnimation{};
  }

  /** @brief 光の玉が進む、実時間に対する速さの倍率*/
  static float GhostSpeed(const ShotGuidePath &path,
                          const ShotGuideStyle &style) {
    return std::max(style.ghostMinSpeed,
                    path.totalTime / std::max(0.1f, style.ghostFlightSeconds));
  }

  void Advance(const ShotGuidePath &path, const ShotGuideStyle &style,
               float dt, float energy) {
    dt = std::clamp(dt, 0.0f, 0.1f);
    const float e = Saturate(energy);
    time += dt;
    fade = std::min(1.0f, fade + dt / 0.18f);

    flowPhase += dt * Lerp(style.flowSpeedIdle, style.flowSpeedCharged, e);
    if (flowPhase >= style.flowWrap) {
      flowPhase = std::fmod(flowPhase, style.flowWrap);
    }
    aimPhase += dt * Lerp(style.aimFlowIdle, style.aimFlowCharged, e);
    aimPhase -= std::floor(aimPhase);
    spin += dt * 0.55f * (1.0f + e);
    if (spin > 6.2831853f) {
      spin -= 6.2831853f;
    }

    reveal = std::min(1.0f, reveal + dt / std::max(0.01f, style.revealSeconds));
    // 印は、線がだいたい届いてから弾んで出る
    if (reveal >= 0.7f) {
      pop = std::min(1.0f, pop + dt / std::max(0.01f, style.popSeconds));
    }

    auto advanceRipple = [&](float &ripple) {
      if (ripple >= 0.0f) {
        ripple += dt / std::max(0.01f, style.rippleSeconds);
        if (ripple >= 1.0f) {
          ripple = -1.0f;
        }
      }
    };
    advanceRipple(landingRipple);
    advanceRipple(restRipple);

    // 光の玉は線が伸びきってから走り出す
    if (reveal < 1.0f || !path.valid) {
      return;
    }
    const float speed = GhostSpeed(path, style);
    const float before = ghostTime;
    ghostTime += dt * speed;
    if (path.hasLanding) {
      const float landingTime = path.Landing().time;
      if (before < landingTime && ghostTime >= landingTime) {
        landingRipple = 0.0f;
      }
    }
    if (before < path.totalTime && ghostTime >= path.totalTime) {
      restRipple = 0.0f;
    }
    const float cycle = path.totalTime + style.ghostPauseSeconds * speed;
    if (ghostTime >= cycle) {
      ghostTime = 0.0f;
    }
  }

  /** @brief 光の玉が今いる道のり[m]*/
  float GhostArc(const ShotGuidePath &path) const {
    return PathArcAtTime(path, ghostTime);
  }

  /** @brief 光の玉の濃さ。出だしはボールと重ならないよう薄く、止まったら消える*/
  float GhostAlpha(const ShotGuidePath &path,
                   const ShotGuideStyle &style) const {
    if (reveal < 1.0f || !path.valid) {
      return 0.0f;
    }
    const float arc = GhostArc(path);
    float alpha = SmoothStep(0.25f, 1.1f, arc);
    if (ghostTime > path.totalTime) {
      const float fadeOut = 0.22f * GhostSpeed(path, style);
      alpha *= Saturate(1.0f - (ghostTime - path.totalTime) / fadeOut);
    }
    return alpha;
  }
};

} // namespace game::guide
