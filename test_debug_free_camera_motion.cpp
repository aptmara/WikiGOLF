#include "src/game/devtools/DebugFreeCameraMotion.h"
#include <cmath>
#include <iostream>

int main() {
  game::debug::DebugFreeCameraPose pose;
  game::debug::DebugFreeCameraInput input;
  input.moveForward = 1.0f;
  pose = game::debug::StepDebugFreeCamera(pose, input, 0.5f, 10.0f);
  if (std::fabs(pose.position.z - 5.0f) > 0.0001f) {
    std::cerr << "Forward movement failed\n";
    return 1;
  }
  input = {};
  input.moveRight = 1.0f;
  input.speedMultiplier = 2.0f;
  pose = game::debug::StepDebugFreeCamera(pose, input, 0.5f, 10.0f);
  if (std::fabs(pose.position.x - 10.0f) > 0.0001f) {
    std::cerr << "Fast strafe failed\n";
    return 1;
  }
  input = {};
  input.pitchDelta = 10.0f;
  pose = game::debug::StepDebugFreeCamera(pose, input, 0.0f, 10.0f);
  if (pose.pitch > 1.5533f) {
    std::cerr << "Pitch clamp failed\n";
    return 1;
  }
  return 0;
}
