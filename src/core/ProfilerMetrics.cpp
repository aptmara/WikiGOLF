/**
 * @file Profiler.cpp
 * @brief フレーム単位のCPU/GPUパフォーマンス計測の実装
*/

#include "Profiler.h"
#include "ProfilerInternals.h"
#include "Logger.h"

#include <Psapi.h>
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <map>
#include <numeric>
#include <sstream>

#pragma comment(lib, "Psapi.lib")


namespace core {

void Profiler::BeginScope(std::string_view name) {
  if (!m_frameActive) {
    return;
  }
  m_scopeStack.push_back(ActiveScope{std::string(name), Clock::now(), 0.0});
}

void Profiler::EndScope() {
  if (!m_frameActive || m_scopeStack.empty()) {
    return;
  }

  ActiveScope scope = std::move(m_scopeStack.back());
  m_scopeStack.pop_back();
  const double inclusiveMs = profiler_detail::ToMilliseconds(Clock::now() - scope.startedAt);
  const double exclusiveMs = std::max(0.0, inclusiveMs - scope.childMs);
  auto &result = m_frameScopes[scope.name];
  result.inclusiveMs += inclusiveMs;
  result.exclusiveMs += exclusiveMs;
  ++result.calls;

  if (!m_scopeStack.empty()) {
    m_scopeStack.back().childMs += inclusiveMs;
  }
}

void Profiler::SetCounter(std::string_view name, double value) {
  if (m_frameActive) {
    m_frameCounters[std::string(name)] = value;
  }
}

void Profiler::AddCounter(std::string_view name, double value) {
  if (m_frameActive) {
    m_frameCounters[std::string(name)] += value;
  }
}

void Profiler::SubmitGpuFrame(GpuFrameSample sample) {
  FrameData *frame = FindFrame(sample.frameIndex);
  if (!frame) {
    return;
  }
  frame->gpuReceived = true;
  frame->gpuValid = sample.valid;
  frame->gpuScopes = std::move(sample.scopes);
  frame->pipeline = sample.pipeline;
}

Profiler::FrameData *Profiler::FindFrame(uint64_t frameIndex) {
  const auto found = m_frameLookup.find(frameIndex);
  if (found == m_frameLookup.end() || found->second >= m_frames.size()) {
    return nullptr;
  }
  return &m_frames[found->second];
}

Profiler::ProcessMetrics Profiler::CaptureProcessMetrics() {
  ProcessMetrics metrics;
  PROCESS_MEMORY_COUNTERS_EX memory{};
  memory.cb = sizeof(memory);
  if (GetProcessMemoryInfo(GetCurrentProcess(),
                           reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory),
                           sizeof(memory))) {
    metrics.workingSetMb = static_cast<double>(memory.WorkingSetSize) /
                           profiler_detail::kBytesPerMb;
    metrics.privateMb = static_cast<double>(memory.PrivateUsage) /
                        profiler_detail::kBytesPerMb;
  }

  FILETIME creation{}, exit{}, kernel{}, user{};
  const auto now = Clock::now();
  if (GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) {
    const uint64_t kernelTime = profiler_detail::FileTimeToUint64(kernel);
    const uint64_t userTime = profiler_detail::FileTimeToUint64(user);
    const double wallSeconds =
        std::chrono::duration<double>(now - m_lastProcessSampleAt).count();
    if (m_lastProcessKernelTime != 0 && wallSeconds > 0.0) {
      const double processSeconds =
          static_cast<double>((kernelTime - m_lastProcessKernelTime) +
                              (userTime - m_lastProcessUserTime)) /
          10000000.0;
      metrics.cpuPercent = processSeconds / wallSeconds /
                           static_cast<double>(m_logicalProcessorCount) * 100.0;
    }
    m_lastProcessKernelTime = kernelTime;
    m_lastProcessUserTime = userTime;
    m_lastProcessSampleAt = now;
  }
  return metrics;
}

} // namespace core
