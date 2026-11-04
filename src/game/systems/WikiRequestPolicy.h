#pragma once
/**
 * @file WikiRequestPolicy.h
 * @brief Wikimediaへのリクエスト間隔と再試行の規則
 *
 * 未ログインのAction APIは同時1件・毎秒5件未満が目安で、429/503には
 * Retry-After（無ければ5秒以上か指数バックオフ）に従う必要がある。
 * https://wikitech.wikimedia.org/wiki/Robot_policy
 * https://www.mediawiki.org/wiki/Wikimedia_APIs/Rate_limits
 */

#include <string_view>

namespace game::systems::wiki_request {

inline constexpr int kMaxAttempts = 3;
/** これより長い待機は行わず失敗扱いにする（ゲームの終了を長く止めないため） */
inline constexpr int kMaxRetryWaitSeconds = 10;
/** ja.wikipedia.org へのリクエスト開始間隔（毎秒5件未満に収める） */
inline constexpr int kMinWikiIntervalMilliseconds = 220;

inline bool IsSuccessStatus(int status) { return status >= 200 && status < 300; }

inline bool IsRetryableStatus(int status) { return status == 429 || status == 503; }

/**
 * @brief 次の試行までの待機秒数を返します。
 * @param retryAfter Retry-Afterヘッダーの値（無ければ空）
 * @param attempt 0から数えた試行回数
 * @details 秒数形式だけを解釈し、日付形式などは指数バックオフ（5, 10, 20...）にします。
 */
inline int RetryDelaySeconds(std::wstring_view retryAfter, int attempt) {
  constexpr int kOverflowGuard = 86400;
  int seconds = 0;
  bool parsed = !retryAfter.empty();
  for (const wchar_t character : retryAfter) {
    if (character < L'0' || character > L'9') {
      parsed = false;
      break;
    }
    seconds = seconds * 10 + (character - L'0');
    if (seconds > kOverflowGuard) {
      seconds = kOverflowGuard;
    }
  }
  if (parsed) {
    return seconds > 0 ? seconds : 1;
  }
  return 5 << (attempt < 0 ? 0 : (attempt > 4 ? 4 : attempt));
}

} // namespace game::systems::wiki_request
