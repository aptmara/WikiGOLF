#pragma once
/**
 * @file FramePacer.h
 * @brief FPS上限を絶対時刻基準で守るフレームペーサー
 * @details 「残り時間だけ眠る」方式はSleepの寝過ごしが毎フレーム累積し、上限60で
 * 実測56FPS程度まで落ちる。目標時刻を前回の目標時刻から積み上げ、終盤だけスピン
 * 待ちすることで平均フレームレートを上限に一致させる。
*/

#include <chrono>
#include <thread>

namespace core {

class FramePacer {
public:
  using Clock = std::chrono::steady_clock;

  /// 目標時刻のこの時間前までSleepし、残りはスピンで待つ（Sleepの寝過ごし対策）
  static constexpr std::chrono::milliseconds kSpinMargin{2};

  /**
   * @brief 次の目標時刻を求めます。
   * @details 1周期以内の遅れは次フレームで取り戻す。それ以上遅れた場合は、
   * 長いヒッチの後に連続で駆け抜けないよう現在時刻を基準に取り直す。
  */
  static Clock::time_point NextDeadline(Clock::time_point previous,
                                        Clock::time_point now,
                                        Clock::duration period) {
    const Clock::time_point next = previous + period;
    if (next + period < now) {
      return now;
    }
    return next;
  }

  /**
   * @brief 次の目標時刻まで待機します。
   * @param fpsLimit FPS上限。0以下は無制限で待機しません。
  */
  void Wait(int fpsLimit) {
    if (fpsLimit <= 0) {
      m_fpsLimit = 0;
      return;
    }

    const Clock::time_point now = Clock::now();
    if (fpsLimit != m_fpsLimit) {
      m_fpsLimit = fpsLimit;
      m_deadline = now;
    }
    const auto period = std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double>(1.0 / static_cast<double>(fpsLimit)));
    m_deadline = NextDeadline(m_deadline, now, period);

    if (m_deadline - now > kSpinMargin) {
      std::this_thread::sleep_for(m_deadline - kSpinMargin - now);
    }
    while (Clock::now() < m_deadline) {
      std::this_thread::yield();
    }
  }

private:
  Clock::time_point m_deadline{};
  int m_fpsLimit = 0;
};

} // namespace core
