#pragma once
/**
 * @file Profiler.h
 * @brief フレーム単位のCPU/GPUパフォーマンス計測
 */

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

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
  std::vector<GpuScopeSample> scopes;
  GpuPipelineStats pipeline;
};

class Profiler {
public:
  static Profiler &Instance();

  void Initialize(const std::filesystem::path &outputRoot = "profiling");
  void Shutdown();

  uint64_t BeginFrame(std::string_view sceneName, size_t entityCount);
  void EndFrame();

  void BeginScope(std::string_view name);
  void EndScope();

  void SetCounter(std::string_view name, double value);
  void AddCounter(std::string_view name, double value);
  void SubmitGpuFrame(GpuFrameSample sample);

  uint64_t GetCurrentFrameIndex() const { return m_currentFrameIndex; }
  const std::filesystem::path &GetOutputDirectory() const {
    return m_outputDirectory;
  }

private:
  using Clock = std::chrono::steady_clock;

  struct ScopeData {
    double inclusiveMs = 0.0;
    double exclusiveMs = 0.0;
    uint32_t calls = 0;
  };

  struct ActiveScope {
    std::string name;
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
    std::unordered_map<std::string, ScopeData> cpuScopes;
    std::unordered_map<std::string, double> counters;
    bool gpuReceived = false;
    bool gpuValid = false;
    std::vector<GpuScopeSample> gpuScopes;
    GpuPipelineStats pipeline;
  };

  Profiler() = default;
  ~Profiler();

  ProcessMetrics CaptureProcessMetrics();
  void LogIntervalReport();
  void WriteReports();
  FrameData *FindFrame(uint64_t frameIndex);

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
  std::unordered_map<std::string, ScopeData> m_frameScopes;
  std::unordered_map<std::string, double> m_frameCounters;
  std::vector<FrameData> m_frames;
  std::unordered_map<uint64_t, size_t> m_frameLookup;

  uint64_t m_lastProcessKernelTime = 0;
  uint64_t m_lastProcessUserTime = 0;
  Clock::time_point m_lastProcessSampleAt{};
  uint32_t m_logicalProcessorCount = 1;
};

class ScopedTimer {
public:
  explicit ScopedTimer(std::string_view name) {
    Profiler::Instance().BeginScope(name);
  }
  ~ScopedTimer() { Profiler::Instance().EndScope(); }

  ScopedTimer(const ScopedTimer &) = delete;
  ScopedTimer &operator=(const ScopedTimer &) = delete;
};

} // namespace core

#define PROFILE_JOIN_IMPL(a, b) a##b
#define PROFILE_JOIN(a, b) PROFILE_JOIN_IMPL(a, b)
#define PROFILE_SCOPE(name)                                                    \
  core::ScopedTimer PROFILE_JOIN(profileTimer_, __LINE__)(name)
