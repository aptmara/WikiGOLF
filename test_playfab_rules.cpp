#include "game/systems/PlayFabRules.h"
#include "game/systems/PlayFabClientJson.h"
#include <cassert>

int main() {
  using game::systems::FormatClearTime;
  using game::systems::IsValidPlayFabDisplayName;

  assert(!IsValidPlayFabDisplayName(L"ab"));
  assert(IsValidPlayFabDisplayName(L"Player"));
  assert(!IsValidPlayFabDisplayName(std::wstring(26, L'a')));
  assert(!IsValidPlayFabDisplayName(L"bad\nname"));

  assert(FormatClearTime(0) == L"0:00.0");
  assert(FormatClearTime(65432) == L"1:05.4");
  assert(FormatClearTime(-1) == L"0:00.0");

  std::vector<game::systems::PlayFabLeaderboardEntry> entries;
  const std::string leaderboard =
      R"({"data":{"Rankings":[{"DisplayName":"Alice","Entity":{"Id":"A1","Type":"title_player_account"},"Rank":1,"Scores":["7"]},{"DisplayName":"Bob","Entity":{"Id":"B2","Type":"title_player_account"},"Rank":2,"Scores":["12"]}]}})";
  assert(game::systems::ParsePlayFabLeaderboard(leaderboard, entries));
  assert(entries.size() == 2);
  assert(entries[0].position == 1);
  assert(entries[0].displayName == "Alice");
  assert(entries[0].playFabId == "A1");
  assert(entries[0].value == 7);
  assert(!game::systems::ParsePlayFabLeaderboard("{}", entries));
  return 0;
}
