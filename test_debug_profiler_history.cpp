#include "src/game/devtools/DebugProfilerHistory.h"
#include <iostream>

int main() {
  game::debug::DebugProfilerHistory history;
  core::ProfilerFrameSnapshot first;
  first.frameIndex = 1;
  first.cpuFrameMs = 4.0;
  first.cpuScopes.push_back({"Physics.Update", 2.0, 1.5, 1});
  first.gpuScopes.push_back({"GPU.Frame", 3.0});
  first.counters.push_back({"Physics.SubSteps", 2.0});
  history.Update(first);

  first.cpuFrameMs = 5.0;
  first.gpuScopes[0].milliseconds = 3.5;
  history.Update(first);
  if (history.Frames().size() != 1 || history.CpuFrameSeries()[0] != 5.0f ||
      history.GpuScopeSeries("GPU.Frame")[0] != 3.5f ||
      history.CpuScopeSeries("Physics.Update", false)[0] != 1.5f ||
      history.CounterSeries("Physics.SubSteps")[0] != 2.0f) {
    std::cerr << "Profiler series or same-frame replacement failed\n";
    return 1;
  }

  core::ProfilerFrameSnapshot second;
  second.frameIndex = 2;
  history.Update(second);
  first.gpuScopes[0].milliseconds = 4.0;
  history.Update(first);
  if (history.Frames().size() != 2 ||
      history.GpuScopeSeries("GPU.Frame")[0] != 4.0f) {
    std::cerr << "Delayed GPU result replacement failed\n";
    return 1;
  }

  for (uint64_t index = 3;
       index <= game::debug::DebugProfilerHistory::kMaximumFrames + 2;
       ++index) {
    core::ProfilerFrameSnapshot frame;
    frame.frameIndex = index;
    history.Update(frame);
  }
  if (history.Frames().size() !=
          game::debug::DebugProfilerHistory::kMaximumFrames ||
      history.Frames().front().frameIndex != 3) {
    std::cerr << "Profiler history capacity failed\n";
    return 1;
  }
  return 0;
}
