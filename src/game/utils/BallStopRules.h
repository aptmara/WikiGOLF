#pragma once

#include "../components/PhysicsComponents.h"

namespace game::utils {

inline constexpr float kBallStopSpeedThreshold = 0.1f;
inline constexpr float kBallStopConfirmSeconds = 0.5f;

inline bool UpdateBallStopConfirmation(float speed, bool isGrounded, float dt,
                                       float &candidateSeconds) {
  const bool isStopCandidate =
      isGrounded && speed < kBallStopSpeedThreshold;
  if (!isStopCandidate) {
    candidateSeconds = 0.0f;
    return false;
  }

  candidateSeconds += dt;
  return candidateSeconds >= kBallStopConfirmSeconds;
}

inline void FreezeBall(game::components::RigidBody &body) {
  body.velocity = {0.0f, 0.0f, 0.0f};
  body.acceleration = {0.0f, 0.0f, 0.0f};
  body.angularVelocity = {0.0f, 0.0f, 0.0f};
  body.isStatic = true;
}

} // namespace game::utils
