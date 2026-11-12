#include "src/game/utils/ShotGaugeRules.h"
#include <cmath>
#include <cstdlib>
#include <iostream>

#define CHECK_TRUE(condition, message)                                          \
  do {                                                                         \
    if (!(condition)) {                                                         \
      std::cerr << "[FAIL] " << message << "\n";                              \
      std::exit(1);                                                            \
    }                                                                          \
    std::cout << "[PASS] " << message << "\n";                                \
  } while (0)

#define CHECK_CLOSE(actual, expected, eps, message)                             \
  do {                                                                         \
    if (std::fabs((actual) - (expected)) > (eps)) {                            \
      std::cerr << "[FAIL] " << message << " (expected " << (expected)         \
                << ", got " << (actual) << ")\n";                             \
      std::exit(1);                                                            \
    }                                                                          \
    std::cout << "[PASS] " << message << "\n";                                \
  } while (0)

int main() {
  using game::components::ShotJudgement;
  using game::utils::EvaluateImpactJudgement;
  using game::utils::GetImpactZoneVisualWidth;

  const game::components::ShotState shotState;
  CHECK_CLOSE(shotState.powerGaugeSpeed, 1.0f, 0.0001f,
              "normal power gauge speed is 1.0");
  CHECK_CLOSE(shotState.impactGaugeSpeed, 1.0f, 0.0001f,
              "normal impact gauge speed is 1.0");

  CHECK_TRUE(EvaluateImpactJudgement(0.5f) == ShotJudgement::Special,
             "center impact is Special");
  CHECK_TRUE(EvaluateImpactJudgement(0.5f + game::ui::kThresholdSpecial +
                                     0.0001f) ==
                 ShotJudgement::Great,
             "impact just outside Special is Great");
  CHECK_TRUE(EvaluateImpactJudgement(0.5f + game::ui::kThresholdGreat +
                                     0.0001f) ==
                 ShotJudgement::Nice,
             "impact just outside Great is Nice");
  CHECK_TRUE(EvaluateImpactJudgement(0.5f + game::ui::kThresholdNice +
                                     0.0001f) ==
                 ShotJudgement::Miss,
             "impact just outside Nice is Miss");

  CHECK_CLOSE(GetImpactZoneVisualWidth(game::ui::kThresholdSpecial),
              game::ui::kThresholdSpecial * 2.0f, 0.0001f,
              "Special visual zone covers both sides of threshold");
  CHECK_CLOSE(GetImpactZoneVisualWidth(game::ui::kThresholdGreat),
              game::ui::kThresholdGreat * 2.0f, 0.0001f,
              "Great visual zone covers both sides of threshold");
  CHECK_CLOSE(GetImpactZoneVisualWidth(game::ui::kThresholdNice),
              game::ui::kThresholdNice * 2.0f, 0.0001f,
              "Nice visual zone covers both sides of threshold");

  using game::utils::ApplyImpactSideError;
  const DirectX::XMFLOAT3 north{0.0f, 0.0f, 1.0f};
  const DirectX::XMFLOAT3 east{1.0f, 0.0f, 0.0f};
  const auto straight =
      ApplyImpactSideError(north, ShotJudgement::Special, 0.49f, 0.5f);
  CHECK_TRUE(straight.x == 0.0f && straight.z == 1.0f,
             "Special flies straight along the aim");
  const auto great =
      ApplyImpactSideError(north, ShotJudgement::Great, 0.47f, 0.5f);
  CHECK_TRUE(great.x < 0.0f && great.x > -0.02f,
             "Great veers only slightly");
  CHECK_TRUE(ApplyImpactSideError(north, ShotJudgement::Miss, 0.2f, 0.5f).x < 0.0f,
             "stopping before the center pulls the ball left");
  CHECK_TRUE(ApplyImpactSideError(north, ShotJudgement::Miss, 0.8f, 0.5f).x > 0.0f,
             "stopping past the center pushes the ball right");
  CHECK_TRUE(ApplyImpactSideError(east, ShotJudgement::Miss, 0.8f, 0.5f).z < 0.0f,
             "right stays relative to the aim when facing east");
  CHECK_TRUE(ApplyImpactSideError(east, ShotJudgement::Miss, 0.2f, 0.5f).z > 0.0f,
             "left stays relative to the aim when facing east");
  const auto missed = ApplyImpactSideError(north, ShotJudgement::Miss, 0.8f, 0.5f);
  const auto niced = ApplyImpactSideError(north, ShotJudgement::Nice, 0.6f, 0.5f);
  CHECK_TRUE(missed.x > niced.x && niced.x > 0.0f,
             "Miss veers further than Nice");
  CHECK_CLOSE(std::hypot(missed.x, missed.z), 1.0f, 0.0001f,
              "side error keeps the horizontal length");

  // 遅れの判定(針の向きも見る)
  using game::utils::IsImpactLate;
  CHECK_TRUE(!IsImpactLate(0.4f, 0.5f, 1.0f),
             "stopping before the center on the way up is early");
  CHECK_TRUE(IsImpactLate(0.6f, 0.5f, 1.0f),
             "stopping past the center is late");
  CHECK_TRUE(!IsImpactLate(0.45f, 0.5f, -1.0f),
             "left of a mid center stays early even on the return pass");
  CHECK_TRUE(IsImpactLate(0.97f, 1.0f, -1.0f),
             "full shot: stopping after the needle bounced back is late");
  CHECK_TRUE(!IsImpactLate(0.97f, 1.0f, 1.0f),
             "full shot: stopping before the end is early");
  CHECK_TRUE(ApplyImpactSideError(north, ShotJudgement::Miss, true).x > 0.0f &&
                 ApplyImpactSideError(north, ShotJudgement::Miss, false).x < 0.0f,
             "late pushes right, early pulls left");

  // 飛距離
  using game::utils::GetJudgementDistanceMultiplier;
  CHECK_CLOSE(GetJudgementDistanceMultiplier(ShotJudgement::Special), 1.0f,
              0.0001f, "Special does not overshoot the aim");
  CHECK_CLOSE(GetJudgementDistanceMultiplier(ShotJudgement::Great), 1.0f,
              0.0001f, "Great keeps the aimed distance");
  CHECK_CLOSE(GetJudgementDistanceMultiplier(ShotJudgement::Nice), 0.95f,
              0.0001f, "Nice is 5% short");
  CHECK_CLOSE(GetJudgementDistanceMultiplier(ShotJudgement::Miss), 0.8f,
              0.0001f, "Miss is 20% short");

  // パーフェクト中心
  using game::utils::ResolveImpactPerfectCenter;
  CHECK_CLOSE(ResolveImpactPerfectCenter(1.0f), 1.0f, 0.0001f,
              "full power keeps the center at the end");
  CHECK_CLOSE(ResolveImpactPerfectCenter(0.0f), game::utils::kImpactCenterMin,
              0.0001f, "the center never sits where the needle starts");
  CHECK_TRUE(EvaluateImpactJudgement(1.0f, ResolveImpactPerfectCenter(1.0f)) ==
                 ShotJudgement::Special,
             "stopping at the end of a full shot is Special");

  const auto driverTicks = game::utils::GetRulerTicks(260.0f);
  CHECK_TRUE(driverTicks.minorYards == 10 && driverTicks.majorYards == 50,
             "driver ruler uses 10y ticks with 50y labels");
  const auto putterTicks = game::utils::GetRulerTicks(10.0f);
  CHECK_TRUE(putterTicks.minorYards == 1 && putterTicks.majorYards == 2,
             "short putter ruler uses 1y ticks with 2y labels");
  for (float yards : {3.0f, 12.0f, 45.0f, 100.0f, 150.0f, 260.0f, 300.0f, 600.0f}) {
    const auto ticks = game::utils::GetRulerTicks(yards);
    CHECK_TRUE(yards / ticks.majorYards <= 6.0f,
               "ruler keeps at most six labelled ticks");
    CHECK_TRUE(ticks.majorYards % ticks.minorYards == 0,
               "labelled ticks line up with minor ticks");
  }
  CHECK_TRUE(game::utils::GetRulerTicks(0.0f).majorYards == 0,
             "ruler without a carry distance has no ticks");

  std::cout << "All shot gauge rule tests passed!\n";
  return 0;
}
