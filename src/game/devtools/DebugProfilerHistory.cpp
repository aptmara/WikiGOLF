#include "DebugProfilerHistory.h"

#include <algorithm>

namespace game::debug {
namespace {

template <typename Finder>
std::vector<float> BuildSeries(
    const std::vector<core::ProfilerFrameSnapshot> &frames, Finder finder) {
  std::vector<float> values;
  values.reserve(frames.size());
  for (const auto &frame : frames) {
    values.push_back(static_cast<float>(finder(frame)));
  }
  return values;
}

} // namespace

void DebugProfilerHistory::Update(
    const core::ProfilerFrameSnapshot &snapshot) {
  const auto existing = std::find_if(
      m_frames.begin(), m_frames.end(), [&](const auto &frame) {
        return frame.frameIndex == snapshot.frameIndex;
      });
  if (existing != m_frames.end()) {
    *existing = snapshot;
    return;
  }
  if (!m_frames.empty() && snapshot.frameIndex < m_frames.back().frameIndex) {
    return;
  }
  m_frames.push_back(snapshot);
  if (m_frames.size() > kMaximumFrames) {
    m_frames.erase(m_frames.begin());
  }
}

void DebugProfilerHistory::Clear() { m_frames.clear(); }

std::vector<float> DebugProfilerHistory::CpuFrameSeries() const {
  return BuildSeries(m_frames,
                     [](const auto &frame) { return frame.cpuFrameMs; });
}

std::vector<float>
DebugProfilerHistory::CpuScopeSeries(std::string_view name,
                                     bool inclusive) const {
  return BuildSeries(m_frames, [&](const auto &frame) {
    const auto found = std::find_if(
        frame.cpuScopes.begin(), frame.cpuScopes.end(),
        [&](const auto &scope) { return scope.name == name; });
    if (found == frame.cpuScopes.end()) {
      return 0.0;
    }
    return inclusive ? found->inclusiveMs : found->exclusiveMs;
  });
}

std::vector<float>
DebugProfilerHistory::GpuScopeSeries(std::string_view name) const {
  return BuildSeries(m_frames, [&](const auto &frame) {
    const auto found = std::find_if(
        frame.gpuScopes.begin(), frame.gpuScopes.end(),
        [&](const auto &scope) { return scope.name == name; });
    return found == frame.gpuScopes.end() ? 0.0 : found->milliseconds;
  });
}

std::vector<float>
DebugProfilerHistory::CounterSeries(std::string_view name) const {
  return BuildSeries(m_frames, [&](const auto &frame) {
    const auto found = std::find_if(
        frame.counters.begin(), frame.counters.end(),
        [&](const auto &counter) { return counter.name == name; });
    return found == frame.counters.end() ? 0.0 : found->value;
  });
}

} // namespace game::debug
