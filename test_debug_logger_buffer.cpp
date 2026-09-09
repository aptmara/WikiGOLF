#include "src/core/Logger.h"
#include <iostream>

int main() {
  auto &logger = core::Logger::Instance();
  logger.ClearRecentEntries();
  logger.Log(core::LogLevel::Info, "Gameplay", __FILE__, __LINE__, "ball hit");
  logger.Log(core::LogLevel::Warning, "Physics", __FILE__, __LINE__,
             "high velocity");

  const auto entries = logger.GetRecentEntries();
  if (entries.size() != 2 || entries[0].level != core::LogLevel::Info ||
      entries[0].category != "Gameplay" ||
      entries[0].text.find("ball hit") == std::string::npos ||
      entries[1].level != core::LogLevel::Warning ||
      entries[1].category != "Physics") {
    std::cerr << "Logger debug buffer did not preserve the existing log data\n";
    return 1;
  }

  logger.ClearRecentEntries();
  if (!logger.GetRecentEntries().empty()) {
    std::cerr << "Logger debug buffer clear failed\n";
    return 1;
  }
  return 0;
}
