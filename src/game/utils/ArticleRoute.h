#pragma once

#include <string>
#include <vector>

namespace game::utils {

struct ArticleVisit {
  std::string title;
  bool returned = false;
};

inline void CompleteArticleRoute(std::vector<ArticleVisit> &route,
                                 const std::string &goal) {
  if (!goal.empty() && (route.empty() || route.back().title != goal)) {
    route.push_back({goal, false});
  }
}

} // namespace game::utils
