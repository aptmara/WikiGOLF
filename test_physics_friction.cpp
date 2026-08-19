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
  const float gravity = 9.8f;
  const float k = frictionCoeff * gravity;

  // 1) 摩擦減速が線形に計算されることを確認
  {
    float startSpeed = 5.0f;
    float newSpeed = ApplyRollingFriction(startSpeed, frictionCoeff, dt);
    float expectedSpeed = startSpeed - frictionCoeff * gravity * dt;
    CHECK_CLOSE(newSpeed, expectedSpeed, 1e-5f,
                "Rolling friction uses linear decay");
  }

  // 2) 速度が極低速しきい値を下回る場合は静止する
  {
    // しきい値を 0.02f としているので、それ以下になるような入力を与える
    float startSpeed = 0.015f; 
    float newSpeed = ApplyRollingFriction(startSpeed, frictionCoeff, dt);
    CHECK(std::fabs(newSpeed) < 1e-6f, "Speed drops to zero when below threshold");
  }

  // 3) 摩擦係数ゼロなら速度は変化しない
  {
    float startSpeed = 3.0f;
    float newSpeed = ApplyRollingFriction(startSpeed, 0.0f, dt);
    CHECK_CLOSE(newSpeed, startSpeed, 1e-6f, "Zero friction keeps velocity unchanged");
  }

  // 4) ドロップ計算ヘルパーは従来の線形計算（静止摩擦判定などで利用）
  {
    float expectedDrop = frictionCoeff * gravity * dt;
    float drop = ComputeRollingFrictionDrop(frictionCoeff, dt);
    CHECK_CLOSE(drop, expectedDrop, 1e-6f, "Drop helper maintains linear gravity-scaled value");
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

    CHECK(flatFairway < flatGreen,
          "Green friction is now heavier than fairway");
    CHECK(bunker > flatFairway * 5.0f,
          "Bunker friction stops the ball much sooner than fairway");
    CHECK(steepFairway > settings.constantBrake * 0.5f,
          "Steep slopes keep a friction floor to avoid endless sliding");

    float creeping =
        game::systems::ComputeGrassRollingAcceleration(
            0.5f, 1.0f, game::components::TerrainMaterial::Fairway, 1.0f,
            settings);
    CHECK(creeping < flatFairway * 0.7f,
          "Nonlinear friction eases at low speed to keep the ball creeping");
  }

  // 7) バンカーだけが速度に応じて沈み、半径内に収まる
  {
    constexpr float ballRadius = 0.02135f;
    float fairway = game::systems::ComputeSurfaceSinkDepth(
        game::components::TerrainMaterial::Fairway, 8.0f, 12.0f,
        ballRadius);
    float restingSand = game::systems::ComputeSurfaceSinkDepth(
        game::components::TerrainMaterial::Bunker, 0.0f, 0.0f, ballRadius);
    float impactSand = game::systems::ComputeSurfaceSinkDepth(
        game::components::TerrainMaterial::Bunker, 12.0f, 18.0f,
        ballRadius);

    CHECK_CLOSE(fairway, 0.0f, 1e-6f,
                "Fairway does not sink the ball");
    CHECK(restingSand > 0.0f, "Resting ball settles slightly into bunker");
    CHECK(restingSand >= ballRadius * 0.95f,
          "Resting ball is buried halfway into bunker sand");
    CHECK(impactSand > restingSand,
          "Hard bunker impact sinks deeper than a resting ball");
    CHECK(impactSand <= ballRadius * 1.55f + 1e-6f,
          "Bunker sink depth remains bounded by the ball radius");
  }

  // 8) バンカー着地時だけ横方向の運動量を砂へ大きく吸収する
  {
    float fairwayRetention =
        game::systems::ComputeSurfaceImpactTangentialRetention(
            game::components::TerrainMaterial::Fairway, 10.0f, 20.0f);
    float softSandRetention =
        game::systems::ComputeSurfaceImpactTangentialRetention(
            game::components::TerrainMaterial::Bunker, 2.0f, 8.0f);
    float hardSandRetention =
        game::systems::ComputeSurfaceImpactTangentialRetention(
            game::components::TerrainMaterial::Bunker, 10.0f, 20.0f);

    CHECK_CLOSE(fairwayRetention, 1.0f, 1e-6f,
                "Fairway preserves tangential impact speed");
    CHECK(softSandRetention < 0.5f,
          "Bunker absorbs more than half of tangential impact speed");
    CHECK(hardSandRetention < softSandRetention,
          "Hard bunker impacts lose more tangential speed");
  }

  std::cout << "All physics friction tests passed!\n";
  return 0;
}
