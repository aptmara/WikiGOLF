#pragma once

#include "DebugSlopeRules.h"

namespace ecs {
class World;
}

namespace game::debug {

struct DebugSlopeStatus {
  bool available = false;
  DebugSlopeEvaluation evaluation;
};

DebugSlopeStatus CaptureSlopeStatus(ecs::World &world);

} // namespace game::debug
