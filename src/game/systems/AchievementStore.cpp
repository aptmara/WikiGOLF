#include "AchievementStore.h"
#include "../../core/SavePaths.h"
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace game::systems {

namespace {
constexpr const char *kAchievementFileName = "achievements.txt";

std::string TrimCopy(const std::string &value) {
  const std::size_t begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return {};
  }
  const std::size_t end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1);
}

std::vector<std::string> SplitCsv(const std::string &value) {
  std::vector<std::string> parts;
  std::stringstream stream(value);
  std::string token;
  while (std::getline(stream, token, ',')) {
    token = TrimCopy(token);
    if (!token.empty()) {
      parts.push_back(token);
    }
  }
  return parts;
}

std::vector<AchievementId> ParseUnlockedList(const std::string &value) {
  std::vector<AchievementId> ids;
  for (const std::string &token : SplitCsv(value)) {
    const int rawId = std::atoi(token.c_str());
    if (rawId >= 0 &&
        rawId < static_cast<int>(AchievementId::Count)) {
      ids.push_back(static_cast<AchievementId>(rawId));
    }
  }
  return ids;
}

std::string SerializeUnlockedList(const std::vector<AchievementId> &ids) {
  std::string text;
  for (std::size_t i = 0; i < ids.size(); ++i) {
    if (i > 0) {
      text += ',';
    }
    text += std::to_string(static_cast<int>(ids[i]));
  }
  return text;
}
} // namespace

AchievementProgress AchievementStore::Load() {
  AchievementProgress progress;
  std::ifstream input(core::SaveFilePath(kAchievementFileName));
  if (!input) {
    return progress;
  }

  std::string line;
  while (std::getline(input, line)) {
    const std::size_t separator = line.find('=');
    if (separator == std::string::npos) {
      continue;
    }
    const std::string key = TrimCopy(line.substr(0, separator));
    const std::string value = TrimCopy(line.substr(separator + 1));

    if (key == "UNLOCKED") {
      progress.unlocked = ParseUnlockedList(value);
    } else if (key == "TOTAL_CLEARS") {
      progress.totalClears = std::atoi(value.c_str());
    } else if (key == "TOTAL_PAGES_VISITED") {
      progress.totalPagesVisited = std::atoi(value.c_str());
    } else if (key == "BEST_STROKES") {
      progress.bestStrokes = std::atoi(value.c_str());
    } else if (key == "BEST_CLEAR_TIME_MS") {
      progress.bestClearTimeMs = std::atoi(value.c_str());
    } else if (key == "DAILY_STREAK") {
      progress.dailyStreak = std::atoi(value.c_str());
    } else if (key == "LAST_DAILY_DATE") {
      progress.lastDailyDateIso = value;
    }
  }
  return progress;
}

bool AchievementStore::Save(const AchievementProgress &progress) {
  std::ofstream output(core::SaveFilePath(kAchievementFileName),
                       std::ios::binary | std::ios::trunc);
  if (!output) {
    return false;
  }
  output << "UNLOCKED=" << SerializeUnlockedList(progress.unlocked) << '\n';
  output << "TOTAL_CLEARS=" << progress.totalClears << '\n';
  output << "TOTAL_PAGES_VISITED=" << progress.totalPagesVisited << '\n';
  output << "BEST_STROKES=" << progress.bestStrokes << '\n';
  output << "BEST_CLEAR_TIME_MS=" << progress.bestClearTimeMs << '\n';
  output << "DAILY_STREAK=" << progress.dailyStreak << '\n';
  output << "LAST_DAILY_DATE=" << progress.lastDailyDateIso << '\n';
  return true;
}

} // namespace game::systems
