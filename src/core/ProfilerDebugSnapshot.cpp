#include "Profiler.h"

#ifdef WIKIGOLF_DEBUG_TOOLS

#include <algorithm>

namespace core {

bool Profiler::GetLatestCompletedFrameSnapshot(
    ProfilerFrameSnapshot &snapshot) const {
  if (m_frames.empty()) {
    return false;
  }
  snapshot = MakeSnapshot(m_frames.back());
  return true;
}

std::vector<ProfilerFrameSnapshot>
Profiler::GetRecentCompletedFrameSnapshots(size_t maximumFrames) const {
  std::vector<ProfilerFrameSnapshot> snapshots;
  const size_t count = (std::min)(maximumFrames, m_frames.size());
  snapshots.reserve(count);
  const size_t first = m_frames.size() - count;
  for (size_t index = first; index < m_frames.size(); ++index) {
    snapshots.push_back(MakeSnapshot(m_frames[index]));
  }
  return snapshots;
}

ProfilerFrameSnapshot Profiler::MakeSnapshot(const FrameData &frame) const {
  ProfilerFrameSnapshot snapshot;
  snapshot.frameIndex = frame.index;
  snapshot.scene = frame.scene;
  snapshot.cpuFrameMs = frame.cpuFrameMs;
  snapshot.profilerOverheadMs = frame.profilerOverheadMs;
  snapshot.entityCount = frame.entityCount;
  snapshot.processCpuPercent = frame.process.cpuPercent;
  snapshot.workingSetMb = frame.process.workingSetMb;
  snapshot.privateMb = frame.process.privateMb;
  snapshot.gpuReceived = frame.gpuReceived;
  snapshot.gpuValid = frame.gpuValid;
  snapshot.pipelineStatsValid = frame.pipelineStatsValid;
  snapshot.gpuScopes = frame.gpuScopes;
  snapshot.pipeline = frame.pipeline;

  snapshot.cpuScopes.reserve(frame.cpuScopes.size());
  for (const auto &[id, scope] : frame.cpuScopes) {
    if (id >= m_scopeMetadata.size()) {
      continue;
    }
    const auto &metadata = m_scopeMetadata[id];
    snapshot.cpuScopes.push_back({metadata.name, scope.inclusiveMs,
                                  scope.exclusiveMs, scope.calls,
                                  metadata.function, metadata.file,
                                  metadata.line});
  }
  std::sort(snapshot.cpuScopes.begin(), snapshot.cpuScopes.end(),
            [](const auto &lhs, const auto &rhs) {
              return lhs.name < rhs.name;
            });

  snapshot.counters.reserve(frame.counters.size());
  for (const auto &[id, value] : frame.counters) {
    if (id < m_counterNames.size()) {
      snapshot.counters.push_back({m_counterNames[id], value});
    }
  }
  std::sort(snapshot.counters.begin(), snapshot.counters.end(),
            [](const auto &lhs, const auto &rhs) {
              return lhs.name < rhs.name;
            });
  std::sort(snapshot.gpuScopes.begin(), snapshot.gpuScopes.end(),
            [](const auto &lhs, const auto &rhs) {
              return lhs.name < rhs.name;
            });
  return snapshot;
}

} // namespace core

#endif
