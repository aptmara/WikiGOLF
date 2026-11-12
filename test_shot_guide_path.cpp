#include "src/game/utils/ShotGuideAnimation.h"
#include "src/game/utils/ShotGuidePath.h"
#include "src/game/utils/ShotGuideStyle.h"
#include "src/game/utils/ShotGuideZone.h"
#include <algorithm>
#include <cmath>
#include <iostream>

// 平坦地面(terrain=nullptr)の経路しか通らないが、GetHeight の呼び出しは
// コンパイル時に生成されるためリンクが必要。test_trajectory_simulation と同じ理由で
// この1関数だけスタブを用意する。
namespace game::systems {
float WikiTerrainSystem::GetHeight(float, float) const { return 0.0f; }
} // namespace game::systems

#define CHECK(condition, message)                                            \
  do {                                                                       \
    if (!(condition)) {                                                     \
      std::cerr << "[FAIL] " << message << "\n";                           \
      std::exit(1);                                                        \
    } else {                                                                \
      std::cout << "[PASS] " << message << "\n";                           \
    }                                                                       \
  } while (0)

int main() {
  using namespace game::guide;
  using namespace game::physics;

  ShotGuideRequest base;
  base.flatGround.enabled = true;
  base.flatGround.groundY = 0.0f;
  base.flatGround.material = game::components::TerrainMaterial::Fairway;
  base.direction = {0.0f, 0.0f, 1.0f};

  // 1) 初速0なら描けない
  {
    const ShotGuidePath path = BuildShotGuidePath(base, nullptr);
    CHECK(!path.valid, "zero speed gives no path");
  }

  // 2) 打ち上げるショット: 着地点があり、静止まで続く
  ShotGuideRequest wedge = base;
  wedge.speed = 28.0f;
  wedge.launchAngleDeg = 45.0f;
  const ShotGuidePath lofted = BuildShotGuidePath(wedge, nullptr);
  {
    CHECK(lofted.valid, "lofted shot gives a path");
    CHECK(lofted.settled, "lofted shot settles within the time limit");
    CHECK(lofted.hasLanding, "lofted shot has a first landing point");
    CHECK(lofted.landingIndex > 0 &&
              lofted.landingIndex < lofted.points.size() - 1,
          "landing lies between start and rest");
    CHECK(lofted.points[lofted.apexIndex].position.y >
              lofted.Start().position.y + 1.0f,
          "apex is well above the start");
    CHECK(lofted.apexIndex < lofted.landingIndex, "apex comes before landing");
    CHECK(lofted.carryDistance > 1.0f &&
              lofted.carryDistance < lofted.totalDistance,
          "carry is shorter than the total distance");
    CHECK(lofted.Landing().grounded, "landing point is on the ground");

    bool monotonic = true;
    for (size_t i = 1; i < lofted.points.size(); ++i) {
      monotonic = monotonic && lofted.points[i].arc > lofted.points[i - 1].arc &&
                  lofted.points[i].time > lofted.points[i - 1].time;
    }
    CHECK(monotonic, "arc and time strictly increase along the path");
    CHECK(lofted.points.size() <= ShotGuidePathOptions{}.maxPoints,
          "point count stays within the limit");

    // 実ショットの距離計算と同じ場所に止まる
    const CarryResult reference = SimulateCarryDistance(
        wedge.speed, wedge.launchAngleDeg, wedge.direction, wedge.start, nullptr,
        wedge.flatGround, wedge.ball, wedge.wind);
    CHECK(std::fabs(reference.horizontalDistance - lofted.totalDistance) < 0.01f,
          "rest position matches SimulateCarryDistance");
  }

  // 3) パター: 浮かないので着地点は無い
  {
    ShotGuideRequest putt = base;
    putt.speed = 6.0f;
    putt.launchAngleDeg = 0.0f;
    const ShotGuidePath path = BuildShotGuidePath(putt, nullptr);
    CHECK(path.valid && path.settled, "putt gives a settled path");
    CHECK(!path.hasLanding, "putt has no landing point");
    bool allGrounded = true;
    for (const auto &point : path.points) {
      allGrounded = allGrounded && point.grounded;
    }
    CHECK(allGrounded, "putt stays on the ground");
  }

  // 4) 点の上限を小さくしても、始点・着地点・終点は残る
  {
    ShotGuidePathOptions options;
    options.maxPoints = 12;
    const ShotGuidePath path = BuildShotGuidePath(wedge, nullptr, nullptr, options);
    CHECK(path.points.size() <= 14, "decimation respects the limit");
    CHECK(path.hasLanding && path.Landing().grounded &&
              std::fabs(path.Landing().arc - lofted.Landing().arc) < 1.0e-3f,
          "decimation keeps the landing point");
    CHECK(std::fabs(path.totalArc - lofted.totalArc) < 1.0e-3f,
          "decimation keeps the rest point");
  }

  // 5) 道のり・時間からの取り出し
  {
    const ShotGuideSample start = SamplePathAtArc(lofted, -5.0f);
    const ShotGuideSample end = SamplePathAtArc(lofted, lofted.totalArc + 5.0f);
    CHECK(std::fabs(start.position.z - lofted.Start().position.z) < 1.0e-4f,
          "sampling before the start clamps to the start");
    CHECK(std::fabs(end.position.z - lofted.Rest().position.z) < 1.0e-4f,
          "sampling past the end clamps to the rest point");
    CHECK(PathArcAtTime(lofted, 0.0f) == 0.0f, "arc at time zero is zero");
    CHECK(std::fabs(PathArcAtTime(lofted, lofted.totalTime + 1.0f) -
                    lofted.totalArc) < 1.0e-4f,
          "arc past the total time is the total arc");
    const float mid = PathArcAtTime(lofted, lofted.totalTime * 0.5f);
    CHECK(mid > 0.0f && mid < lofted.totalArc, "arc at half time is in between");
  }

  // 6) 動き: 線が伸びきってから玉が走り、着地と静止で波紋が1回ずつ出る
  {
    ShotGuideStyle style;
    ShotGuideAnimation animation;
    animation.Restart();
    int landingRipples = 0;
    int restRipples = 0;
    bool wrapped = true;
    bool ghostWaitedForReveal = true;
    const float cycleSeconds =
        style.revealSeconds +
        lofted.totalTime / ShotGuideAnimation::GhostSpeed(lofted, style) +
        style.ghostPauseSeconds * 0.5f;
    for (float t = 0.0f; t < cycleSeconds; t += 1.0f / 60.0f) {
      const float landingBefore = animation.landingRipple;
      const float restBefore = animation.restRipple;
      animation.Advance(lofted, style, 1.0f / 60.0f, 0.5f);
      if (landingBefore < 0.0f && animation.landingRipple >= 0.0f) ++landingRipples;
      if (restBefore < 0.0f && animation.restRipple >= 0.0f) ++restRipples;
      wrapped = wrapped && animation.flowPhase < style.flowWrap &&
                animation.aimPhase < 1.0f;
      if (animation.reveal < 1.0f) {
        ghostWaitedForReveal = ghostWaitedForReveal &&
                               animation.GhostAlpha(lofted, style) == 0.0f;
      }
    }
    CHECK(animation.reveal == 1.0f && animation.pop == 1.0f,
          "reveal and pop complete");
    CHECK(ghostWaitedForReveal, "ghost ball waits until the line is drawn");
    CHECK(landingRipples == 1, "landing ripple fires once per run");
    CHECK(restRipples == 1, "rest ripple fires once per run");
    CHECK(wrapped, "flow phases stay wrapped");
  }

  // 7) 色: 待機中はパワーに関係なく落ち着いた色、ゲージ中は熱い色へ寄る
  {
    const GuidePalette idleLow = PaletteFor(0.1f, false);
    const GuidePalette idleHigh = PaletteFor(1.0f, false);
    CHECK(idleLow.nearColor.r == idleHigh.nearColor.r &&
              idleLow.nearColor.b == idleHigh.nearColor.b,
          "idle palette ignores power");
    const GuidePalette hot = PaletteFor(1.0f, true);
    CHECK(hot.nearColor.r > idleHigh.nearColor.r &&
              hot.nearColor.b < idleHigh.nearColor.b,
          "charging palette warms up with power");
    CHECK(MoodFor(game::components::TerrainMaterial::Water) ==
              SurfaceMood::Danger,
          "water is a danger surface");
    CHECK(std::fabs(EaseOutBack(1.0f) - 1.0f) < 1.0e-5f &&
              EaseOutBack(0.0f) < 1.0e-5f,
          "pop easing starts at 0 and ends at 1");
  }

  // 8) 壁: z=20 の面より先へは進まず、跳ね返って手前に止まる
  {
    struct WallAtZ final : IShotGuideObstacles {
      float z = 20.0f;
      bool Resolve(BallSimState &state, float radius,
                   float restitution) const override {
        if (state.position.z + radius <= z) return false;
        state.position.z = z - radius;
        if (state.velocity.z > 0.0f) {
          state.velocity.z = -state.velocity.z * restitution;
        }
        return true;
      }
    } wall;
    CHECK(lofted.totalDistance > 25.0f, "without a wall the shot passes z=20");
    const ShotGuidePath blocked = BuildShotGuidePath(wedge, nullptr, &wall);
    CHECK(blocked.valid && blocked.hitObstacle, "the wall is hit");
    float maxZ = 0.0f;
    for (const auto &point : blocked.points) {
      maxZ = std::max(maxZ, point.position.z);
    }
    CHECK(maxZ <= 20.0f + 1.0e-3f, "no point goes through the wall");
    CHECK(blocked.Rest().position.z < 20.0f - blocked.ballRadius,
          "the ball bounces back from the wall");
    CHECK(!lofted.hitObstacle, "no obstacle hit without obstacles");
  }

  // 9) 止まりそうな範囲: ぶれの規則どおりの大きさで、予測の手前寄りに置かれる
  {
    ShotGuideSpread spread;
    spread.side = 0.05f;
    spread.shortBy = 0.05f;
    const ShotGuideZone zone =
        BuildShotGuideZone(lofted, spread, nullptr, wedge.flatGround);
    CHECK(zone.valid && zone.rim.size() == 32, "zone has a full rim");
    CHECK(std::fabs(zone.halfWidth - lofted.totalDistance * 0.05f) < 1.0e-3f,
          "zone width follows the side spread");
    CHECK(zone.center.z < lofted.Rest().position.z &&
              zone.center.z + zone.halfLength >=
                  lofted.Rest().position.z - 1.0e-3f,
          "zone sits short of the prediction but still covers it");
    const float shortest = lofted.Rest().position.z - lofted.totalDistance * 0.05f;
    CHECK(zone.center.z - zone.halfLength <= shortest + 1.0e-3f,
          "zone covers the shortest expected shot");

    // パターのような短いショットでも、見える大きさを保つ
    ShotGuideRequest putt = base;
    putt.speed = 3.0f;
    const ShotGuidePath shortPath = BuildShotGuidePath(putt, nullptr);
    const ShotGuideZone puttZone =
        BuildShotGuideZone(shortPath, spread, nullptr, putt.flatGround);
    CHECK(puttZone.valid && puttZone.halfWidth >= spread.minHalfSize &&
              puttZone.halfLength >= spread.minHalfSize,
          "short shots keep a visible zone");

    ShotGuideStyle style;
    CHECK(GuideScaleFor(style, 1.0f) == style.nearScale &&
              GuideScaleFor(style, 200.0f) == 1.0f &&
              GuideScaleFor(style, 10.0f) > style.nearScale &&
              GuideScaleFor(style, 10.0f) < 1.0f,
          "display scale shrinks for short shots");
  }

  std::cout << "All shot guide tests passed.\n";
  return 0;
}
