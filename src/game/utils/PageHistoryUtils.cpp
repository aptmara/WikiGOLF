#include "PageHistoryUtils.h"

namespace game::utils {

std::optional<std::string>
ConsumePreviousPage(std::vector<std::string>& pathHistory) {
  if (pathHistory.size() < 2) {
    return std::nullopt;
  }

  const std::string previousPage = pathHistory[pathHistory.size() - 2];
  pathHistory.resize(pathHistory.size() - 2);
  return previousPage;
}

} // namespace game::utils
