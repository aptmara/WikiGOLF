#pragma once
/**
 * @file ShotGuidePath.h
 * @brief ショット予測の「道すじ」を表すデータと、その計算
 *
 * 予測線・着地点・向きの表示・ミニマップは、どれもこの ShotGuidePath だけを
 * 読んで描く。描画側はクラブやゲージを知らず、計算側は描画を知らない。
 *
 * 計算は実ショットと同じ StepBallSimulation を、実ショットと同じ刻みで
 * 静止するまで進める。画面のフレームレートには左右されない。
*/

#include "TrajectorySimulation.h"
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace game::guide {

/** @brief 道すじ上の1点*/
struct ShotGuidePoint {
  DirectX::XMFLOAT3 position{0.0f, 0.0f, 0.0f}; ///< ボール中心の位置
  float arc = 0.0f;      ///< 打ち出しからの道のり[m]
  float time = 0.0f;     ///< 打ち出しからの時間[秒]
  bool grounded = false; ///< 地面に付いているか（転がり区間）
};

/** @brief 予測の道すじ全体*/
struct ShotGuidePath {
  std::vector<ShotGuidePoint> points;
  bool valid = false;   ///< 2点以上あって描けるか
  bool settled = false; ///< 上限時間までに静止したか
  bool hitObstacle = false; ///< 途中で壁や障害物にぶつかったか

  bool hasLanding = false;  ///< 空中から最初に着地した点があるか（パターには無い）
  size_t landingIndex = 0;  ///< 最初の着地点の points 上の位置
  size_t apexIndex = 0;     ///< 最高点の points 上の位置

  DirectX::XMFLOAT3 startNormal{0.0f, 1.0f, 0.0f};   ///< 打ち出し地点の地面の向き
  DirectX::XMFLOAT3 landingNormal{0.0f, 1.0f, 0.0f}; ///< 着地点の地面の向き
  DirectX::XMFLOAT3 restNormal{0.0f, 1.0f, 0.0f};    ///< 静止点の地面の向き
  game::components::TerrainMaterial landingMaterial =
      game::components::TerrainMaterial::Fairway;
  game::components::TerrainMaterial restMaterial =
      game::components::TerrainMaterial::Fairway;

  float totalArc = 0.0f;      ///< 道のりの合計[m]
  float totalTime = 0.0f;     ///< 静止までの時間[秒]
  float carryDistance = 0.0f; ///< 最初の着地までの水平距離[m]
  float totalDistance = 0.0f; ///< 静止点までの水平距離[m]
  float ballRadius = game::physics::kBallRadius;

  const ShotGuidePoint &Start() const { return points.front(); }
  const ShotGuidePoint &Rest() const { return points.back(); }
  const ShotGuidePoint &Landing() const { return points[landingIndex]; }
};

/** @brief 道すじの計算に渡す条件*/
struct ShotGuideRequest {
  DirectX::XMFLOAT3 start{0.0f, 0.0f, 0.0f};     ///< ボールの位置
  DirectX::XMFLOAT3 direction{0.0f, 0.0f, 1.0f}; ///< 水平の打ち出し方向
  float speed = 0.0f;          ///< 初速
  float launchAngleDeg = 0.0f; ///< 打ち出し角
  game::physics::BallPhysicsParams ball;
  game::physics::WindParams wind;
  game::physics::FlatGroundParams flatGround; ///< 地形が無いときの地面

  bool operator==(const ShotGuideRequest &o) const {
    return start.x == o.start.x && start.y == o.start.y &&
           start.z == o.start.z && direction.x == o.direction.x &&
           direction.y == o.direction.y && direction.z == o.direction.z &&
           speed == o.speed && launchAngleDeg == o.launchAngleDeg &&
           ball.drag == o.ball.drag && ball.restitution == o.ball.restitution &&
           ball.mass == o.ball.mass && ball.ballRadius == o.ball.ballRadius &&
           ball.rollingFrictionScale == o.ball.rollingFrictionScale &&
           wind.windSpeed == o.wind.windSpeed &&
           wind.windDirection.x == o.wind.windDirection.x &&
           wind.windDirection.y == o.wind.windDirection.y &&
           flatGround.enabled == o.flatGround.enabled &&
           flatGround.groundY == o.flatGround.groundY;
  }
};

/**
 * @brief 壁や障害物など、地形以外にボールがぶつかるもの。
 * @details 道すじの計算は「何にぶつかるか」を知らない。ぶつかる相手を知っている側が
 *          これを実装して渡す。渡さなければ地形だけを相手にする。
*/
class IShotGuideObstacles {
public:
  virtual ~IShotGuideObstacles() = default;
  /**
   * @brief ぶつかっていれば押し戻して跳ね返す。
   * @return ぶつかったら true
  */
  virtual bool Resolve(game::physics::BallSimState &state, float ballRadius,
                       float ballRestitution) const = 0;
};

/** @brief 点の間引き方*/
struct ShotGuidePathOptions {
  float maxSeconds = 20.0f;     ///< これを超えても止まらなければ打ち切る
  float minSpacing = 0.06f;     ///< これより近い点は置かない[m]
  float maxSpacing = 2.4f;      ///< まっすぐでもこの間隔では点を置く[m]
  float maxTurnDegrees = 2.0f;  ///< 進行方向がこれだけ変わったら点を置く
  size_t maxPoints = 200;       ///< 点の上限（超えたら均等に間引く）
  float minAirSeconds = 0.12f;  ///< これより短い浮きは「着地」に数えない
  float minAirHeight = 0.10f;   ///< これより低い浮きは「着地」に数えない[m]
};

namespace path_detail {

inline float HorizontalDistance(const DirectX::XMFLOAT3 &a,
                                const DirectX::XMFLOAT3 &b) {
  const float dx = a.x - b.x;
  const float dz = a.z - b.z;
  return std::sqrt(dx * dx + dz * dz);
}

inline float Distance(const DirectX::XMFLOAT3 &a, const DirectX::XMFLOAT3 &b) {
  const float dx = a.x - b.x;
  const float dy = a.y - b.y;
  const float dz = a.z - b.z;
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

inline float GroundHeight(game::systems::WikiTerrainSystem *terrain,
                          const game::physics::FlatGroundParams &flat, float x,
                          float z) {
  return game::physics::ToVisualSurfaceHeight(
      terrain ? terrain->GetHeight(x, z) : flat.groundY);
}

/** @brief 地面の向き。飾りを寝かせる用なので、物理より広い幅でならして取る*/
inline DirectX::XMFLOAT3 GroundNormal(game::systems::WikiTerrainSystem *terrain,
                                      float x, float z) {
  if (!terrain) {
    return {0.0f, 1.0f, 0.0f};
  }
  constexpr float kStep = 0.35f;
  const float hx0 = terrain->GetHeight(x - kStep, z);
  const float hx1 = terrain->GetHeight(x + kStep, z);
  const float hz0 = terrain->GetHeight(x, z - kStep);
  const float hz1 = terrain->GetHeight(x, z + kStep);
  DirectX::XMFLOAT3 n{-(hx1 - hx0) / (2.0f * kStep), 1.0f,
                      -(hz1 - hz0) / (2.0f * kStep)};
  const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
  if (!std::isfinite(len) || len < 1.0e-5f) {
    return {0.0f, 1.0f, 0.0f};
  }
  return {n.x / len, n.y / len, n.z / len};
}

} // namespace path_detail

/**
 * @brief 条件からショットの道すじを計算する。
 * @param terrain 地形。nullptr なら request.flatGround の平面を使う。
 * @param obstacles 壁や障害物。nullptr なら地形だけを相手にする。
*/
inline ShotGuidePath
BuildShotGuidePath(const ShotGuideRequest &request,
                   game::systems::WikiTerrainSystem *terrain,
                   const IShotGuideObstacles *obstacles = nullptr,
                   const ShotGuidePathOptions &options = {}) {
  using namespace DirectX;
  using namespace game::physics;

  ShotGuidePath path;
  path.ballRadius = request.ball.ballRadius;
  if (!(request.speed > 0.0f) || !std::isfinite(request.speed)) {
    return path;
  }

  const bool hasGround = terrain != nullptr || request.flatGround.enabled;
  auto terrainData = terrain ? terrain->GetTerrainData() : nullptr;

  BallSimState state;
  state.position = request.start;
  if (hasGround) {
    state.position.y = path_detail::GroundHeight(terrain, request.flatGround,
                                                 request.start.x,
                                                 request.start.z) +
                       request.ball.ballRadius;
  }
  {
    XMVECTOR dir = XMVectorSet(request.direction.x, 0.0f, request.direction.z, 0.0f);
    if (XMVectorGetX(XMVector3LengthSq(dir)) < 1.0e-8f) {
      dir = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
    }
    dir = XMVector3Normalize(dir);
    const float rad = XMConvertToRadians(request.launchAngleDeg);
    XMVECTOR vel = XMVectorScale(dir, std::cos(rad) * request.speed);
    vel = XMVectorSetY(vel, std::sin(rad) * request.speed);
    XMStoreFloat3(&state.velocity, vel);
  }

  // 実ショットの距離計算(SimulateCarryDistance)と同じ刻み
  const float subDt = kMaxSimulationDeltaTime / 4.0f;
  const int maxSteps =
      std::max(1, static_cast<int>(options.maxSeconds / subDt));
  const float cosTurn =
      std::cos(XMConvertToRadians(std::max(0.1f, options.maxTurnDegrees)));

  path.points.reserve(256);
  path.points.push_back({state.position, 0.0f, 0.0f, true});

  XMFLOAT3 previous = state.position;
  XMFLOAT3 emittedDir = state.velocity; // 最後に点を置いたときの進行方向
  float arc = 0.0f;
  float apexY = state.position.y;
  bool airborne = false;
  float airSeconds = 0.0f;
  float airPeak = 0.0f;

  auto emit = [&](float time, bool grounded) {
    path.points.push_back({state.position, arc, time, grounded});
    emittedDir = state.velocity;
    if (state.position.y > apexY) {
      apexY = state.position.y;
      path.apexIndex = path.points.size() - 1;
    }
  };

  for (int step = 1; step <= maxSteps; ++step) {
    StepBallSimulation(state, terrain, request.flatGround, request.ball,
                       request.wind, subDt);
    const bool hitObstacle =
        obstacles && obstacles->Resolve(state, request.ball.ballRadius,
                                        request.ball.restitution);
    if (hitObstacle) {
      path.hitObstacle = true;
    }
    if (!std::isfinite(state.position.x) || !std::isfinite(state.position.y) ||
        !std::isfinite(state.position.z)) {
      break;
    }
    const float time = static_cast<float>(step) * subDt;
    arc += path_detail::Distance(state.position, previous);
    previous = state.position;

    bool landedNow = false;
    if (!state.isGrounded) {
      airborne = true;
      airSeconds += subDt;
      if (hasGround) {
        const float ground = path_detail::GroundHeight(
            terrain, request.flatGround, state.position.x, state.position.z);
        airPeak = std::max(airPeak, state.position.y -
                                        request.ball.ballRadius - ground);
      }
    } else if (airborne) {
      landedNow = !path.hasLanding && airSeconds >= options.minAirSeconds &&
                  airPeak >= options.minAirHeight;
      airborne = false;
      airSeconds = 0.0f;
      airPeak = 0.0f;
    }

    const XMVECTOR velocity = XMLoadFloat3(&state.velocity);
    const float speed = trajectory_detail::SafeVecLength(velocity);
    const bool settled = speed < 0.008f && state.isGrounded;

    const ShotGuidePoint &last = path.points.back();
    const float sinceLast = arc - last.arc;
    // ぶつかった所は折れ目になるので、必ず点を置く
    bool place = landedNow || settled ||
                 (hitObstacle && sinceLast > 1.0e-4f);
    if (!place && sinceLast >= options.minSpacing) {
      if (sinceLast >= options.maxSpacing ||
          last.grounded != state.isGrounded) {
        place = true;
      } else {
        const XMVECTOR prevDir = XMLoadFloat3(&emittedDir);
        const float prevLen = trajectory_detail::SafeVecLength(prevDir);
        if (prevLen > 1.0e-4f && speed > 1.0e-4f) {
          const float cosine =
              XMVectorGetX(XMVector3Dot(prevDir, velocity)) / (prevLen * speed);
          place = cosine < cosTurn;
        }
      }
    }
    if (place) {
      emit(time, state.isGrounded);
      if (landedNow) {
        path.hasLanding = true;
        path.landingIndex = path.points.size() - 1;
      }
    }
    if (settled) {
      path.settled = true;
      break;
    }
  }

  // 打ち切りで終わったときも、最後の位置までは線を引く
  if (path_detail::Distance(path.points.back().position, state.position) >
      1.0e-4f) {
    emit(static_cast<float>(maxSteps) * subDt, state.isGrounded);
  }

  // 点が多すぎるときは均等に間引く（始点・着地点・終点は残す）
  if (path.points.size() > options.maxPoints && options.maxPoints >= 3) {
    std::vector<ShotGuidePoint> reduced;
    reduced.reserve(options.maxPoints + 2);
    const size_t count = path.points.size();
    const float stride = static_cast<float>(count - 1) /
                         static_cast<float>(options.maxPoints - 1);
    size_t newLanding = 0;
    size_t newApex = 0;
    size_t lastTaken = static_cast<size_t>(-1);
    auto take = [&](size_t index) {
      if (index == lastTaken) return;
      if (lastTaken != static_cast<size_t>(-1) && index < lastTaken) return;
      lastTaken = index;
      if (index == path.landingIndex) newLanding = reduced.size();
      if (index == path.apexIndex) newApex = reduced.size();
      reduced.push_back(path.points[index]);
    };
    for (size_t i = 0; i < options.maxPoints; ++i) {
      const size_t index = std::min(
          count - 1, static_cast<size_t>(std::round(stride * static_cast<float>(i))));
      // 飛ばした区間に着地点・最高点があれば先に入れる
      for (size_t key : {std::min(path.landingIndex, path.apexIndex),
                         std::max(path.landingIndex, path.apexIndex)}) {
        if (key > 0 && key < index &&
            (lastTaken == static_cast<size_t>(-1) || key > lastTaken)) {
          take(key);
        }
      }
      take(index);
    }
    path.points = std::move(reduced);
    path.landingIndex = newLanding;
    path.apexIndex = newApex;
  }

  path.valid = path.points.size() >= 2 && path.points.back().arc > 1.0e-3f;
  if (!path.valid) {
    path.hasLanding = false;
    return path;
  }

  path.totalArc = path.points.back().arc;
  path.totalTime = path.points.back().time;
  const XMFLOAT3 &start = path.points.front().position;
  const XMFLOAT3 &rest = path.points.back().position;
  path.totalDistance = path_detail::HorizontalDistance(start, rest);
  path.startNormal = path_detail::GroundNormal(terrain, start.x, start.z);
  path.restNormal = path_detail::GroundNormal(terrain, rest.x, rest.z);
  path.restMaterial = trajectory_detail::SampleGroundMaterial(
      rest.x, rest.z, terrainData, request.flatGround);
  if (path.hasLanding) {
    const XMFLOAT3 &landing = path.points[path.landingIndex].position;
    path.carryDistance = path_detail::HorizontalDistance(start, landing);
    path.landingNormal =
        path_detail::GroundNormal(terrain, landing.x, landing.z);
    path.landingMaterial = trajectory_detail::SampleGroundMaterial(
        landing.x, landing.z, terrainData, request.flatGround);
  }
  return path;
}

/** @brief 道すじ上の1か所を取り出した結果*/
struct ShotGuideSample {
  DirectX::XMFLOAT3 position{0.0f, 0.0f, 0.0f};
  DirectX::XMFLOAT3 tangent{0.0f, 0.0f, 1.0f}; ///< 進行方向（正規化済み）
  bool grounded = false;
};

/** @brief 道のり arc の位置を取り出す（範囲外は端に丸める）*/
inline ShotGuideSample SamplePathAtArc(const ShotGuidePath &path, float arc) {
  ShotGuideSample sample;
  if (path.points.empty()) {
    return sample;
  }
  if (path.points.size() == 1) {
    sample.position = path.points.front().position;
    return sample;
  }
  arc = std::clamp(arc, 0.0f, path.points.back().arc);
  auto upper = std::lower_bound(
      path.points.begin() + 1, path.points.end(), arc,
      [](const ShotGuidePoint &p, float value) { return p.arc < value; });
  if (upper == path.points.end()) {
    upper = path.points.end() - 1;
  }
  const ShotGuidePoint &b = *upper;
  const ShotGuidePoint &a = *(upper - 1);
  const float span = b.arc - a.arc;
  const float t = span > 1.0e-6f ? (arc - a.arc) / span : 0.0f;
  sample.position = {a.position.x + (b.position.x - a.position.x) * t,
                     a.position.y + (b.position.y - a.position.y) * t,
                     a.position.z + (b.position.z - a.position.z) * t};
  const float dx = b.position.x - a.position.x;
  const float dy = b.position.y - a.position.y;
  const float dz = b.position.z - a.position.z;
  const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
  if (len > 1.0e-6f) {
    sample.tangent = {dx / len, dy / len, dz / len};
  }
  sample.grounded = a.grounded && b.grounded;
  return sample;
}

/** @brief 打ち出しから time 秒後にいる道のりを返す（範囲外は端に丸める）*/
inline float PathArcAtTime(const ShotGuidePath &path, float time) {
  if (path.points.size() < 2) {
    return 0.0f;
  }
  if (time <= 0.0f) {
    return 0.0f;
  }
  if (time >= path.points.back().time) {
    return path.points.back().arc;
  }
  auto upper = std::lower_bound(
      path.points.begin() + 1, path.points.end(), time,
      [](const ShotGuidePoint &p, float value) { return p.time < value; });
  const ShotGuidePoint &b = *upper;
  const ShotGuidePoint &a = *(upper - 1);
  const float span = b.time - a.time;
  const float t = span > 1.0e-6f ? (time - a.time) / span : 0.0f;
  return a.arc + (b.arc - a.arc) * t;
}

} // namespace game::guide
