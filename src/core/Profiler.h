#pragma once
/**
 * @file Profiler.h
 * @brief フレーム単位のCPU/GPUパフォーマンス計測
*/

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "ProfilerStatistics.h"

namespace core {

struct GpuScopeSample {
  std::string name;
  double milliseconds = 0.0;
};

struct GpuPipelineStats {
  uint64_t inputAssemblerVertices = 0;
  uint64_t inputAssemblerPrimitives = 0;
  uint64_t vertexShaderInvocations = 0;
  uint64_t pixelShaderInvocations = 0;
};

struct GpuFrameSample {
  uint64_t frameIndex = 0;
  bool valid = false;
  bool pipelineValid = false;
  std::vector<GpuScopeSample> scopes;
  GpuPipelineStats pipeline;
};

#ifdef WIKIGOLF_DEBUG_TOOLS
struct ProfilerNamedValue {
  std::string name;
  double value = 0.0;
};

struct ProfilerCpuScopeSnapshot {
  std::string name;
  double inclusiveMs = 0.0;
  double exclusiveMs = 0.0;
  uint32_t calls = 0;
  std::string function;
  std::string file;
  uint32_t line = 0;
};

struct ProfilerFrameSnapshot {
  uint64_t frameIndex = 0;
  std::string scene;
  double cpuFrameMs = 0.0;
  double profilerOverheadMs = 0.0;
  size_t entityCount = 0;
  double processCpuPercent = 0.0;
  double workingSetMb = 0.0;
  double privateMb = 0.0;
  std::vector<ProfilerCpuScopeSnapshot> cpuScopes;
  std::vector<ProfilerNamedValue> counters;
  bool gpuReceived = false;
  bool gpuValid = false;
  bool pipelineStatsValid = false;
  std::vector<GpuScopeSample> gpuScopes;
  GpuPipelineStats pipeline;
};
#endif

class Profiler {
public:
  static Profiler &Instance();

  void Initialize(const std::filesystem::path &outputRoot = "profiling");
  void Shutdown();

  uint64_t BeginFrame(std::string_view sceneName, size_t entityCount);
  void EndFrame();

  void BeginScope(std::string_view name, std::string_view function = {},
                  std::string_view file = {}, uint32_t line = 0);
  void EndScope();

  void SetCounter(std::string_view name, double value);
  void AddCounter(std::string_view name, double value);
  void SubmitGpuFrame(GpuFrameSample sample);

  uint64_t GetCurrentFrameIndex() const { return m_currentFrameIndex; }
  const std::filesystem::path &GetOutputDirectory() const {
    return m_outputDirectory;
  }
#ifdef WIKIGOLF_DEBUG_TOOLS
  bool GetLatestCompletedFrameSnapshot(ProfilerFrameSnapshot &snapshot) const;
  std::vector<ProfilerFrameSnapshot>
  GetRecentCompletedFrameSnapshots(size_t maximumFrames) const;
#endif

private:
  using Clock = std::chrono::steady_clock;
  using MetricId = uint32_t;

  struct ScopeMetadata {
    std::string name;
    std::string function;
    std::string file;
    uint32_t line = 0;
  };

  struct ScopeData {
    double inclusiveMs = 0.0;
    double exclusiveMs = 0.0;
    uint32_t calls = 0;
  };

  struct ActiveScope {
    MetricId id = 0;
    Clock::time_point startedAt;
    double childMs = 0.0;
  };

  struct ProcessMetrics {
    double cpuPercent = 0.0;
    double workingSetMb = 0.0;
    double privateMb = 0.0;
  };

  struct FrameData {
    uint64_t index = 0;
    std::string scene;
    double cpuFrameMs = 0.0;
    double profilerOverheadMs = 0.0;
    size_t entityCount = 0;
    ProcessMetrics process;
    bool processSampled = false;
    std::unordered_map<MetricId, ScopeData> cpuScopes;
    std::unordered_map<MetricId, double> counters;
    bool gpuReceived = false;
    bool gpuValid = false;
    bool pipelineStatsValid = false;
    bool finalized = false;
    double gpuFrameMs = 0.0;
    uint32_t gpuQueryLatencyFrames = 0;
    std::vector<GpuScopeSample> gpuScopes;
    GpuPipelineStats pipeline;
  };

  struct SceneAggregate {
    uint64_t frames = 0;
    uint64_t gpuValidFrames = 0;
    profiler_detail::BoundedStatistics cpu;
    profiler_detail::BoundedStatistics gpu;
    profiler_detail::BoundedStatistics overhead;
    double peakWorkingSetMb = 0.0;
    double peakPrivateMb = 0.0;
  };

  struct ScopeAggregate {
    profiler_detail::BoundedStatistics inclusive;
    profiler_detail::BoundedStatistics exclusive;
    uint64_t calls = 0;
  };

  struct SlowFrame {
    uint64_t index = 0;
    std::string scene;
    double cpuMs = 0.0;
    double gpuMs = 0.0;
  };

  Profiler() = default;
  ~Profiler();

  ProcessMetrics CaptureProcessMetrics();
  MetricId InternScope(std::string_view name, std::string_view function,
                       std::string_view file, uint32_t line);
  MetricId InternCounter(std::string_view name);
  void OpenRawReports();
  void FinalizeFrame(FrameData &frame);
  void WriteFrameRows(const FrameData &frame);
  void AccumulateFrame(const FrameData &frame);
  void ReleaseOldFrameDetails();
  void LogIntervalReport();
  void WriteReports();
  FrameData *FindFrame(uint64_t frameIndex);
#ifdef WIKIGOLF_DEBUG_TOOLS
  ProfilerFrameSnapshot MakeSnapshot(const FrameData &frame) const;
#endif

  bool m_initialized = false;
  bool m_frameActive = false;
  uint64_t m_currentFrameIndex = 0;
  size_t m_currentEntityCount = 0;
  std::string m_currentScene = "Unknown";
  Clock::time_point m_frameStartedAt{};
  Clock::time_point m_lastReportAt{};
  size_t m_lastReportFrame = 0;
  std::filesystem::path m_outputDirectory;
  std::vector<ActiveScope> m_scopeStack;
  std::unordered_map<MetricId, ScopeData> m_frameScopes;
  std::unordered_map<MetricId, double> m_frameCounters;
  std::vector<FrameData> m_frames;
  std::unordered_map<uint64_t, size_t> m_frameLookup;

  std::vector<ScopeMetadata> m_scopeMetadata;
  std::unordered_map<uint64_t, std::vector<MetricId>> m_scopeIds;
  std::vector<std::string> m_counterNames;
  std::unordered_map<uint64_t, std::vector<MetricId>> m_counterIds;
  std::map<std::string, SceneAggregate> m_sceneSummary;
  std::map<MetricId, ScopeAggregate> m_cpuSummary;
  std::map<std::string, ScopeAggregate> m_gpuSummary;
  std::vector<SlowFrame> m_slowFrames;
  uint64_t m_finalizedFrameCount = 0;
  uint64_t m_gpuValidFrameCount = 0;
  ProcessMetrics m_cachedProcessMetrics;
  std::ofstream m_framesFile;
  std::ofstream m_scopesFile;
  std::ofstream m_countersFile;
  std::ofstream m_slowFramesFile;

  uint64_t m_lastProcessKernelTime = 0;
  uint64_t m_lastProcessUserTime = 0;
  Clock::time_point m_lastProcessSampleAt{};
  uint32_t m_logicalProcessorCount = 1;
};

class ScopedTimer {
public:
  ScopedTimer(std::string_view name, std::string_view function,
              std::string_view file, uint32_t line) {
    Profiler::Instance().BeginScope(name, function, file, line);
  }
  ~ScopedTimer() { Profiler::Instance().EndScope(); }

  ScopedTimer(const ScopedTimer &) = delete;
  ScopedTimer &operator=(const ScopedTimer &) = delete;
};

} // namespace core

#define PROFILE_JOIN_IMPL(a, b) a##b
#define PROFILE_JOIN(a, b) PROFILE_JOIN_IMPL(a, b)
#ifdef WIKIGOLF_PROFILING
#define PROFILE_SCOPE(name)                                                    \
  core::ScopedTimer PROFILE_JOIN(profileTimer_, __LINE__)(                     \
      name, __FUNCTION__, __FILE__, static_cast<uint32_t>(__LINE__))
#else
#define PROFILE_SCOPE(name) ((void)0)
#endif
