#include "src/core/FramePacer.h"

#include <chrono>
#include <iostream>

namespace {

using core::FramePacer;
using namespace std::chrono_literals;

bool Check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

} // namespace

int main() {
  const FramePacer::Clock::time_point base{};
  const FramePacer::Clock::duration period = 10ms;
  bool ok = true;

  // 間に合っていれば前回の目標時刻から積み上げる（寝過ごしを累積させない）
  ok &= Check(FramePacer::NextDeadline(base, base + 3ms, period) == base + 10ms,
              "on-time frame must advance from the previous deadline");

  // 1周期以内の遅れは取り戻す
  ok &= Check(FramePacer::NextDeadline(base, base + 15ms, period) == base + 10ms,
              "small overrun must be caught up");
  ok &= Check(FramePacer::NextDeadline(base, base + 20ms, period) == base + 10ms,
              "overrun of exactly one period must be caught up");

  // 長いヒッチの後は基準を取り直す（連続で駆け抜けない）
  ok &= Check(FramePacer::NextDeadline(base, base + 500ms, period) == base + 500ms,
              "long hitch must reset the deadline to now");

  // 上限なしは待機しない
  FramePacer pacer;
  const auto startedAt = FramePacer::Clock::now();
  for (int i = 0; i < 1000; ++i) {
    pacer.Wait(0);
  }
  ok &= Check(FramePacer::Clock::now() - startedAt < 100ms,
              "unlimited frame rate must not wait");

  return ok ? 0 : 1;
}
