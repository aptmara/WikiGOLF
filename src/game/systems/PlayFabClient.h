#pragma once

#include <string>
#include <vector>

namespace game::systems {

struct PlayFabLeaderboardEntry {
  int position = 0;
  std::string playFabId;
  std::string displayName;
  int value = 0;
};

struct PlayFabResult {
  bool success = false;
  std::string errorMessage;
};

struct PlayFabLeaderboardResult : PlayFabResult {
  std::vector<PlayFabLeaderboardEntry> entries;
};

struct PlayFabProfile {
  std::string customId;
  std::string displayName;
};

class PlayFabClient {
public:
  static constexpr const char *kStrokesStatistic = "Strokes";
  static constexpr const char *kClearTimeStatistic = "ClearTimeMs";

  static PlayFabProfile LoadOrCreateProfile();
  static bool SaveDisplayName(const std::string &displayName);
  static bool HasDisplayName();

  PlayFabResult UpdateDisplayName(const std::string &displayName);
  PlayFabResult SubmitDailyResult(int strokes, int clearTimeMs);
  PlayFabLeaderboardResult FetchLeaderboard(const std::string &statisticName,
                                             int maxResults = 10);

private:
  enum class Authorization { None, SessionTicket, EntityToken };

  PlayFabResult Login();
  std::string Post(const std::wstring &path, const std::string &body,
                   Authorization authorization, int &statusCode);

  PlayFabProfile m_profile;
  std::string m_sessionTicket;
  std::string m_entityToken;
};

} // namespace game::systems
