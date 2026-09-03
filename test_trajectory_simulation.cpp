#include "src/game/utils/TrajectorySimulation.h"
#include <cmath>
#include <iostream>

// このテストは terrainSystem=nullptr(平坦地面)の経路しか通らないが、
// WikiTerrainSystem::GetHeight の呼び出し自体はコンパイル時に生成されるため
// リンクが必要になる。WikiTerrainSystem.cppは重いグラフィック/リソース依存
// を引きずるため、テスト用にこの1関数だけスタブ実装を提供する。
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

#define CHECK_CLOSE(actual, expected, eps, message)                          \
  CHECK(std::fabs((actual) - (expected)) <= (eps), message)

int main() {
  using namespace game::physics;
  using game::utils::LookupSpeedForDistance;

  const DirectX::XMFLOAT3 dir = {0.0f, 0.0f, 1.0f};
  const DirectX::XMFLOAT3 startPos = {0.0f, 0.0f, 0.0f};

  FlatGroundParams flat;
  flat.enabled = true;
  flat.groundY = 0.0f;
  flat.material = game::components::TerrainMaterial::Fairway;
  WindParams noWind;
  BallPhysicsParams ballParams;

  // 1) 初速0なら飛距離もほぼ0
  {
    auto result = SimulateCarryDistance(0.0f, 12.0f, dir, startPos, nullptr,
                                        flat, ballParams, noWind);
    CHECK(result.horizontalDistance < 0.05f,
          "Zero initial speed yields near-zero carry distance");
  }

  // 2) 初速が大きいほど飛距離も大きい(単調性)
  {
    auto low = SimulateCarryDistance(20.0f, 12.0f, dir, startPos, nullptr,
                                     flat, ballParams, noWind);
    auto high = SimulateCarryDistance(40.0f, 12.0f, dir, startPos, nullptr,
                                      flat, ballParams, noWind);
    CHECK(high.horizontalDistance > low.horizontalDistance,
          "Higher initial speed yields greater carry distance");
  }

  // 3) キャリー距離テーブルの往復精度: 目標飛距離->初速->飛距離が目標に近い
  {
    const float maxSpeed = 130.0f;
    const float launchAngle = 12.0f;
    auto table = BuildCarryDistanceTable(maxSpeed, launchAngle, ballParams);

    CHECK(table.speeds.size() == table.distances.size(),
          "Carry distance table has matching speed/distance sample counts");
    CHECK(table.distances.back() > 0.0f,
          "Full-power sample yields a positive base carry distance");

    const float targetDistance = table.distances.back() * 0.5f;
    const float solvedSpeed = LookupSpeedForDistance(table, targetDistance);
    auto verify = SimulateCarryDistance(solvedSpeed, launchAngle, dir,
                                        startPos, nullptr, flat, ballParams,
                                        noWind);

    // テーブルは12分割の線形補間のため、往復誤差は基準飛距離の5%以内に収まる。
    const float tolerance = table.distances.back() * 0.05f;
    CHECK_CLOSE(verify.horizontalDistance, targetDistance, tolerance,
                "Distance->speed->distance round trip stays within tolerance");
  }

  // 4) 目標飛距離0以下では初速0を返す
  {
    game::utils::CarryDistanceTable table;
    table.speeds = {0.0f, 50.0f, 100.0f};
    table.distances = {0.0f, 100.0f, 200.0f};
    CHECK(LookupSpeedForDistance(table, 0.0f) == 0.0f,
          "Zero target distance resolves to zero speed");
    CHECK(LookupSpeedForDistance(table, -10.0f) == 0.0f,
          "Negative target distance clamps to zero speed");
  }

  // 5) テーブル範囲を超える目標飛距離は最大速度にクランプされる
  {
    game::utils::CarryDistanceTable table;
    table.speeds = {0.0f, 50.0f, 100.0f};
    table.distances = {0.0f, 100.0f, 200.0f};
    CHECK(LookupSpeedForDistance(table, 500.0f) == 100.0f,
          "Out-of-range target distance clamps to the table's max speed");
  }

  std::cout << "All trajectory simulation tests passed.\n";

  // 参考: 実際のクラブ設定での基準飛距離を出力(ClubController.cppの値と同じ)
  struct ClubSpec { const char* name; float maxPower; float angle; float frictionScale; };
  ClubSpec clubs[] = {
      {"Driver", 130.0f, 12.0f, 3.0f}, {"3W", 115.0f, 14.0f, 2.5f},
      {"5W", 100.0f, 16.0f, 2.0f},     {"5I", 85.0f, 20.0f, 1.5f},
      {"7I", 70.0f, 24.0f, 1.2f},      {"9I", 55.0f, 28.0f, 1.0f},
      {"PW", 40.0f, 32.0f, 1.5f},      {"SW", 25.0f, 38.0f, 2.5f},
      {"Putter", 10.0f, 0.0f, 1.0f},
  };
  std::cout << "\n[Reference] baseCarryDistance per club:\n";
  for (const auto& c : clubs) {
    BallPhysicsParams p;
    p.rollingFrictionScale = c.frictionScale;
    auto t = BuildCarryDistanceTable(c.maxPower, c.angle, p);
    std::cout << "  " << c.name << ": " << t.distances.back() << "\n";
  }

  return 0;
}
