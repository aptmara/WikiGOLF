#include "src/game/systems/PhysicsFriction.h"
#include <cmath>
#include <iostream>

#define CHECK(condition, message)                                              \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << "[FAIL] " << message << "\n";                               \
      std::exit(1);                                                            \
    } else {                                                                   \
      std::cout << "[PASS] " << message << "\n";                               \
    }                                                                          \
  } while (0)

#define CHECK_CLOSE(actual, expected, eps, message)                            \
  CHECK(std::fabs((actual) - (expected)) <= (eps), message)

int main() {
  using game::systems::ApplyRollingFriction;
  using game::systems::ComputeRollingFrictionDrop;

  const float frictionCoeff = 0.35f;
  const float dt = 0.008f; // 30FPSサブステップ相当
  const float expectedDrop = frictionCoeff * 9.8f * dt;

  // 1) 摩擦減速が重力係数込みで計算されることを確認
  {
    float startSpeed = 5.0f;
    float newSpeed = ApplyRollingFriction(startSpeed, frictionCoeff, dt);
    CHECK_CLOSE(newSpeed, startSpeed - expectedDrop, 1e-5f,
                "Rolling friction uses gravity-scaled deceleration");
  }

  // 2) 減速量が速度を上回る場合は静止する
  {
    float newSpeed = ApplyRollingFriction(0.01f, frictionCoeff, dt);
    CHECK(std::fabs(newSpeed) < 1e-6f, "Speed drops to zero when friction exceeds velocity");
  }

  // 3) 摩擦係数ゼロなら速度は変化しない
  {
    float startSpeed = 3.0f;
    float newSpeed = ApplyRollingFriction(startSpeed, 0.0f, dt);
    CHECK_CLOSE(newSpeed, startSpeed, 1e-6f, "Zero friction keeps velocity unchanged");
  }

  // 4) ドロップ計算ヘルパーの単体検証
  {
    float drop = ComputeRollingFrictionDrop(frictionCoeff, dt);
    CHECK_CLOSE(drop, expectedDrop, 1e-6f, "Drop helper multiplies coefficient by gravity and dt");
  }

  // 5) 静止摩擦が坂の微小な押し出しを打ち消す
  {
    float smallSpeed = 0.05f;
    float gentleSlopeAcc = 1.0f; // 摩擦 0.35 * 9.8 = 3.43 > 1.0
    bool holds = game::systems::CanStaticFrictionHold(
        smallSpeed, frictionCoeff, gentleSlopeAcc, dt);
    CHECK(holds, "Static friction holds when tangential acceleration is weaker than friction");

    bool slides = game::systems::CanStaticFrictionHold(
        smallSpeed, frictionCoeff, 15.0f, dt);
    CHECK(!slides, "Static friction yields when slope acceleration exceeds friction capability");

    bool movingFast = game::systems::CanStaticFrictionHold(
        0.6f, frictionCoeff, gentleSlopeAcc, dt);
    CHECK(!movingFast, "Static friction is skipped once speed exceeds threshold");
  }

  std::cout << "All physics friction tests passed!\n";
  return 0;
}
