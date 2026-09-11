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

Profiler &Profiler::Instance() {
  static Profiler instance;
  return instance;
}

Profiler::~Profiler() { Shutdown(); }

void Profiler::Initialize(const std::filesystem::path &outputRoot) {
  if (m_initialized) {
    return;
  }

  SYSTEM_INFO systemInfo{};
  GetSystemInfo(&systemInfo);
  m_logicalProcessorCount = std::max<DWORD>(1, systemInfo.dwNumberOfProcessors);

  m_outputDirectory = outputRoot / profiler_detail::MakeSessionName();
  std::error_code error;
  std::filesystem::create_directories(m_outputDirectory, error);
  if (error) {
    LOG_ERROR("Profiler", "Failed to create output directory '{}': {}",
              m_outputDirectory.string(), error.message());
    m_outputDirectory.clear();
  }

  m_frames.clear();
  m_frames.reserve(36000);
  m_frameLookup.clear();
  m_sceneSummary.clear();
  m_cpuSummary.clear();
  m_gpuSummary.clear();
  m_slowFrames.clear();
  m_finalizedFrameCount = 0;
  m_gpuValidFrameCount = 0;
  m_currentFrameIndex = 0;
  m_lastReportAt = Clock::now();
  m_lastProcessSampleAt = m_lastReportAt;
  m_cachedProcessMetrics = CaptureProcessMetrics();
  OpenRawReports();
  m_initialized = true;

  LOG_INFO("Profiler", "Detailed profiler started. Output='{}'",
           m_outputDirectory.string());
}

void Profiler::Shutdown() {
  if (!m_initialized) {
    return;
  }
  if (m_frameActive) {
    EndFrame();
  }
  for (auto &frame : m_frames) {
    FinalizeFrame(frame);
  }
  WriteReports();
  m_framesFile.close();
  m_scopesFile.close();
  m_countersFile.close();
  m_slowFramesFile.close();
  LOG_INFO("Profiler", "Detailed profiler stopped. Frames={} Output='{}'",
           m_frames.size(), m_outputDirectory.string());
  m_initialized = false;
}

uint64_t Profiler::BeginFrame(std::string_view sceneName, size_t entityCount) {
  if (!m_initialized) {
    Initialize();
  }
  if (m_frameActive) {
    EndFrame();
  }

  ++m_currentFrameIndex;
  if (sceneName.empty()) {
    m_currentScene = "Unknown";
  } else {
    m_currentScene = std::string(sceneName);
  }
  m_currentEntityCount = entityCount;
  m_frameScopes.clear();
  m_frameCounters.clear();
  m_scopeStack.clear();
  m_frameStartedAt = Clock::now();
  m_frameActive = true;
  return m_currentFrameIndex;
}

void Profiler::EndFrame() {
  if (!m_frameActive) {
    return;
  }

  const auto profilerStartedAt = Clock::now();
  while (!m_scopeStack.empty()) {
    EndScope();
  }

  FrameData frame;
  frame.index = m_currentFrameIndex;
  frame.scene = m_currentScene;
  frame.cpuFrameMs = profiler_detail::ToMilliseconds(profilerStartedAt - m_frameStartedAt);
  frame.entityCount = m_currentEntityCount;
  const auto now = Clock::now();
  if (now - m_lastProcessSampleAt >= std::chrono::milliseconds(250)) {
    m_cachedProcessMetrics = CaptureProcessMetrics();
    frame.processSampled = true;
  }
  frame.process = m_cachedProcessMetrics;
  frame.cpuScopes = std::move(m_frameScopes);
  frame.counters = std::move(m_frameCounters);

  m_frameLookup[frame.index] = m_frames.size();
  m_frames.push_back(std::move(frame));
  m_frameActive = false;

  const auto reportNow = Clock::now();
  if (reportNow - m_lastReportAt >= std::chrono::seconds(1)) {
    LogIntervalReport();
    m_lastReportAt = Clock::now();
    m_lastReportFrame = m_frames.size();
  }

  m_frames.back().profilerOverheadMs =
      profiler_detail::ToMilliseconds(Clock::now() - profilerStartedAt);

  constexpr size_t kMaximumGpuLatencyFrames = 16;
  if (m_frames.size() > kMaximumGpuLatencyFrames) {
    auto &expired = m_frames[m_frames.size() - kMaximumGpuLatencyFrames - 1];
    FinalizeFrame(expired);
  }
}

} // namespace core
