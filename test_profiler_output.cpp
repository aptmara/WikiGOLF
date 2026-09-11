#include "src/core/Profiler.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {

std::string ReadFile(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

} // namespace

int main() {
  const auto outputRoot =
      std::filesystem::current_path() / "profiler_output_test";
  std::error_code error;
  std::filesystem::remove_all(outputRoot, error);

  auto &profiler = core::Profiler::Instance();
  profiler.Initialize(outputRoot);
  const uint64_t frameIndex = profiler.BeginFrame("TestScene", 7);
  profiler.BeginScope("Test.Scope", "TestFunction", "TestFile.cpp", 42);
  profiler.EndScope();
  profiler.SetCounter("Test.Counter", 3.0);
  profiler.EndFrame();

  core::GpuFrameSample gpu;
  gpu.frameIndex = frameIndex;
  gpu.valid = true;
  gpu.pipelineValid = true;
  gpu.scopes.push_back({"GPU.Frame", 1.25});
  gpu.pipeline.inputAssemblerVertices = 12;
  profiler.SubmitGpuFrame(std::move(gpu));
  profiler.Shutdown();

  const auto session = profiler.GetOutputDirectory();
  const std::string scopes = ReadFile(session / "performance_scopes.csv");
  const std::string frames = ReadFile(session / "performance_frames.csv");
  const std::string summary = ReadFile(session / "performance_summary.txt");
  if (scopes.find("Test.Scope") == std::string::npos ||
      scopes.find("TestFunction") == std::string::npos ||
      scopes.find("TestFile.cpp,42") == std::string::npos ||
      frames.find("pipeline_stats_valid") == std::string::npos ||
      frames.find("TestScene") == std::string::npos ||
      summary.find("Frames: 1") == std::string::npos) {
    std::cerr << "Profiler detailed output validation failed\n";
    return 1;
  }
  return 0;
}
