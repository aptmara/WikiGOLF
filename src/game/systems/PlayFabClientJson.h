#pragma once

#include "PlayFabClient.h"
#include <string>
#include <vector>

namespace game::systems {

bool ParsePlayFabLeaderboard(
    const std::string &response,
    std::vector<PlayFabLeaderboardEntry> &entries);

} // namespace game::systems
