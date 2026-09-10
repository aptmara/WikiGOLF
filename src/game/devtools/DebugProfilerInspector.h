#pragma once

#include "DebugProfilerHistory.h"
#include <string>

namespace game::debug {

class DebugProfilerInspector {
public:
  void Draw();

private:
  DebugProfilerHistory m_history;
  std::string m_cpuScope;
  std::string m_gpuScope;
  std::string m_counter;
};

} // namespace game::debug
