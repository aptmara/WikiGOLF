/**
 * @file test_aim_pin_solver.cpp
 * @brief エイムピンの必要パワー逆算（高低差の織り込み）を検証します。
 */

#include "src/game/utils/AimPinSolver.h"
#include <cmath>
#include <cstdlib>
#include <iostream>

#define CHECK_TRUE(condition, message)                                         \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << "[FAIL] " << message << "\n";                               \
      std::exit(1);                                                            \
    }                                                                          \
    std::cout << "[PASS] " << message << "\n";                                 \
  } while (0)

#define CHECK_CLOSE(actual, expected, eps, message)                            \
  do {                                                                         \
    if (std::fabs((actual) - (expected)) > (eps)) {                            \
      std::cerr << "[FAIL] " << message << " (expected " << (expected)         \
                << ", got " << (actual) << ")\n";                              \
      std::exit(1);                                                            \
    }                                                                          \
    std::cout << "[PASS] " << message << "\n";                                 \
  } while (0)

int main() {
  using game::utils::SolveAimPinPower;

  constexpr float kBaseCarry = 200.0f;

  // 平坦: 必要パワー比は素直に「距離 / 基準飛距離」になる。
  {
    const auto solution = SolveAimPinPower(
        100.0f, kBaseCarry, [](float ratio) { return kBaseCarry * ratio; });
    CHECK_CLOSE(solution.requiredPowerRatio, 0.5f, 0.01f,
                "flat lie needs half power for half distance");
    CHECK_CLOSE(solution.playsLikeDistance, 100.0f, 2.0f,
                "flat lie plays as its real distance");
    CHECK_TRUE(solution.reachable, "flat half distance is reachable");
  }

  // 打ち上げ: 同じ強さでも手前に落ちるため、必要パワーは平坦より大きくなる。
  {
    const auto uphill = SolveAimPinPower(100.0f, kBaseCarry, [](float ratio) {
      return kBaseCarry * ratio * 0.8f; // 高低差で2割ぶん手前に着弾
    });
    CHECK_CLOSE(uphill.requiredPowerRatio, 0.625f, 0.01f,
                "uphill pin needs more power than the flat ratio");
    CHECK_CLOSE(uphill.playsLikeDistance, 125.0f, 2.0f,
                "uphill pin plays longer than its real distance");
    CHECK_TRUE(uphill.reachable, "uphill pin is still reachable");
  }

  // 打ち下ろし: 同じ強さで奥まで転がるため、必要パワーは小さくなる。
  {
    const auto downhill = SolveAimPinPower(100.0f, kBaseCarry, [](float ratio) {
      return kBaseCarry * ratio * 1.25f;
    });
    CHECK_CLOSE(downhill.requiredPowerRatio, 0.4f, 0.01f,
                "downhill pin needs less power than the flat ratio");
    CHECK_CLOSE(downhill.playsLikeDistance, 80.0f, 2.0f,
                "downhill pin plays shorter than its real distance");
  }

  // フルスイングでも届かない場合は、足りない分を外挿して比率1.0超を返す。
  {
    const auto tooFar = SolveAimPinPower(
        260.0f, kBaseCarry, [](float ratio) { return kBaseCarry * ratio; });
    CHECK_TRUE(!tooFar.reachable, "pin beyond full swing is not reachable");
    CHECK_TRUE(tooFar.requiredPowerRatio > 1.0f,
               "unreachable pin reports a ratio above full power");
    CHECK_TRUE(tooFar.playsLikeDistance > kBaseCarry,
               "unreachable pin plays longer than the club's carry");
  }

  // 非線形な到達距離でも、追加サンプルによる補正で十分な精度が出る。
  {
    const auto curved = SolveAimPinPower(50.0f, kBaseCarry, [](float ratio) {
      return kBaseCarry * ratio * ratio; // 弱いほど極端に飛ばない特性
    });
    CHECK_CLOSE(curved.requiredPowerRatio, 0.5f, 0.02f,
                "non-linear reach curve is inverted accurately");
  }

  // 到達距離が斜面の跳ね方でわずかに前後しても、解が破綻しない。
  {
    const auto noisy = SolveAimPinPower(100.0f, kBaseCarry, [](float ratio) {
      const float wobble = (ratio > 0.55f && ratio < 0.65f) ? -6.0f : 0.0f;
      return kBaseCarry * ratio + wobble;
    });
    CHECK_TRUE(noisy.requiredPowerRatio > 0.45f &&
                   noisy.requiredPowerRatio < 0.58f,
               "non-monotonic samples still produce a sane ratio");
  }

  // 退化ケース: 基準飛距離が無い（クラブ未初期化）ときは0を返して黙って諦める。
  {
    const auto invalid =
        SolveAimPinPower(100.0f, 0.0f, [](float) { return 0.0f; });
    CHECK_CLOSE(invalid.requiredPowerRatio, 0.0f, 0.0001f,
                "zero carry distance yields zero ratio");
    CHECK_TRUE(!invalid.reachable, "zero carry distance is not reachable");
  }

  // ピンがボール直下（距離0）なら力は不要。
  {
    const auto zero = SolveAimPinPower(
        0.0f, kBaseCarry, [](float ratio) { return kBaseCarry * ratio; });
    CHECK_CLOSE(zero.requiredPowerRatio, 0.0f, 0.0001f,
                "zero distance needs no power");
    CHECK_TRUE(zero.reachable, "zero distance is reachable");
  }

  std::cout << "All aim pin solver tests passed.\n";
  return 0;
}
