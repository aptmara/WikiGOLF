/**
 * @file test_wiki_request_policy.cpp
 * @brief Wikimediaへの再試行規則を検証します。
 */

#include "src/game/systems/WikiRequestPolicy.h"

#include <cassert>

using namespace game::systems::wiki_request;

int main() {
  assert(IsSuccessStatus(200));
  assert(!IsSuccessStatus(304));
  assert(!IsSuccessStatus(404));
  assert(!IsSuccessStatus(429));
  assert(!IsSuccessStatus(0));

  assert(IsRetryableStatus(429));
  assert(IsRetryableStatus(503));
  assert(!IsRetryableStatus(404));
  assert(!IsRetryableStatus(500));
  assert(!IsRetryableStatus(0));

  // Retry-Afterの秒数を優先する
  assert(RetryDelaySeconds(L"7", 0) == 7);
  assert(RetryDelaySeconds(L"120", 2) == 120);
  assert(RetryDelaySeconds(L"0", 0) == 1);

  // 無い場合と日付形式は、5秒からの指数バックオフにする
  assert(RetryDelaySeconds(L"", 0) == 5);
  assert(RetryDelaySeconds(L"", 1) == 10);
  assert(RetryDelaySeconds(L"", 2) == 20);
  assert(RetryDelaySeconds(L"Wed, 21 Oct 2026 07:28:00 GMT", 0) == 5);

  // Action APIだけ、1秒を超えた応答の後に5秒空ける
  assert(IsActionApiPath(L"/w/api.php?action=query"));
  assert(!IsActionApiPath(L"/w/rest.php/v1/page/A/html"));
  assert(!IsActionApiPath(L"/wikipedia/commons/a/ab/A.jpg"));
  assert(SlowRequestCooldownSeconds(true, 1001) == 5);
  assert(SlowRequestCooldownSeconds(true, 1000) == 0);
  assert(SlowRequestCooldownSeconds(false, 5000) == 0);
  assert(kMaxWikiConcurrentRequests == 1);
  assert(kMaxMediaConcurrentRequests <= 2);
  // 同時1件で開始間隔を守れば、毎秒5件未満と毎分200件の両方に収まる
  assert(kMaxWikiRequestsPerMinute <= 200);
  assert(kMinWikiIntervalMilliseconds > 200);
  assert(60000 / kMinWikiIntervalMilliseconds <= kMaxWikiRequestsPerMinute);

  // 異常に大きな値でも桁あふれしない
  assert(RetryDelaySeconds(L"99999999999999999999", 0) > kMaxRetryWaitSeconds);
  return 0;
}
