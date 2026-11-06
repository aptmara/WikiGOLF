#pragma once
/**
 * @file GoalNoticeRules.h
 * @brief 記事到着時の紙の演出とゴール案内で使う純粋な規則
*/

#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace game::utils {

/** @brief 記事の紙が立っている時間と、倒れる時間です（秒）。*/
struct ArticleArrivalTiming {
  float fallStart = 0.0f;    ///< 紙が倒れ始める時刻
  float fallDuration = 0.0f; ///< 倒れ終わるまでの時間

  constexpr float End() const { return fallStart + fallDuration; }
};

/** @brief ラウンド開始時。道のりと旗が立ち終わってから読む時間を取ります。*/
inline constexpr ArticleArrivalTiming kRoundStartArrival{3.4f, 0.8f};
/** @brief 2記事目以降。紙が倒れるだけの短縮版です。*/
inline constexpr ArticleArrivalTiming kShortArrival{0.25f, 0.95f};

inline constexpr float kGoalNoticeInputLock = 0.2f; ///< 前の画面の入力を拾わない時間

/** @brief 紙が倒れる進み具合を返します。倒れるほど速くなります。*/
inline float CalculatePaperFallProgress(float elapsed,
                                        const ArticleArrivalTiming &timing) {
  const float t = std::clamp((elapsed - timing.fallStart) /
                                 std::max(timing.fallDuration, 0.001f),
                             0.0f, 1.0f);
  return t * t;
}

/**
 * @brief 紙の傾きを返します。
 * @param standAngle カメラと正対して立っているときの角度（ラジアン）です。
 * @return standAngle から、地面と平行になる π/2 までの角度です。
*/
inline float CalculatePaperAngle(float fallProgress, float standAngle) {
  return standAngle + (DirectX::XM_PIDIV2 - standAngle) * fallProgress;
}

/**
 * @brief 紙の不透明度を返します。
 * @details ボールを隠さないよう、倒れる時間の後半で消し切ります。
*/
inline float CalculatePaperOpacity(float elapsed,
                                   const ArticleArrivalTiming &timing) {
  const float t = std::clamp((elapsed - timing.fallStart) /
                                 std::max(timing.fallDuration, 0.001f),
                             0.0f, 1.0f);
  return t <= 0.5f ? 1.0f : 1.0f - (t - 0.5f) * 2.0f;
}

/**
 * @brief プレイヤーの入力を受けたあとの経過時間を返します。
 * @details 紙が立っている間なら、倒れ始める時刻まで飛ばします。
*/
inline float ResolveGoalNoticeAdvance(float elapsed,
                                      const ArticleArrivalTiming &timing) {
  if (elapsed < kGoalNoticeInputLock) {
    return elapsed;
  }
  return std::max(elapsed, timing.fallStart);
}

/**
 * @brief 道のりの欄に出す補足を作ります。
 * @param par パーです。0以下なら省きます。
 * @param hops 最短手数です。0（未解析）なら省きます。
*/
inline std::wstring FormatGoalNoticeDetail(int par, int hops) {
  std::wstring detail;
  if (par > 0) {
    detail = L"パー" + std::to_wstring(par);
  }
  if (hops > 0) {
    if (!detail.empty()) {
      detail += L" · ";
    }
    detail += L"最短" + std::to_wstring(hops) + L"手";
  }
  return detail;
}

// ---- ラウンド開始時の道のり ----
// 座標は 1280x720 の仮想解像度です。スキップボタン（y=646〜）に重ならない高さにしています。
inline constexpr float kRouteBandTop = 290.0f;    ///< 道のりの欄の上端
inline constexpr float kRouteBandBottom = 640.0f; ///< 道のりの欄の下端
inline constexpr float kRouteCupY = 528.0f;       ///< 線とカップの高さ
inline constexpr float kRouteStartX = 200.0f;     ///< スタートのボールの位置
inline constexpr float kRouteEndX = 1060.0f;      ///< ゴールの旗の位置
inline constexpr float kRoutePixelsPerFlagMeter = 32.0f; ///< 旗の 1m を何ピクセルで見せるか
inline constexpr float kRouteLineStart = 0.3f;    ///< 線が伸び始める時刻
inline constexpr float kRouteLineEnd = 1.3f;      ///< 線がゴールへ届く時刻
inline constexpr float kRouteFlagDuration = 0.38f;     ///< 旗が立ち終わるまで
inline constexpr float kRouteGoalFlagDuration = 0.48f; ///< ゴールの旗が立ち終わるまで
inline constexpr float kRouteFadeOutDuration = 0.2f;   ///< 紙が倒れ始めてから文字が消えるまで

/** @brief 道のりに立てる旗1本です。*/
struct GoalRouteFlag {
  int step = 0;      ///< 何手目の旗か。最短手数が不明なら 0
  int remaining = 0; ///< その旗のホールに入ったあとの残り手数。0 はゴールへ直接
  float x = 0.0f;    ///< 仮想解像度での位置
};

/**
 * @brief 最短手数から、たどる順に旗を並べます。
 * @param hops スタートからの最短手数です。0（未計算）ならゴールの旗だけを返します。
*/
inline std::vector<GoalRouteFlag> BuildGoalRouteFlags(int hops) {
  if (hops <= 0) {
    return {{0, 0, kRouteEndX}};
  }
  std::vector<GoalRouteFlag> flags;
  flags.reserve(static_cast<std::size_t>(hops));
  for (int step = 1; step <= hops; ++step) {
    const float t = static_cast<float>(step) / static_cast<float>(hops);
    flags.push_back(
        {step, hops - step, kRouteStartX + (kRouteEndX - kRouteStartX) * t});
  }
  return flags;
}

/** @brief 線の伸び具合を返します。0でスタート、1でゴールの旗まで届いています。*/
inline float CalculateRouteLineProgress(float elapsed) {
  const float t = std::clamp(
      (elapsed - kRouteLineStart) / (kRouteLineEnd - kRouteLineStart), 0.0f, 1.0f);
  const float rest = 1.0f - t;
  return 1.0f - rest * rest * rest;
}

/** @brief 線の先端が x へ届く時刻を返します。線の伸び方（ease out）に合わせます。*/
inline float CalculateRouteArrivalTime(float x) {
  const float reach =
      std::clamp((x - kRouteStartX) / (kRouteEndX - kRouteStartX), 0.0f, 1.0f);
  const float t = 1.0f - std::cbrt(1.0f - reach);
  return kRouteLineStart + (kRouteLineEnd - kRouteLineStart) * t;
}

/** @brief 旗が立つ進み具合を返します。線が届いてから立ち始めます。*/
inline float CalculateRouteFlagProgress(float elapsed, const GoalRouteFlag &flag) {
  const float duration =
      flag.remaining == 0 ? kRouteGoalFlagDuration : kRouteFlagDuration;
  return std::clamp((elapsed - CalculateRouteArrivalTime(flag.x)) / duration,
                    0.0f, 1.0f);
}

/** @brief 竿が伸びる割合です。カップから勢いよく出て、少し行き過ぎてから戻ります。*/
inline float CalculateFlagRise(float progress) {
  const float t = std::clamp(progress / 0.6f, 0.0f, 1.0f);
  if (t <= 0.0f) {
    return 0.0f;
  }
  constexpr float kOvershoot = 1.9f;
  const float u = t - 1.0f;
  return 1.0f + (kOvershoot + 1.0f) * u * u * u + kOvershoot * u * u;
}

/** @brief 布が広がる割合です。竿が伸びきる手前から広がります。*/
inline float CalculateFlagUnfurl(float progress) {
  const float t = std::clamp((progress - 0.45f) / 0.55f, 0.0f, 1.0f);
  const float rest = 1.0f - t;
  return 1.0f - rest * rest * rest;
}

/** @brief 旗の下の文字の不透明度です。旗が半分立ってから出します。*/
inline float CalculateRouteLabelOpacity(float flagProgress) {
  return std::clamp((flagProgress - 0.5f) / 0.5f, 0.0f, 1.0f);
}

/** @brief ゴールの記事名を出す進み具合です。ゴールの旗が立ち始めてから出します。*/
inline float CalculateRouteGoalTitleProgress(float elapsed) {
  return std::clamp(
      (elapsed - CalculateRouteArrivalTime(kRouteEndX) - 0.25f) / 0.32f, 0.0f,
      1.0f);
}

/** @brief スタートのボールが落ちて弾む高さを返します（0で着地、仮想ピクセル）。*/
inline float CalculateRouteBallLift(float elapsed) {
  const float t = std::clamp((elapsed - 0.12f) / 0.32f, 0.0f, 1.0f);
  if (t < 0.7f) {
    const float fall = t / 0.7f;
    return (1.0f - fall * fall) * 70.0f;
  }
  return std::sin((t - 0.7f) / 0.3f * DirectX::XM_PI) * 6.0f;
}

/** @brief スタートのボールを出す時刻です。*/
inline constexpr float kRouteBallAppear = 0.12f;

/** @brief 道のりの文字と線の不透明度です。紙が倒れ始めたらすぐ消します。*/
inline float CalculateRouteOpacity(float elapsed,
                                   const ArticleArrivalTiming &timing) {
  const float fadeIn = std::clamp(elapsed / 0.2f, 0.0f, 1.0f);
  const float fadeOut = std::clamp(
      (elapsed - timing.fallStart) / kRouteFadeOutDuration, 0.0f, 1.0f);
  return fadeIn * (1.0f - fadeOut);
}

/** @brief 旗の下に出す残り手数です。*/
inline std::wstring FormatRouteRemainingLabel(int remaining) {
  return remaining <= 0 ? std::wstring(L"ゴールへ")
                        : L"あと" + std::to_wstring(remaining) + L"手";
}

/** @brief 旗の下に出す何手目かです。最短手数が不明なら出しません。*/
inline std::wstring FormatRouteStepLabel(int step) {
  return step <= 0 ? std::wstring() : std::to_wstring(step) + L"手目";
}

/** @brief ゴールの記事名の文字サイズを文字数から選びます。*/
inline float SelectRouteGoalTitleFontSize(std::size_t titleCharacters) {
  if (titleCharacters <= 10) {
    return 46.0f;
  }
  if (titleCharacters <= 16) {
    return 38.0f;
  }
  if (titleCharacters <= 24) {
    return 30.0f;
  }
  return 24.0f;
}

/** @brief 紙の左右の余白です（紙の幅に対する割合）。ArticlePaperPS.hlsl へ渡します。*/
inline constexpr float kArticlePaperSideMargin = 0.03f;

/** @brief 記事タイルの幅と高さです。*/
struct ArticlePaperTileSize {
  float width = 0.0f;
  float height = 0.0f;
};

/** @brief 紙を構成する1枚の帯です。*/
struct ArticlePaperStrip {
  int tileIndex = -1;            ///< 使うタイル。-1 は記事のない白紙です
  float bottomOffset = 0.0f;     ///< 紙の下辺から帯の下辺までの距離
  float height = 0.0f;           ///< 帯の高さ
  float verticalFraction = 0.0f; ///< タイルの上から何割を使うか
};

/**
 * @brief 記事タイルを上から並べて、紙を埋める帯の一覧を作ります。
 * @details 記事は512pxごとのタイルに分かれているので、紙の高さに届くまで複数枚を
 *          使います。記事が短くて足りない分は白紙で埋めます。
 * @param contentWidth 紙のうち記事を置く幅です。字が歪まないよう高さの換算に使います。
*/
template <class TileRange>
std::vector<ArticlePaperStrip> BuildArticlePaperStrips(
    float contentWidth, float paperHeight, const TileRange &tiles) {
  std::vector<ArticlePaperStrip> strips;
  float remaining = paperHeight;
  int index = 0;
  for (const ArticlePaperTileSize &tile : tiles) {
    if (remaining <= 0.0f) {
      break;
    }
    if (tile.width > 0.0f && tile.height > 0.0f) {
      const float heightOnPaper = contentWidth * tile.height / tile.width;
      const float height = std::min(remaining, heightOnPaper);
      remaining -= height;
      strips.push_back({index, remaining, height, height / heightOnPaper});
    }
    ++index;
  }
  if (remaining > 0.0f) {
    strips.push_back({-1, 0.0f, remaining, 0.0f});
  }
  return strips;
}

/**
 * @brief コース上のホールの「ゴールまでの手数」から、スタート記事からの最短手数を求めます。
 * @details ゴールへのホールは 0、未解析は負値です。ホールへ入る 1 手を足して返します。
 * @return 最短手数。どのホールも未解析なら 0 を返します。
*/
template <class HopRange>
int CalculateStartToGoalHops(const HopRange &hopsToTarget) {
  int best = -1;
  for (int hops : hopsToTarget) {
    if (hops >= 0 && (best < 0 || hops < best)) {
      best = hops;
    }
  }
  return best < 0 ? 0 : best + 1;
}

} // namespace game::utils
