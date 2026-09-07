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

  const double averageFrame = profiler_detail::Average(frameTimes);
  LOG_INFO("Perf",
           "scene={} frames={} FPS={:.1f} CPU frame avg={:.3f}ms "
           "p95={:.3f}ms max={:.3f}ms GPU avg={:.3f}ms p95={:.3f}ms "
           "processCPU(max)={:.1f}% workingSet(max)={:.1f}MB",
           scene, frameTimes.size(), profiler_detail::FramesPerSecond(averageFrame),
           averageFrame, profiler_detail::Percentile(frameTimes, 0.95),
           *std::max_element(frameTimes.begin(), frameTimes.end()),
           profiler_detail::Average(gpuTimes), profiler_detail::Percentile(gpuTimes, 0.95), maxCpu, maxWorkingSet);
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
  std::map<std::string, profiler_detail::ScopeAggregate> cpuSummary;
  std::map<std::string, profiler_detail::ScopeAggregate> gpuSummary;

  for (const auto &frame : m_frames) {
    double gpuFrameMs = 0.0;
    for (const auto &scope : frame.gpuScopes) {
      if (scope.name == "GPU.Frame") {
        gpuFrameMs = scope.milliseconds;
      }
      scopesFile << frame.index << ',' << profiler_detail::CsvEscape(frame.scene) << ",GPU,"
                 << profiler_detail::CsvEscape(scope.name) << ',' << scope.milliseconds << ','
                 << scope.milliseconds << ",1\n";
      gpuSummary[scope.name].inclusive.push_back(scope.milliseconds);
      gpuSummary[scope.name].exclusive.push_back(scope.milliseconds);
      ++gpuSummary[scope.name].calls;
    }

    const double fps = profiler_detail::FramesPerSecond(frame.cpuFrameMs);
    int gpuValid = 0;
    if (frame.gpuValid) {
      gpuValid = 1;
    }
    framesFile << frame.index << ',' << profiler_detail::CsvEscape(frame.scene) << ','
               << frame.cpuFrameMs << ',' << fps << ','
               << frame.profilerOverheadMs << ',' << frame.process.cpuPercent
               << ',' << frame.process.workingSetMb << ','
               << frame.process.privateMb << ',' << frame.entityCount << ','
               << gpuValid << ',' << gpuFrameMs << ','
               << frame.pipeline.inputAssemblerVertices << ','
               << frame.pipeline.inputAssemblerPrimitives << ','
               << frame.pipeline.vertexShaderInvocations << ','
               << frame.pipeline.pixelShaderInvocations << '\n';

    for (const auto &[name, scope] : frame.cpuScopes) {
      scopesFile << frame.index << ',' << profiler_detail::CsvEscape(frame.scene) << ",CPU,"
                 << profiler_detail::CsvEscape(name) << ',' << scope.inclusiveMs << ','
                 << scope.exclusiveMs << ',' << scope.calls << '\n';
      auto &aggregate = cpuSummary[name];
      aggregate.inclusive.push_back(scope.inclusiveMs);
      aggregate.exclusive.push_back(scope.exclusiveMs);
      aggregate.calls += scope.calls;
    }
    for (const auto &[name, value] : frame.counters) {
      countersFile << frame.index << ',' << profiler_detail::CsvEscape(frame.scene) << ','
                   << profiler_detail::CsvEscape(name) << ',' << value << '\n';
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
                << " FPS(avg)=" << profiler_detail::FramesPerSecond(
                       profiler_detail::Average(cpu))
                << " CPU ms avg/p50/p95/p99/max=" << profiler_detail::Average(cpu) << '/'
                << profiler_detail::Percentile(cpu, 0.50) << '/' << profiler_detail::Percentile(cpu, 0.95) << '/'
                << profiler_detail::Percentile(cpu, 0.99) << '/'
                << *std::max_element(cpu.begin(), cpu.end())
                << " GPU ms avg/p95=" << profiler_detail::Average(gpu) << '/'
                << profiler_detail::Percentile(gpu, 0.95)
                << " profiler overhead avg/p95=" << profiler_detail::Average(overhead) << '/'
                << profiler_detail::Percentile(overhead, 0.95)
                << " memory peak working/private MB=" << peakWorkingSet << '/'
                << peakPrivate << '\n';
  }

  const auto writeScopeSummary = [&summaryFile](
                                     std::string_view title,
                                     const std::map<std::string, profiler_detail::ScopeAggregate> &summary) {
    struct RankedScope {
      std::string name;
      double average = 0.0;
      const profiler_detail::ScopeAggregate *data = nullptr;
    };
    std::vector<RankedScope> ranked;
    for (const auto &[name, aggregate] : summary) {
      ranked.push_back({name, profiler_detail::Average(aggregate.exclusive), &aggregate});
    }
    std::sort(ranked.begin(), ranked.end(),
              [](const RankedScope &lhs, const RankedScope &rhs) {
                return lhs.average > rhs.average;
              });

    summaryFile << "\n[" << title << "]\n";
    summaryFile << "name,avg_inclusive_ms,avg_exclusive_ms,p95_inclusive_ms,"
                   "p99_inclusive_ms,max_inclusive_ms,total_calls\n";
    for (const auto &scope : ranked) {
      summaryFile << scope.name << ',' << profiler_detail::Average(scope.data->inclusive) << ','
                  << profiler_detail::Average(scope.data->exclusive) << ','
                  << profiler_detail::Percentile(scope.data->inclusive, 0.95) << ','
                  << profiler_detail::Percentile(scope.data->inclusive, 0.99) << ','
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
