#include "src/game/utils/BallFastForwardState.h"
#include <cmath>
#include <cstdlib>
#include <iostream>

#define CHECK(condition, message)                                             \
  do {                                                                        \
    if (!(condition)) {                                                        \
      std::cerr << "[FAIL] " << message << "\n";                            \
      return 1;                                                               \
    }                                                                         \
  } while (false)

#define CHECK_CLOSE(actual, expected, epsilon, message)                       \
  CHECK(std::fabs((actual) - (expected)) <= (epsilon), message)

int main() {
  using game::utils::FastForwardTier;
  using game::utils::GetFastForwardSpeedMultiplier;
  using game::utils::ResolveFastForwardTier;

  CHECK(ResolveFastForwardTier(9.999f) == FastForwardTier::Normal,
        "10秒未満は通常速度です");
  CHECK(ResolveFastForwardTier(10.0f) == FastForwardTier::Speed1_5x,
        "10秒で1.5倍速へ切り替わります");
  CHECK(ResolveFastForwardTier(12.5f) == FastForwardTier::Speed2_0x,
        "12.5秒で2倍速へ切り替わります");
  CHECK(ResolveFastForwardTier(15.0f) == FastForwardTier::Speed4_0x,
        "15秒で4倍速へ切り替わります");
  CHECK(ResolveFastForwardTier(17.5f) == FastForwardTier::Speed8_0x,
        "17.5秒で8倍速へ切り替わります");

  CHECK_CLOSE(GetFastForwardSpeedMultiplier(FastForwardTier::Normal), 1.0f,
              0.0001f, "通常速度の倍率は1です");
  CHECK_CLOSE(GetFastForwardSpeedMultiplier(FastForwardTier::Speed1_5x), 1.5f,
              0.0001f, "1.5倍速の倍率を返します");
  CHECK_CLOSE(GetFastForwardSpeedMultiplier(FastForwardTier::Speed2_0x), 2.0f,
              0.0001f, "2倍速の倍率を返します");
  CHECK_CLOSE(GetFastForwardSpeedMultiplier(FastForwardTier::Speed4_0x), 4.0f,
              0.0001f, "4倍速の倍率を返します");
  CHECK_CLOSE(GetFastForwardSpeedMultiplier(FastForwardTier::Speed8_0x), 8.0f,
              0.0001f, "8倍速の倍率を返します");

  std::cout << "[PASS] 着地後の倍速段階を確認しました\n";
  return 0;
}
