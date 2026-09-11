#include "PlayFabClientJson.h"
#include "WikiClientJson.h"
#include <algorithm>

namespace game::systems {

bool ParsePlayFabLeaderboard(
    const std::string &response,
    std::vector<PlayFabLeaderboardEntry> &entries) {
  entries.clear();
  const size_t listKey = response.find("\"Rankings\":");
  const size_t arrayStart = response.find('[', listKey);
  if (listKey == std::string::npos || arrayStart == std::string::npos) {
    return false;
  }

  const size_t arrayEnd = wiki_json::SkipValue(response, arrayStart);
  size_t objectStart = response.find('{', arrayStart);
  while (objectStart != std::string::npos && objectStart < arrayEnd) {
    const size_t objectEnd =
        std::min(wiki_json::SkipValue(response, objectStart), arrayEnd);
    PlayFabLeaderboardEntry entry;
    size_t next = 0;
    wiki_json::ExtractIntField(response, "\"Rank\":", objectStart,
                               objectEnd, entry.position);
    std::string score;
    if (wiki_json::ExtractStringField(response, "\"Scores\":[\"", objectStart,
                                      objectEnd, score, next)) {
      try {
        entry.value = std::stoi(score);
      } catch (...) {
        entry.value = 0;
      }
    }
    wiki_json::ExtractStringField(response, "\"Id\":\"", objectStart,
                                  objectEnd, entry.playFabId, next);
    wiki_json::ExtractStringField(response, "\"DisplayName\":\"", objectStart,
                                  objectEnd, entry.displayName, next);
    entries.push_back(std::move(entry));
    objectStart = response.find('{', objectEnd);
  }
  return true;
}

} // namespace game::systems
