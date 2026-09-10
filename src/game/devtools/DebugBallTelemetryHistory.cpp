#include "DebugBallTelemetryHistory.h"

#include "../../ecs/World.h"
#include "../components/PhysicsComponents.h"
#include "../components/WikiComponents.h"
#include <cmath>

namespace game::debug {
namespace {

template <typename Selector>
std::vector<float> SelectSeries(
    const std::vector<DebugBallTelemetrySample> &samples, Selector selector) {
  std::vector<float> values;
  values.reserve(samples.size());
  for (const auto &sample : samples) {
    values.push_back(selector(sample));
  }
  return values;
}

float Length(const DirectX::XMFLOAT3 &value) {
  return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

} // namespace

bool DebugBallTelemetryHistory::Update(ecs::World &world,
                                       bool simulationAdvanced) {
  if (!simulationAdvanced) {
    return false;
  }
  const auto *state = world.GetGlobal<game::components::GolfGameState>();
  if (!state) {
    return false;
  }
  const auto *body = world.Get<game::components::RigidBody>(
      static_cast<ecs::Entity>(state->ballEntity));
  if (!body) {
    return false;
  }
  m_samples.push_back({Length(body->velocity), body->velocity.y,
                       Length(body->angularVelocity),
                       state->isBallGrounded ? 1.0f : 0.0f});
  if (m_samples.size() > kMaximumFrames) {
    m_samples.erase(m_samples.begin());
  }
  return true;
}

void DebugBallTelemetryHistory::Clear() { m_samples.clear(); }

std::vector<float> DebugBallTelemetryHistory::SpeedSeries() const {
  return SelectSeries(m_samples, [](const auto &sample) { return sample.speed; });
}

std::vector<float>
DebugBallTelemetryHistory::VerticalVelocitySeries() const {
  return SelectSeries(m_samples,
                      [](const auto &sample) { return sample.verticalVelocity; });
}

std::vector<float> DebugBallTelemetryHistory::AngularSpeedSeries() const {
  return SelectSeries(m_samples,
                      [](const auto &sample) { return sample.angularSpeed; });
}

std::vector<float> DebugBallTelemetryHistory::GroundedSeries() const {
  return SelectSeries(m_samples,
                      [](const auto &sample) { return sample.grounded; });
}

} // namespace game::debug
