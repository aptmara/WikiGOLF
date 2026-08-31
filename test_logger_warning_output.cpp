#include "src/core/Logger.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {

std::string ReadAll(const std::filesystem::path &path) {
  std::ifstream stream(path);
  return {std::istreambuf_iterator<char>(stream),
          std::istreambuf_iterator<char>()};
}

bool Contains(const std::string &text, const std::string &value) {
  return text.find(value) != std::string::npos;
}

} // namespace

int main() {
  const std::filesystem::path regularPath = "test_logger_output.log";
  const std::filesystem::path warningPath = "test_logger_output_warnings.log";
  std::filesystem::remove(regularPath);
  std::filesystem::remove(warningPath);

  core::Logger::Instance().Initialize(regularPath.string());
  LOG_INFO("LoggerTest", "info-message");
  LOG_WARN("LoggerTest", "warning-message");
  LOG_ERROR("LoggerTest", "error-message");
  core::Logger::Instance().Shutdown();

  const std::string regularLog = ReadAll(regularPath);
  const std::string warningLog = ReadAll(warningPath);

  const bool regularHasAll = Contains(regularLog, "info-message") &&
                             Contains(regularLog, "warning-message") &&
                             Contains(regularLog, "error-message");
  const bool warningHasOnlyWarnings =
      !Contains(warningLog, "info-message") &&
      Contains(warningLog, "warning-message") &&
      Contains(warningLog, "error-message");

  std::filesystem::remove(regularPath);
  std::filesystem::remove(warningPath);

  if (!regularHasAll || !warningHasOnlyWarnings) {
    std::cerr << "Logger output filtering failed\n";
    return 1;
  }
  return 0;
}
