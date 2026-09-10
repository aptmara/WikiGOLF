#pragma once

#include <cstddef>
#include <vector>

namespace ecs {
class World;
}

namespace game::debug {

struct DebugBallTelemetrySample {
  float speed = 0.0f;
  float verticalVelocity = 0.0f;
  float angularSpeed = 0.0f;
  float grounded = 0.0f;
};

class DebugBallTelemetryHistory {
public:
  static constexpr std::size_t kMaximumFrames = 240;

  bool Update(ecs::World &world, bool simulationAdvanced);
  void Clear();
  const std::vector<DebugBallTelemetrySample> &Samples() const {
    return m_samples;
  }
  std::vector<float> SpeedSeries() const;
  std::vector<float> VerticalVelocitySeries() const;
  std::vector<float> AngularSpeedSeries() const;
  std::vector<float> GroundedSeries() const;

private:
  std::vector<DebugBallTelemetrySample> m_samples;
};

} // namespace game::debug
