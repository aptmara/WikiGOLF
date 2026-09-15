#pragma once
/**
 * @file StartGoalIntroductionRules.h
 * @brief ラウンド開始時のスタート/ゴール記事紹介演出で使う純粋な規則
*/

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

namespace game::utils {

/** @brief スタート/ゴール紹介演出の時間配分です（秒）。*/
struct StartGoalIntroductionTiming {
  static constexpr float kRevealDuration = 0.45f;  ///< 各要素が現れるまでの時間
  static constexpr float kHeaderDelay = 0.10f;     ///< 見出しの登場開始
  static constexpr float kStartCardDelay = 0.30f;  ///< スタート記事カードの登場開始
  static constexpr float kArrowDelay = 0.95f;      ///< 矢印とPARの登場開始
  static constexpr float kGoalCardDelay = 1.35f;   ///< ゴール記事カードの登場開始
  static constexpr float kHintDelay = 2.10f;       ///< 操作案内の登場開始
  static constexpr float kAutoAdvanceTime = 7.5f;  ///< 自動でフェードアウトを始める時刻
  static constexpr float kFadeOutDuration = 0.45f; ///< 全体のフェードアウト時間

  /** @brief すべての要素が表示し終わる時刻です。*/
  static constexpr float FullyRevealedTime() {
    return kHintDelay + kRevealDuration;
  }
};

/** @brief プレイヤーが「次へ」を入力したときの挙動です。*/
enum class StartGoalAdvance {
  RevealAll, ///< 登場中の要素を即座に出し切る
  FadeOut    ///< 演出を閉じてコース紹介へ進む
};

/**
 * @brief 登場開始時刻からの進捗を ease-out cubic で返します。
 * @param elapsed 演出開始からの経過秒数です。
 * @param delay 要素の登場開始時刻です。
 * @return 0.0（未登場）から 1.0（登場完了）です。
*/
inline float CalculateStartGoalRevealProgress(float elapsed, float delay) {
  const float t = std::clamp(
      (elapsed - delay) / StartGoalIntroductionTiming::kRevealDuration, 0.0f,
      1.0f);
  const float inverse = 1.0f - t;
  return 1.0f - inverse * inverse * inverse;
}

/**
 * @brief 「次へ」入力時に要素を出し切るか、演出を閉じるかを決めます。
 * @param elapsed 演出開始からの経過秒数です。
*/
inline StartGoalAdvance ResolveStartGoalAdvance(float elapsed) {
  return elapsed < StartGoalIntroductionTiming::FullyRevealedTime()
             ? StartGoalAdvance::RevealAll
             : StartGoalAdvance::FadeOut;
}

/**
 * @brief 記事タイトルの文字数からカード見出しのフォントサイズを選びます。
 * @details 長いタイトルでも 2 行以内に収まるよう段階的に小さくします。
*/
inline float SelectStartGoalTitleFontSize(std::size_t titleCharacters) {
  if (titleCharacters <= 10) {
    return 32.0f;
  }
  if (titleCharacters <= 16) {
    return 27.0f;
  }
  if (titleCharacters <= 24) {
    return 22.0f;
  }
  return 19.0f;
}

/** @brief 概要本文を枠内へ収めるための文字サイズと最大文字数です。*/
struct StartGoalBodyFit {
  float fontSize = 15.0f;
  std::size_t maxCharacters = 180;
  bool fits = true; ///< どの候補でも収まらなかった場合は false
};

/**
 * @brief 概要本文が上限高さへ収まる、最も読みやすい組み合わせを選びます。
 * @details 先に文字サイズを段階的に下げ、それでも溢れる場合だけ文字数を削ります。
 * @param measureTallest (fontSize, maxCharacters) を受け取り、
 *        左右カードのうち高い方の本文高さを返す関数です。
 * @param maxBodyHeight 本文に使える最大高さです。
*/
template <class MeasureFn>
StartGoalBodyFit FitStartGoalBody(MeasureFn measureTallest,
                                  float maxBodyHeight) {
  constexpr float kFontSizes[] = {15.0f, 14.0f, 13.0f};
  constexpr std::size_t kCharacterLimits[] = {180, 150, 120, 90, 60};

  StartGoalBodyFit fit;
  for (float fontSize : kFontSizes) {
    fit.fontSize = fontSize;
    fit.maxCharacters = kCharacterLimits[0];
    if (measureTallest(fit.fontSize, fit.maxCharacters) <= maxBodyHeight) {
      return fit;
    }
  }
  for (std::size_t limit : kCharacterLimits) {
    fit.maxCharacters = limit;
    if (measureTallest(fit.fontSize, fit.maxCharacters) <= maxBodyHeight) {
      return fit;
    }
  }
  fit.fits = false;
  return fit;
}

/**
 * @brief コース上のホールの「目的記事までの手数」から、スタート記事からの最短手数を求めます。
 * @details 目的記事ホールは 0、未解析は負値です。ホールへ入る 1 手を足して返します。
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

/**
 * @brief カード下部に表示する Wikipedia 記事 URL 風の文字列を作ります。
 * @param title 記事タイトルです。
 * @param maxTitleCharacters タイトル部分の最大文字数です（超過時は末尾を…にします）。
*/
inline std::wstring FormatWikipediaArticleUrl(
    std::wstring_view title, std::size_t maxTitleCharacters = 22) {
  std::wstring path(title);
  std::replace(path.begin(), path.end(), L' ', L'_');
  if (maxTitleCharacters > 0 && path.size() > maxTitleCharacters) {
    path.resize(maxTitleCharacters - 1);
    path += L'…';
  }
  return L"ja.wikipedia.org/wiki/" + path;
}

} // namespace game::utils
