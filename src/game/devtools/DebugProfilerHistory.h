#pragma once

#include "../../core/Profiler.h"
#include <cstddef>
#include <string_view>
#include <vector>

namespace game::debug {

class DebugProfilerHistory {
public:
  static constexpr std::size_t kMaximumFrames = 240;

  void Update(const core::ProfilerFrameSnapshot &snapshot);
  void Clear();
  const std::vector<core::ProfilerFrameSnapshot> &Frames() const {
    return m_frames;
  }
  std::vector<float> CpuFrameSeries() const;
  std::vector<float> CpuScopeSeries(std::string_view name,
                                    bool inclusive) const;
  std::vector<float> GpuScopeSeries(std::string_view name) const;
  std::vector<float> CounterSeries(std::string_view name) const;

private:
  std::vector<core::ProfilerFrameSnapshot> m_frames;
};

} // namespace game::debug
