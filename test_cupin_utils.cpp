#include "src/game/scenes/CupInUtils.h"
#include "src/game/components/WikiComponents.h"
#include <algorithm>
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

namespace {

struct RollResult {
  bool settled = false;
  bool escapedBelowRim = false;
  float finalDistance = 0.0f;
  float finalY = 0.0f;
};

void ApplyContacts(const game::physics::GolfCupShape &cup,
                   DirectX::XMFLOAT3 &p, DirectX::XMFLOAT3 &v, float r) {
  using namespace game::physics;
  GolfCupContact contacts[4];
  const int count = ComputeCupInteriorContacts(cup, p, r, contacts);
  for (int i = 0; i < count; ++i) {
    const auto &c = contacts[i];
    p.x += c.normal.x * c.penetration;
    p.y += c.normal.y * c.penetration;
    p.z += c.normal.z * c.penetration;
    const float vn = v.x * c.normal.x + v.y * c.normal.y + v.z * c.normal.z;
    if (vn >= 0.0f) {
      continue;
    }
    if (c.kind == GolfCupContactKind::Floor) {
      const float k = vn * (1.0f + kGolfCupFloorRestitution);
      v.x -= c.normal.x * k;
      v.y -= c.normal.y * k;
      v.z -= c.normal.z * k;
    } else {
      const float nx = c.normal.x * vn;
      const float ny = c.normal.y * vn;
      const float nz = c.normal.z * vn;
      v.x = -nx * kGolfCupWallRestitution +
            (v.x - nx) * kGolfCupWallTangentialRetention;
      v.y = -ny * kGolfCupWallRestitution +
            (v.y - ny) * kGolfCupWallTangentialRetention;
      v.z = -nz * kGolfCupWallRestitution +
            (v.z - nz) * kGolfCupWallTangentialRetention;
    }
  }
}

// PhysicsSystemSimulation と同じ順序（積分前の接触→重力→積分→積分後の接触）で、
// 平らな地面を転がるボールをカップへ向かわせる簡易シミュレーション。
RollResult RollIntoCup(float speed, float lateralOffset) {
  using namespace game::physics;
  GolfCupShape cup;
  cup.pinRadius = 0.0275f * 0.9f;
  cup.pinHeight = 2.4f;
  const float r = kBallRadius;
  DirectX::XMFLOAT3 p{-0.3f, r, lateralOffset};
  DirectX::XMFLOAT3 v{speed, 0.0f, 0.0f};
  const float frameDt = 1.0f / 60.0f;
  RollResult result;
  for (int frame = 0; frame < 600; ++frame) {
    const float horizontal = std::sqrt(v.x * v.x + v.z * v.z);
    const int subSteps = horizontal < 0.75f ? 1 : (horizontal < 8.0f ? 2 : 4);
    const float dt = frameDt / subSteps;
    for (int step = 0; step < subSteps; ++step) {
      const bool overCup = IsOverCupOpening(cup, p.x, p.y, p.z);
      bool grounded = false;
      if (overCup) {
        ApplyContacts(cup, p, v, r);
        grounded = IsRestingOnCupFloor(cup, p, r);
      } else if (p.y - r <= 0.0f) {
        p.y = r;
        v.y = (std::max)(v.y, 0.0f);
        grounded = true;
      }
      if (grounded) {
        const float s = std::sqrt(v.x * v.x + v.z * v.z);
        const float drop = 1.2f * dt; // 転がり摩擦の目安
        const float scale = s > drop ? (s - drop) / s : 0.0f;
        v.x *= scale;
        v.z *= scale;
      } else {
        v.y -= 9.8f * dt;
      }
      p.x += v.x * dt;
      p.y += v.y * dt;
      p.z += v.z * dt;
      if (overCup) {
        ApplyContacts(cup, p, v, r);
      }
    }
    const float d = HorizontalDistanceToCup(cup, p.x, p.z);
    if (d > PhysicsCupRadius(cup) && p.y < cup.rimY - 0.01f) {
      result.escapedBelowRim = true;
    }
    if (IsBallSettledInCup(cup, p, r)) {
      result.settled = true;
      break;
    }
  }
  result.finalDistance = HorizontalDistanceToCup(GolfCupShape{}, p.x, p.z);
  result.finalY = p.y;
  return result;
}

} // namespace

int main() {
  using game::scenes::cupin::IsBallWithinCupApproachRange;
  using game::scenes::cupin::IsBallReadyForCupIn;
  using namespace game::physics;

  DirectX::XMFLOAT3 hole{0.0f, 0.0f, 0.0f};
  const float r = kBallRadius;
  const float cupRadius = kGolfCupRadius;
  const GolfCupShape cup = game::scenes::cupin::MakeCupShape(hole, cupRadius);
  const float floorY = PhysicsCupFloorY(cup);
  const float innerRadius = PhysicsCupRadius(cup);

  CHECK(game::components::GolfHole{}.gravity == 0.0f,
        "Cup should not attract the ball before cup-in");
  CHECK(game::components::GolfHole{}.radius == kGolfCupRadius,
        "Hole radius should match the visual cup radius");

  CHECK(IsBallWithinCupApproachRange({1.24f, 0.0f, 0.0f}, hole),
        "Cup approach effect should start just inside its radius");
  CHECK(!IsBallWithinCupApproachRange({1.26f, 0.0f, 0.0f}, hole),
        "Cup approach effect should stay local to the cup");

  CHECK(!IsBallReadyForCupIn({0.0f, r, 0.0f}, hole, cupRadius, r),
        "Ball on the rim level should not trigger cup-in");
  CHECK(!IsBallReadyForCupIn({0.0f, floorY + r + 0.15f, 0.0f}, hole,
                             cupRadius, r),
        "Ball still falling inside the cup should wait for the floor");
  CHECK(IsBallReadyForCupIn({0.05f, floorY + r, 0.05f}, hole, cupRadius, r),
        "Ball resting on the cup floor should cup-in");
  CHECK(!IsBallReadyForCupIn({innerRadius + 0.01f, floorY + r, 0.0f}, hole,
                             cupRadius, r),
        "Ball outside the cup wall should not cup-in");

  GolfCupContact contacts[4];
  // 縁より下で壁を越えた（すり抜けた）球は内側へ押し戻される
  int count = ComputeCupInteriorContacts(
      cup, {innerRadius + 0.02f, -0.1f, 0.0f}, r, contacts);
  CHECK(count == 1 && contacts[0].kind == GolfCupContactKind::Wall &&
            contacts[0].normal.x < -0.99f,
        "Tunneled ball below the rim should be pushed back inside");

  // 縁の上に乗った球は内側・上向きの法線を受ける
  count = ComputeCupInteriorContacts(
      cup, {innerRadius - 0.01f, r * 0.9f, 0.0f}, r, contacts);
  CHECK(count == 1 && contacts[0].kind == GolfCupContactKind::Rim &&
            contacts[0].normal.x < 0.0f && contacts[0].normal.y > 0.0f,
        "Ball on the lip should roll toward the cup center");

  // 底の角では底と壁の両方に接触する
  count = ComputeCupInteriorContacts(
      cup, {innerRadius - r * 0.5f, floorY + r * 0.5f, 0.0f}, r, contacts);
  CHECK(count == 2, "Ball in the floor corner should touch floor and wall");

  // 開口部の外（地表上）は対象外
  CHECK(!IsOverCupOpening(cup, innerRadius + 0.01f, r, 0.0f),
        "Ball beside the cup should be supported by terrain");
  CHECK(IsOverCupOpening(cup, 0.0f, r, 0.0f),
        "Ball above the opening should lose terrain support");

  GolfCupShape pinned = cup;
  pinned.pinRadius = 0.03f;
  pinned.pinHeight = 2.0f;
  count = ComputeCupInteriorContacts(pinned, {0.02f, floorY + r, 0.0f}, r,
                                     contacts);
  bool hitPin = false;
  for (int i = 0; i < count; ++i) {
    hitPin = hitPin || (contacts[i].kind == GolfCupContactKind::Pin &&
                        contacts[i].normal.x > 0.99f);
  }
  CHECK(hitPin, "Pin should push the ball away from the cup center");

  for (float speed : {0.6f, 1.5f, 2.5f, 3.5f}) {
    const RollResult roll = RollIntoCup(speed, 0.0f);
    std::cout << "  roll speed=" << speed << " settled=" << roll.settled
              << " d=" << roll.finalDistance << " y=" << roll.finalY << "\n";
    CHECK(roll.settled && !roll.escapedBelowRim,
          "Ball rolling through the cup center should drop in and stay");
  }
  {
    const RollResult roll = RollIntoCup(1.5f, 0.12f);
    CHECK(roll.settled && !roll.escapedBelowRim,
          "Ball rolling off-center at moderate speed should drop in");
  }
  for (float speed : {0.6f, 3.0f, 12.0f, 30.0f}) {
    const RollResult roll = RollIntoCup(speed, 0.2f);
    std::cout << "  lip speed=" << speed << " settled=" << roll.settled
              << " d=" << roll.finalDistance << " y=" << roll.finalY << "\n";
    CHECK(!roll.escapedBelowRim,
          "Ball must never pass through the cup wall below the rim");
  }
  {
    const RollResult roll = RollIntoCup(30.0f, 0.0f);
    std::cout << "  fast speed=30 settled=" << roll.settled
              << " d=" << roll.finalDistance << "\n";
    CHECK(!roll.escapedBelowRim,
          "Very fast ball must not tunnel through the wall");
  }

  std::cout << "All cup-in utility tests passed!\n";
  return 0;
}
