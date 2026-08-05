/**
 * @file Profiler.cpp
 * @brief フレーム単位のCPU/GPUパフォーマンス計測の実装
 */

#include "Profiler.h"
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
namespace {

constexpr double kBytesPerMb = 1024.0 * 1024.0;

double ToMilliseconds(const std::chrono::steady_clock::duration duration) {
  return std::chrono::duration<double, std::milli>(duration).count();
}

double Percentile(std::vector<double> values, double percentile) {
  if (values.empty()) {
    return 0.0;
  }
  std::sort(values.begin(), values.end());
  const double position = percentile * static_cast<double>(values.size() - 1);
  const size_t lower = static_cast<size_t>(std::floor(position));
  const size_t upper = static_cast<size_t>(std::ceil(position));
  if (lower == upper) {
    return values[lower];
  }
  const double fraction = position - static_cast<double>(lower);
  return values[lower] + (values[upper] - values[lower]) * fraction;
}

double Average(const std::vector<double> &values) {
  if (values.empty()) {
    return 0.0;
  }
  return std::accumulate(values.begin(), values.end(), 0.0) /
         static_cast<double>(values.size());
}

std::string CsvEscape(std::string_view value) {
  bool needsQuotes = false;
  std::string escaped;
  escaped.reserve(value.size() + 2);
  for (const char c : value) {
    if (c == '"') {
      escaped += "\"\"";
      needsQuotes = true;
    } else {
      escaped += c;
      needsQuotes = needsQuotes || c == ',' || c == '\n' || c == '\r';
    }
  }
  return needsQuotes ? std::string("\"") + escaped + "\"" : escaped;
}

uint64_t FileTimeToUint64(const FILETIME &time) {
  ULARGE_INTEGER value{};
  value.LowPart = time.dwLowDateTime;
  value.HighPart = time.dwHighDateTime;
  return value.QuadPart;
}

std::string MakeSessionName() {
  const std::time_t now = std::time(nullptr);
  std::tm local{};
  localtime_s(&local, &now);
  std::ostringstream stream;
  stream << "session_" << std::put_time(&local, "%Y%m%d_%H%M%S");
  return stream.str();
}

struct ScopeAggregate {
  std::vector<double> inclusive;
  std::vector<double> exclusive;
  uint64_t calls = 0;
};

} // namespace

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

  m_outputDirectory = outputRoot / MakeSessionName();
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
  m_currentFrameIndex = 0;
  m_lastReportAt = Clock::now();
  m_lastProcessSampleAt = m_lastReportAt;
  CaptureProcessMetrics();
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
  WriteReports();
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
  m_currentScene = sceneName.empty() ? "Unknown" : std::string(sceneName);
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
  frame.cpuFrameMs = ToMilliseconds(profilerStartedAt - m_frameStartedAt);
  frame.entityCount = m_currentEntityCount;
  frame.process = CaptureProcessMetrics();
  frame.cpuScopes = std::move(m_frameScopes);
  frame.counters = std::move(m_frameCounters);

  m_frameLookup[frame.index] = m_frames.size();
  m_frames.push_back(std::move(frame));
  m_frameActive = false;

  const auto now = Clock::now();
  if (now - m_lastReportAt >= std::chrono::seconds(1)) {
    LogIntervalReport();
    m_lastReportAt = Clock::now();
    m_lastReportFrame = m_frames.size();
  }

  m_frames.back().profilerOverheadMs =
      ToMilliseconds(Clock::now() - profilerStartedAt);
}

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
  const double inclusiveMs = ToMilliseconds(Clock::now() - scope.startedAt);
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
    metrics.workingSetMb = static_cast<double>(memory.WorkingSetSize) / kBytesPerMb;
    metrics.privateMb = static_cast<double>(memory.PrivateUsage) / kBytesPerMb;
  }

  FILETIME creation{}, exit{}, kernel{}, user{};
  const auto now = Clock::now();
  if (GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) {
    const uint64_t kernelTime = FileTimeToUint64(kernel);
    const uint64_t userTime = FileTimeToUint64(user);
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

void Profiler::LogIntervalReport() {
  if (m_lastReportFrame >= m_frames.size()) {
    return;
  }

  std::vector<double> frameTimes;
  std::vector<double> gpuTimes;
  std::unordered_map<std::string, double> exclusive;
  double maxWorkingSet = 0.0;
  double maxCpu = 0.0;
  const std::string &scene = m_frames.back().scene;

  for (size_t i = m_lastReportFrame; i < m_frames.size(); ++i) {
    const auto &frame = m_frames[i];
    frameTimes.push_back(frame.cpuFrameMs);
    maxWorkingSet = std::max(maxWorkingSet, frame.process.workingSetMb);
    maxCpu = std::max(maxCpu, frame.process.cpuPercent);
    for (const auto &[name, scope] : frame.cpuScopes) {
      exclusive[name] += scope.exclusiveMs;
    }
    if (frame.gpuValid) {
      const auto gpuFrame = std::find_if(
          frame.gpuScopes.begin(), frame.gpuScopes.end(),
          [](const GpuScopeSample &sample) { return sample.name == "GPU.Frame"; });
      if (gpuFrame != frame.gpuScopes.end()) {
        gpuTimes.push_back(gpuFrame->milliseconds);
      }
    }
  }

  std::vector<std::pair<std::string, double>> ranked;
  ranked.reserve(exclusive.size());
  for (const auto &[name, total] : exclusive) {
    ranked.emplace_back(name, total / frameTimes.size());
  }
  std::sort(ranked.begin(), ranked.end(),
            [](const auto &lhs, const auto &rhs) {
              return lhs.second > rhs.second;
            });

  const double averageFrame = Average(frameTimes);
  LOG_INFO("Perf",
           "scene={} frames={} FPS={:.1f} CPU frame avg={:.3f}ms "
           "p95={:.3f}ms max={:.3f}ms GPU avg={:.3f}ms p95={:.3f}ms "
           "processCPU(max)={:.1f}% workingSet(max)={:.1f}MB",
           scene, frameTimes.size(), averageFrame > 0.0 ? 1000.0 / averageFrame : 0.0,
           averageFrame, Percentile(frameTimes, 0.95),
           *std::max_element(frameTimes.begin(), frameTimes.end()),
           Average(gpuTimes), Percentile(gpuTimes, 0.95), maxCpu, maxWorkingSet);
  const size_t count = std::min<size_t>(6, ranked.size());
  for (size_t i = 0; i < count; ++i) {
    LOG_INFO("Perf", "  CPU exclusive #{} {} avg={:.3f}ms/frame",
             i + 1, ranked[i].first, ranked[i].second);
  }
}

void Profiler::WriteReports() {
  if (m_outputDirectory.empty() || m_frames.empty()) {
    return;
  }

  const auto framesPath = m_outputDirectory / "performance_frames.csv";
  const auto scopesPath = m_outputDirectory / "performance_scopes.csv";
  const auto countersPath = m_outputDirectory / "performance_counters.csv";
  const auto summaryPath = m_outputDirectory / "performance_summary.txt";

  std::ofstream framesFile(framesPath);
  std::ofstream scopesFile(scopesPath);
  std::ofstream countersFile(countersPath);
  std::ofstream summaryFile(summaryPath);
  if (!framesFile || !scopesFile || !countersFile || !summaryFile) {
    LOG_ERROR("Profiler", "Failed to open one or more profiler output files");
    return;
  }

  framesFile << "frame,scene,cpu_frame_ms,fps,profiler_overhead_ms,"
                "process_cpu_percent,working_set_mb,private_mb,entity_count,"
                "gpu_valid,gpu_frame_ms,ia_vertices,ia_primitives,"
                "vs_invocations,ps_invocations\n";
  scopesFile << "frame,scene,domain,name,inclusive_ms,exclusive_ms,calls\n";
  countersFile << "frame,scene,name,value\n";

  framesFile << std::fixed << std::setprecision(4);
  scopesFile << std::fixed << std::setprecision(4);
  countersFile << std::fixed << std::setprecision(4);

  std::map<std::string, std::vector<const FrameData *>> framesByScene;
  std::map<std::string, ScopeAggregate> cpuSummary;
  std::map<std::string, ScopeAggregate> gpuSummary;

  for (const auto &frame : m_frames) {
    double gpuFrameMs = 0.0;
    for (const auto &scope : frame.gpuScopes) {
      if (scope.name == "GPU.Frame") {
        gpuFrameMs = scope.milliseconds;
      }
      scopesFile << frame.index << ',' << CsvEscape(frame.scene) << ",GPU,"
                 << CsvEscape(scope.name) << ',' << scope.milliseconds << ','
                 << scope.milliseconds << ",1\n";
      gpuSummary[scope.name].inclusive.push_back(scope.milliseconds);
      gpuSummary[scope.name].exclusive.push_back(scope.milliseconds);
      ++gpuSummary[scope.name].calls;
    }

    const double fps = frame.cpuFrameMs > 0.0 ? 1000.0 / frame.cpuFrameMs : 0.0;
    framesFile << frame.index << ',' << CsvEscape(frame.scene) << ','
               << frame.cpuFrameMs << ',' << fps << ','
               << frame.profilerOverheadMs << ',' << frame.process.cpuPercent
               << ',' << frame.process.workingSetMb << ','
               << frame.process.privateMb << ',' << frame.entityCount << ','
               << (frame.gpuValid ? 1 : 0) << ',' << gpuFrameMs << ','
               << frame.pipeline.inputAssemblerVertices << ','
               << frame.pipeline.inputAssemblerPrimitives << ','
               << frame.pipeline.vertexShaderInvocations << ','
               << frame.pipeline.pixelShaderInvocations << '\n';

    for (const auto &[name, scope] : frame.cpuScopes) {
      scopesFile << frame.index << ',' << CsvEscape(frame.scene) << ",CPU,"
                 << CsvEscape(name) << ',' << scope.inclusiveMs << ','
                 << scope.exclusiveMs << ',' << scope.calls << '\n';
      auto &aggregate = cpuSummary[name];
      aggregate.inclusive.push_back(scope.inclusiveMs);
      aggregate.exclusive.push_back(scope.exclusiveMs);
      aggregate.calls += scope.calls;
    }
    for (const auto &[name, value] : frame.counters) {
      countersFile << frame.index << ',' << CsvEscape(frame.scene) << ','
                   << CsvEscape(name) << ',' << value << '\n';
    }
    framesByScene[frame.scene].push_back(&frame);
  }

  summaryFile << std::fixed << std::setprecision(3);
  summaryFile << "WikiGOLF detailed performance profile\n"
              << "Frames: " << m_frames.size() << "\n"
              << "GPU valid frames: "
              << std::count_if(m_frames.begin(), m_frames.end(),
                               [](const FrameData &frame) {
                                 return frame.gpuValid;
                               })
              << "\n\n";

  summaryFile << "[Scene summary]\n";
  for (const auto &[scene, sceneFrames] : framesByScene) {
    std::vector<double> cpu;
    std::vector<double> gpu;
    std::vector<double> overhead;
    double peakWorkingSet = 0.0;
    double peakPrivate = 0.0;
    for (const auto *frame : sceneFrames) {
      cpu.push_back(frame->cpuFrameMs);
      overhead.push_back(frame->profilerOverheadMs);
      peakWorkingSet = std::max(peakWorkingSet, frame->process.workingSetMb);
      peakPrivate = std::max(peakPrivate, frame->process.privateMb);
      if (frame->gpuValid) {
        for (const auto &sample : frame->gpuScopes) {
          if (sample.name == "GPU.Frame") {
            gpu.push_back(sample.milliseconds);
          }
        }
      }
    }
    summaryFile << scene << ": frames=" << sceneFrames.size()
                << " FPS(avg)=" << (Average(cpu) > 0.0 ? 1000.0 / Average(cpu) : 0.0)
                << " CPU ms avg/p50/p95/p99/max=" << Average(cpu) << '/'
                << Percentile(cpu, 0.50) << '/' << Percentile(cpu, 0.95) << '/'
                << Percentile(cpu, 0.99) << '/'
                << *std::max_element(cpu.begin(), cpu.end())
                << " GPU ms avg/p95=" << Average(gpu) << '/'
                << Percentile(gpu, 0.95)
                << " profiler overhead avg/p95=" << Average(overhead) << '/'
                << Percentile(overhead, 0.95)
                << " memory peak working/private MB=" << peakWorkingSet << '/'
                << peakPrivate << '\n';
  }

  const auto writeScopeSummary = [&summaryFile](
                                     std::string_view title,
                                     const std::map<std::string, ScopeAggregate> &summary) {
    struct RankedScope {
      std::string name;
      double average = 0.0;
      const ScopeAggregate *data = nullptr;
    };
    std::vector<RankedScope> ranked;
    for (const auto &[name, aggregate] : summary) {
      ranked.push_back({name, Average(aggregate.exclusive), &aggregate});
    }
    std::sort(ranked.begin(), ranked.end(),
              [](const RankedScope &lhs, const RankedScope &rhs) {
                return lhs.average > rhs.average;
              });

    summaryFile << "\n[" << title << "]\n";
    summaryFile << "name,avg_inclusive_ms,avg_exclusive_ms,p95_inclusive_ms,"
                   "p99_inclusive_ms,max_inclusive_ms,total_calls\n";
    for (const auto &scope : ranked) {
      summaryFile << scope.name << ',' << Average(scope.data->inclusive) << ','
                  << Average(scope.data->exclusive) << ','
                  << Percentile(scope.data->inclusive, 0.95) << ','
                  << Percentile(scope.data->inclusive, 0.99) << ','
                  << *std::max_element(scope.data->inclusive.begin(),
                                       scope.data->inclusive.end())
                  << ',' << scope.data->calls << '\n';
    }
  };

  writeScopeSummary("CPU scopes ranked by exclusive time", cpuSummary);
  writeScopeSummary("GPU scopes ranked by time", gpuSummary);

  std::vector<const FrameData *> slowFrames;
  slowFrames.reserve(m_frames.size());
  for (const auto &frame : m_frames) {
    slowFrames.push_back(&frame);
  }
  std::sort(slowFrames.begin(), slowFrames.end(),
            [](const FrameData *lhs, const FrameData *rhs) {
              return lhs->cpuFrameMs > rhs->cpuFrameMs;
            });
  summaryFile << "\n[Top 20 slow CPU frames]\nframe,scene,cpu_ms,gpu_ms\n";
  for (size_t i = 0; i < std::min<size_t>(20, slowFrames.size()); ++i) {
    double gpuMs = 0.0;
    for (const auto &sample : slowFrames[i]->gpuScopes) {
      if (sample.name == "GPU.Frame") {
        gpuMs = sample.milliseconds;
      }
    }
    summaryFile << slowFrames[i]->index << ',' << slowFrames[i]->scene << ','
                << slowFrames[i]->cpuFrameMs << ',' << gpuMs << '\n';
  }
}

} // namespace core
