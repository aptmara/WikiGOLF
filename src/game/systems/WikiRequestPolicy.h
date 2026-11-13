#pragma once
/**
 * @file WikiRequestPolicy.h
 * @brief Wikimediaへのリクエスト間隔と再試行の規則
 *
 * 未ログインのクライアントに求められている内容（2026年11月時点）:
 * - Action API: 同時1件・毎秒5件未満。1秒を超えた応答の後は5秒空ける。
 * - REST API: 同時3件まで・毎秒5件未満。
 * - 記事のHTML: Action APIではなく、通常ページ（/wiki/記事名、クエリなし）かREST APIから取る。
 * - 画像（upload / thumb）: 同時2件まで。
 * - User-Agentを名乗る未ログインクライアントは、全APIの合計で毎分200件まで。
 * - 429/503にはRetry-After（無ければ5秒以上か指数バックオフ）に従う。
 * https://wikitech.wikimedia.org/wiki/Robot_policy
 * https://www.mediawiki.org/wiki/Wikimedia_APIs/Rate_limits
 */

#include <string_view>

namespace game::systems::wiki_request {

inline constexpr int kMaxAttempts = 3;
/** これより長い待機は行わず失敗扱いにする（ゲームの終了を長く止めないため） */
inline constexpr int kMaxRetryWaitSeconds = 10;
/**
 * ja.wikipedia.org へのリクエスト開始間隔。
 * 同時1件でこの間隔を守れば毎分177件以下になり、毎秒5件未満と毎分200件の両方に収まる。
 */
inline constexpr int kMinWikiIntervalMilliseconds = 340;
/** ja.wikipedia.org への同時リクエスト数。Action APIの上限に合わせ、RESTも同じ枠で送る */
inline constexpr int kMaxWikiConcurrentRequests = 1;
/** 画像配信ホスト（upload / thumb）への同時リクエスト数 */
inline constexpr int kMaxMediaConcurrentRequests = 2;
/** ja.wikipedia.org へ1分間に送ってよい件数の上限 */
inline constexpr int kMaxWikiRequestsPerMinute = 200;
/** Action APIの応答がこれより遅かったら、次のリクエストまで間を空ける */
inline constexpr int kSlowActionApiThresholdMilliseconds = 1000;
inline constexpr int kSlowActionApiCooldownSeconds = 5;

/** @brief Action API（/w/api.php）へのリクエストかどうかを返します。 */
inline bool IsActionApiPath(std::wstring_view path) {
  constexpr std::wstring_view kActionApi = L"/w/api.php";
  return path.substr(0, kActionApi.size()) == kActionApi;
}

/**
 * @brief 応答時間に応じて、次のリクエストまで空ける秒数を返します。
 * @details Action APIだけが対象で、RESTや画像の取得には適用しません。
 */
inline int SlowRequestCooldownSeconds(bool isActionApi, long long elapsedMs) {
  return isActionApi && elapsedMs > kSlowActionApiThresholdMilliseconds
             ? kSlowActionApiCooldownSeconds
             : 0;
}

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
