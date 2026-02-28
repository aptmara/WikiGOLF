#include "src/game/components/WikiComponents.h"
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

  // 6) ゴルフゲーム用の芝フリクションがマテリアルと斜面で変化する
  {
    game::systems::SurfaceFrictionSettings settings =
        game::systems::DefaultSurfaceFrictionSettings();

    float flatFairway = game::systems::ComputeGrassRollingAcceleration(
        5.0f, 1.0f, game::components::TerrainMaterial::Fairway, 1.0f,
        settings);
    float flatGreen = game::systems::ComputeGrassRollingAcceleration(
        5.0f, 1.0f, game::components::TerrainMaterial::Green, 1.0f, settings);
    float steepFairway = game::systems::ComputeGrassRollingAcceleration(
        10.0f, 0.2f, game::components::TerrainMaterial::Fairway, 1.0f,
        settings);
    float bunker = game::systems::ComputeGrassRollingAcceleration(
        8.0f, 1.0f, game::components::TerrainMaterial::Bunker, 1.0f, settings);

    CHECK(flatFairway > flatGreen,
          "Green friction stays lighter than fairway");
    CHECK(bunker > flatFairway * 1.5f,
          "Bunker friction is stronger than fairway");
    CHECK(steepFairway > settings.constantBrake * 0.5f,
          "Steep slopes keep a friction floor to avoid endless sliding");

    float creeping =
        game::systems::ComputeGrassRollingAcceleration(
            0.5f, 1.0f, game::components::TerrainMaterial::Fairway, 1.0f,
            settings);
    CHECK(creeping < flatFairway * 0.7f,
          "Nonlinear friction eases at low speed to keep the ball creeping");
  }

  std::cout << "All physics friction tests passed!\n";
  return 0;
}
