#pragma once

#include "DebugGameplayCheckpoint.h"
#include <string>

namespace core {
struct GameContext;
}

namespace game::debug {

class DebugCheckpointInspector {
public:
  void Draw(core::GameContext &ctx);

private:
  DebugGameplayCheckpoint m_checkpoint;
  std::string m_result;
};

} // namespace game::debug
