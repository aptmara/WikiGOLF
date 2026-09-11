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

Profiler::MetricId Profiler::InternScope(std::string_view name,
                                        std::string_view function,
                                        std::string_view file, uint32_t line) {
  uint64_t hash = 1469598103934665603ull;
  const auto appendHash = [&hash](std::string_view value) {
    for (const unsigned char character : value) {
      hash ^= character;
      hash *= 1099511628211ull;
    }
    hash ^= 0xff;
    hash *= 1099511628211ull;
  };
  appendHash(name);
  appendHash(function);
  appendHash(file);
  hash ^= line;
  hash *= 1099511628211ull;
  const auto found = m_scopeIds.find(hash);
  if (found != m_scopeIds.end()) {
    for (const auto id : found->second) {
      const auto &metadata = m_scopeMetadata[id];
      if (metadata.name == name && metadata.function == function &&
          metadata.file == file && metadata.line == line) {
        return id;
      }
    }
  }
  const auto id = static_cast<MetricId>(m_scopeMetadata.size());
  m_scopeMetadata.push_back(
      {std::string(name), std::string(function), std::string(file), line});
  m_scopeIds[hash].push_back(id);
  return id;
}

Profiler::MetricId Profiler::InternCounter(std::string_view name) {
  uint64_t hash = 1469598103934665603ull;
  for (const unsigned char character : name) {
    hash ^= character;
    hash *= 1099511628211ull;
  }
  const auto found = m_counterIds.find(hash);
  if (found != m_counterIds.end()) {
    for (const auto id : found->second) {
      if (m_counterNames[id] == name) {
        return id;
      }
    }
  }
  const auto id = static_cast<MetricId>(m_counterNames.size());
  m_counterNames.emplace_back(name);
  m_counterIds[hash].push_back(id);
  return id;
}

void Profiler::BeginScope(std::string_view name, std::string_view function,
                          std::string_view file, uint32_t line) {
  if (!m_frameActive) {
    return;
  }
  m_scopeStack.push_back(
      ActiveScope{InternScope(name, function, file, line), Clock::now(), 0.0});
}

void Profiler::EndScope() {
  if (!m_frameActive || m_scopeStack.empty()) {
    return;
  }

  ActiveScope scope = std::move(m_scopeStack.back());
  m_scopeStack.pop_back();
  const double inclusiveMs = profiler_detail::ToMilliseconds(Clock::now() - scope.startedAt);
  const double exclusiveMs = std::max(0.0, inclusiveMs - scope.childMs);
  auto &result = m_frameScopes[scope.id];
  result.inclusiveMs += inclusiveMs;
  result.exclusiveMs += exclusiveMs;
  ++result.calls;

  if (!m_scopeStack.empty()) {
    m_scopeStack.back().childMs += inclusiveMs;
  }
}

void Profiler::SetCounter(std::string_view name, double value) {
  if (m_frameActive) {
    m_frameCounters[InternCounter(name)] = value;
  }
}

void Profiler::AddCounter(std::string_view name, double value) {
  if (m_frameActive) {
    m_frameCounters[InternCounter(name)] += value;
  }
}

void Profiler::SubmitGpuFrame(GpuFrameSample sample) {
  FrameData *frame = FindFrame(sample.frameIndex);
  if (!frame) {
    return;
  }
  frame->gpuReceived = true;
  frame->gpuValid = sample.valid;
  frame->pipelineStatsValid = sample.pipelineValid;
  frame->gpuScopes = std::move(sample.scopes);
  frame->pipeline = sample.pipeline;
  FinalizeFrame(*frame);
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
