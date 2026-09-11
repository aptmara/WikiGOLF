/** @file ProfilerReports.cpp @brief プロファイル結果の逐次出力と省メモリ集計 */
#include "Profiler.h"
#include "ProfilerInternals.h"
#include "Logger.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace core {

void Profiler::OpenRawReports() {
  if (m_outputDirectory.empty()) return;
  m_framesFile.open(m_outputDirectory / "performance_frames.csv");
  m_scopesFile.open(m_outputDirectory / "performance_scopes.csv");
  m_countersFile.open(m_outputDirectory / "performance_counters.csv");
  m_slowFramesFile.open(m_outputDirectory / "performance_slow_frames.csv");
  if (!m_framesFile || !m_scopesFile || !m_countersFile || !m_slowFramesFile) {
    LOG_ERROR("Profiler", "Failed to open one or more profiler output files");
    return;
  }
  m_framesFile << "frame,scene,cpu_frame_ms,fps,profiler_overhead_ms,process_cpu_percent,working_set_mb,private_mb,entity_count,gpu_valid,gpu_frame_ms,ia_vertices,ia_primitives,vs_invocations,ps_invocations,process_metrics_sampled,pipeline_stats_valid,gpu_query_latency_frames\n";
  m_scopesFile << "frame,scene,domain,name,inclusive_ms,exclusive_ms,calls,function,file,line\n";
  m_countersFile << "frame,scene,name,value\n";
  m_slowFramesFile << "frame,scene,cpu_frame_ms,gpu_frame_ms,rank,name,exclusive_ms,inclusive_ms,calls,function,file,line\n";
  m_framesFile << std::fixed << std::setprecision(4);
  m_scopesFile << std::fixed << std::setprecision(4);
  m_countersFile << std::fixed << std::setprecision(4);
  m_slowFramesFile << std::fixed << std::setprecision(4);
}

void Profiler::WriteFrameRows(const FrameData &frame) {
  if (!m_framesFile || !m_scopesFile || !m_countersFile) return;
  m_framesFile << frame.index << ',' << profiler_detail::CsvEscape(frame.scene)
               << ',' << frame.cpuFrameMs << ',' << profiler_detail::FramesPerSecond(frame.cpuFrameMs)
               << ',' << frame.profilerOverheadMs << ',' << frame.process.cpuPercent
               << ',' << frame.process.workingSetMb << ',' << frame.process.privateMb
               << ',' << frame.entityCount << ',' << (frame.gpuValid ? 1 : 0)
               << ',' << frame.gpuFrameMs << ',' << frame.pipeline.inputAssemblerVertices
               << ',' << frame.pipeline.inputAssemblerPrimitives << ','
               << frame.pipeline.vertexShaderInvocations << ',' << frame.pipeline.pixelShaderInvocations
               << ',' << (frame.processSampled ? 1 : 0) << ','
               << (frame.pipelineStatsValid ? 1 : 0) << ',' << frame.gpuQueryLatencyFrames << '\n';

  for (const auto &[id, scope] : frame.cpuScopes) {
    if (id >= m_scopeMetadata.size()) continue;
    const auto &meta = m_scopeMetadata[id];
    m_scopesFile << frame.index << ',' << profiler_detail::CsvEscape(frame.scene)
                 << ",CPU," << profiler_detail::CsvEscape(meta.name) << ','
                 << scope.inclusiveMs << ',' << scope.exclusiveMs << ',' << scope.calls
                 << ',' << profiler_detail::CsvEscape(meta.function) << ','
                 << profiler_detail::CsvEscape(meta.file) << ',' << meta.line << '\n';
  }
  for (const auto &scope : frame.gpuScopes) {
    m_scopesFile << frame.index << ',' << profiler_detail::CsvEscape(frame.scene)
                 << ",GPU," << profiler_detail::CsvEscape(scope.name) << ','
                 << scope.milliseconds << ',' << scope.milliseconds << ",1,,,0\n";
  }
  for (const auto &[id, value] : frame.counters) {
    if (id < m_counterNames.size()) {
      m_countersFile << frame.index << ',' << profiler_detail::CsvEscape(frame.scene)
                     << ',' << profiler_detail::CsvEscape(m_counterNames[id]) << ',' << value << '\n';
    }
  }

  if (!m_slowFramesFile || frame.cpuFrameMs < 1000.0 / 60.0) return;
  std::vector<std::pair<MetricId, const ScopeData *>> ranked;
  for (const auto &[id, scope] : frame.cpuScopes) ranked.emplace_back(id, &scope);
  std::sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) {
    return a.second->exclusiveMs > b.second->exclusiveMs;
  });
  for (size_t rank = 0; rank < std::min<size_t>(8, ranked.size()); ++rank) {
    const auto id = ranked[rank].first;
    if (id >= m_scopeMetadata.size()) continue;
    const auto &scope = *ranked[rank].second;
    const auto &meta = m_scopeMetadata[id];
    m_slowFramesFile << frame.index << ',' << profiler_detail::CsvEscape(frame.scene)
                     << ',' << frame.cpuFrameMs << ',' << frame.gpuFrameMs << ',' << rank + 1
                     << ',' << profiler_detail::CsvEscape(meta.name) << ',' << scope.exclusiveMs
                     << ',' << scope.inclusiveMs << ',' << scope.calls << ','
                     << profiler_detail::CsvEscape(meta.function) << ','
                     << profiler_detail::CsvEscape(meta.file) << ',' << meta.line << '\n';
  }
}

void Profiler::AccumulateFrame(const FrameData &frame) {
  auto &scene = m_sceneSummary[frame.scene];
  ++scene.frames;
  scene.cpu.Add(frame.cpuFrameMs);
  scene.overhead.Add(frame.profilerOverheadMs);
  scene.peakWorkingSetMb = std::max(scene.peakWorkingSetMb, frame.process.workingSetMb);
  scene.peakPrivateMb = std::max(scene.peakPrivateMb, frame.process.privateMb);
  if (frame.gpuValid) {
    ++scene.gpuValidFrames;
    ++m_gpuValidFrameCount;
    scene.gpu.Add(frame.gpuFrameMs);
  }
  for (const auto &[id, scope] : frame.cpuScopes) {
    auto &aggregate = m_cpuSummary[id];
    aggregate.inclusive.Add(scope.inclusiveMs);
    aggregate.exclusive.Add(scope.exclusiveMs);
    aggregate.calls += scope.calls;
  }
  for (const auto &scope : frame.gpuScopes) {
    auto &aggregate = m_gpuSummary[scope.name];
    aggregate.inclusive.Add(scope.milliseconds);
    aggregate.exclusive.Add(scope.milliseconds);
    ++aggregate.calls;
  }
  m_slowFrames.push_back({frame.index, frame.scene, frame.cpuFrameMs, frame.gpuFrameMs});
  std::sort(m_slowFrames.begin(), m_slowFrames.end(), [](const auto &a, const auto &b) {
    return a.cpuMs > b.cpuMs;
  });
  if (m_slowFrames.size() > 20) m_slowFrames.resize(20);
}

void Profiler::ReleaseOldFrameDetails() {
  constexpr size_t kRecentDetailedFrames = 512;
  if (m_frames.size() <= kRecentDetailedFrames) return;
  auto &frame = m_frames[m_frames.size() - kRecentDetailedFrames - 1];
  if (!frame.finalized) return;
  frame.cpuScopes.clear();
  frame.counters.clear();
  frame.gpuScopes.clear();
}

void Profiler::FinalizeFrame(FrameData &frame) {
  if (frame.finalized) return;
  if (frame.gpuValid) {
    const auto found = std::find_if(frame.gpuScopes.begin(), frame.gpuScopes.end(),
                                    [](const auto &scope) { return scope.name == "GPU.Frame"; });
    if (found != frame.gpuScopes.end()) frame.gpuFrameMs = found->milliseconds;
  }
  if (m_currentFrameIndex >= frame.index) {
    frame.gpuQueryLatencyFrames = static_cast<uint32_t>(
        std::min<uint64_t>(m_currentFrameIndex - frame.index, UINT32_MAX));
  }
  WriteFrameRows(frame);
  AccumulateFrame(frame);
  frame.finalized = true;
  m_frameLookup.erase(frame.index);
  ++m_finalizedFrameCount;
  if ((m_finalizedFrameCount % 256) == 0) {
    m_framesFile.flush(); m_scopesFile.flush(); m_countersFile.flush(); m_slowFramesFile.flush();
  }
  ReleaseOldFrameDetails();
}

void Profiler::LogIntervalReport() {
  if (m_lastReportFrame >= m_frames.size()) return;
  profiler_detail::BoundedStatistics cpu, gpu;
  std::unordered_map<MetricId, double> exclusive;
  double maxWorkingSet = 0.0, maxCpu = 0.0;
  for (size_t i = m_lastReportFrame; i < m_frames.size(); ++i) {
    const auto &frame = m_frames[i];
    cpu.Add(frame.cpuFrameMs);
    maxWorkingSet = std::max(maxWorkingSet, frame.process.workingSetMb);
    maxCpu = std::max(maxCpu, frame.process.cpuPercent);
    for (const auto &[id, scope] : frame.cpuScopes) exclusive[id] += scope.exclusiveMs;
    if (frame.gpuValid) gpu.Add(frame.gpuFrameMs);
  }
  std::vector<std::pair<MetricId, double>> ranked(exclusive.begin(), exclusive.end());
  std::sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
  LOG_INFO("Perf", "scene={} frames={} FPS={:.1f} CPU frame avg={:.3f}ms p95={:.3f}ms max={:.3f}ms GPU avg={:.3f}ms p95={:.3f}ms processCPU(max)={:.1f}% workingSet(max)={:.1f}MB",
           m_frames.back().scene, cpu.count, profiler_detail::FramesPerSecond(cpu.Average()),
           cpu.Average(), cpu.Percentile(0.95), cpu.maximum, gpu.Average(),
           gpu.Percentile(0.95), maxCpu, maxWorkingSet);
  for (size_t i = 0; i < std::min<size_t>(6, ranked.size()); ++i) {
    if (ranked[i].first >= m_scopeMetadata.size()) continue;
    const auto &meta = m_scopeMetadata[ranked[i].first];
    LOG_INFO("Perf", "  CPU exclusive #{} {} [{}:{}] avg={:.3f}ms/frame", i + 1,
             meta.name, meta.function, meta.line, ranked[i].second / static_cast<double>(cpu.count));
  }
}

void Profiler::WriteReports() {
  m_framesFile.flush(); m_scopesFile.flush(); m_countersFile.flush(); m_slowFramesFile.flush();
  if (m_outputDirectory.empty() || m_finalizedFrameCount == 0) return;
  std::ofstream out(m_outputDirectory / "performance_summary.txt");
  if (!out) { LOG_ERROR("Profiler", "Failed to open profiler summary output"); return; }
  out << std::fixed << std::setprecision(3)
      << "WikiGOLF detailed performance profile\nFrames: " << m_finalizedFrameCount
      << "\nGPU valid frames: " << m_gpuValidFrameCount
      << "\nPercentiles: exact up to 4096 samples per item; deterministic reservoir estimate beyond that.\n\n[Scene summary]\n";
  for (const auto &[name, s] : m_sceneSummary) {
    out << name << ": frames=" << s.frames << " GPU-valid=" << s.gpuValidFrames
        << " FPS(avg)=" << profiler_detail::FramesPerSecond(s.cpu.Average())
        << " CPU ms avg/p50/p95/p99/max=" << s.cpu.Average() << '/' << s.cpu.Percentile(.50)
        << '/' << s.cpu.Percentile(.95) << '/' << s.cpu.Percentile(.99) << '/' << s.cpu.maximum
        << " GPU ms avg/p95=" << s.gpu.Average() << '/' << s.gpu.Percentile(.95)
        << " profiler overhead avg/p95=" << s.overhead.Average() << '/' << s.overhead.Percentile(.95)
        << " memory peak working/private MB=" << s.peakWorkingSetMb << '/' << s.peakPrivateMb << '\n';
  }

  out << "\n[CPU scopes ranked by exclusive time]\nname,function,file,line,avg_inclusive_ms,avg_exclusive_ms,p95_inclusive_ms,p99_inclusive_ms,max_inclusive_ms,total_calls\n";
  std::vector<std::pair<double, std::string>> rows;
  for (const auto &[id, a] : m_cpuSummary) {
    if (id >= m_scopeMetadata.size()) continue;
    const auto &m = m_scopeMetadata[id];
    std::ostringstream row;
    row << profiler_detail::CsvEscape(m.name) << ',' << profiler_detail::CsvEscape(m.function)
        << ',' << profiler_detail::CsvEscape(m.file) << ',' << m.line << ','
        << a.inclusive.Average() << ',' << a.exclusive.Average() << ','
        << a.inclusive.Percentile(.95) << ',' << a.inclusive.Percentile(.99) << ','
        << a.inclusive.maximum << ',' << a.calls;
    rows.emplace_back(a.exclusive.Average(), row.str());
  }
  std::sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
  for (const auto &row : rows) out << row.second << '\n';

  out << "\n[GPU scopes ranked by time]\nname,avg_ms,p95_ms,p99_ms,max_ms,total_calls\n";
  rows.clear();
  for (const auto &[name, a] : m_gpuSummary) {
    std::ostringstream row;
    row << profiler_detail::CsvEscape(name) << ',' << a.inclusive.Average() << ','
        << a.inclusive.Percentile(.95) << ',' << a.inclusive.Percentile(.99) << ','
        << a.inclusive.maximum << ',' << a.calls;
    rows.emplace_back(a.exclusive.Average(), row.str());
  }
  std::sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
  for (const auto &row : rows) out << row.second << '\n';

  out << "\n[Top 20 slow CPU frames]\nframe,scene,cpu_ms,gpu_ms\n";
  for (const auto &f : m_slowFrames) out << f.index << ',' << profiler_detail::CsvEscape(f.scene) << ',' << f.cpuMs << ',' << f.gpuMs << '\n';
}

} // namespace core
