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

  // 異常に大きな値でも桁あふれしない
  assert(RetryDelaySeconds(L"99999999999999999999", 0) > kMaxRetryWaitSeconds);
  return 0;
}
