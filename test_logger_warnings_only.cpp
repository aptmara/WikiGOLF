// WIKIGOLF_LOG_WARNINGS_ONLY（製品版・体験版）のログ出力を確かめる
//   - 情報ログだけなら、ファイルを 1 つも作らない
//   - 警告・エラーは <名前>_warnings.log にだけ書く
//   - 通常ログと Perf ログのファイルは作らない
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

int g_failures = 0;

void Expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    ++g_failures;
  }
}

} // namespace

int main() {
  const std::filesystem::path regularPath = "test_logger_quiet.log";
  const std::filesystem::path warningPath = "test_logger_quiet_warnings.log";
  const std::filesystem::path perfPath = "test_logger_quiet_perf.log";
  auto removeAll = [&] {
    std::filesystem::remove(regularPath);
    std::filesystem::remove(warningPath);
    std::filesystem::remove(perfPath);
  };
  removeAll();

  // 情報ログだけなら何も作らない（マクロ自体が消えるが、直接呼んでも書かない）
  core::Logger::Instance().Initialize(regularPath.string());
  LOG_INFO("LoggerTest", "info-macro");
  core::Logger::Instance().Log(core::LogLevel::Info, "LoggerTest", __FILE__,
                               __LINE__, "info-direct");
  core::Logger::Instance().Log(core::LogLevel::Info, "Perf", __FILE__, __LINE__,
                               "perf-direct");
  core::Logger::Instance().Shutdown();
  Expect(!std::filesystem::exists(regularPath), "info only: regular log created");
  Expect(!std::filesystem::exists(warningPath), "info only: warning log created");
  Expect(!std::filesystem::exists(perfPath), "info only: perf log created");

  // 警告・エラーは警告用ファイルにだけ書く
  core::Logger::Instance().Initialize(regularPath.string());
  LOG_INFO("LoggerTest", "info-message");
  LOG_WARN("LoggerTest", "warning-message");
  LOG_ERROR("LoggerTest", "error-message");
  core::Logger::Instance().Shutdown();

  const std::string warningLog = ReadAll(warningPath);
  Expect(Contains(warningLog, "warning-message"), "warning not written");
  Expect(Contains(warningLog, "error-message"), "error not written");
  Expect(!Contains(warningLog, "info-message"), "info written to warning log");
  Expect(!Contains(warningLog, "=== Diagnostics ==="),
         "diagnostics written without provider");
  Expect(!std::filesystem::exists(regularPath), "regular log created");
  Expect(!std::filesystem::exists(perfPath), "perf log created");
  removeAll();

  // 診断情報は最初の警告の前に 1 回だけ書き、診断情報もメッセージも個人情報を伏せる
  static int providerCalls = 0;
  core::Logger::Instance().SetDiagnosticsProvider([]() -> std::string {
    ++providerCalls;
    return "GPU: Test Adapter\nPath: C:\\Users\\taro\\x\n";
  });
  core::Logger::Instance().SetPrivacyReplacements(
      {{"C:\\Users\\taro", "%USERPROFILE%", false}, {"taro", "<USER>", true}});
  core::Logger::Instance().Initialize(regularPath.string());
  LOG_INFO("LoggerTest", "info-before");
  Expect(providerCalls == 0, "provider called for info");
  LOG_WARN("LoggerTest", "cannot open C:\\Users\\taro\\settings.ini for taro");
  LOG_ERROR("LoggerTest", "second-error");
  core::Logger::Instance().Shutdown();

  const std::string diagnosedLog = ReadAll(warningPath);
  const std::size_t diagnosticsPos = diagnosedLog.find("=== Diagnostics ===");
  const std::size_t warningPos = diagnosedLog.find("cannot open");
  Expect(providerCalls == 1, "provider not called exactly once");
  Expect(diagnosticsPos == 0, "diagnostics not at the top");
  Expect(diagnosedLog.find("=== Diagnostics ===", diagnosticsPos + 1) ==
             std::string::npos,
         "diagnostics written twice");
  Expect(Contains(diagnosedLog, "GPU: Test Adapter"), "diagnostics body missing");
  Expect(warningPos != std::string::npos && warningPos > diagnosticsPos,
         "warning not after diagnostics");
  Expect(Contains(diagnosedLog, "%USERPROFILE%\\settings.ini for <USER>"),
         "message not redacted");
  Expect(Contains(diagnosedLog, "Path: %USERPROFILE%\\x"), "diagnostics not redacted");
  Expect(!Contains(diagnosedLog, "taro"), "private name left in log");
  Expect(Contains(diagnosedLog, "second-error"), "second error missing");

  core::Logger::Instance().SetDiagnosticsProvider(nullptr);
  core::Logger::Instance().SetPrivacyReplacements({});
  removeAll();
  return g_failures == 0 ? 0 : 1;
}
