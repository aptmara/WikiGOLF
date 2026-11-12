#include "src/game/utils/PageHistoryUtils.h"
#include "src/game/utils/ArticleRoute.h"
#include <cassert>
#include <vector>

int main() {
  {
    std::vector<game::utils::ArticleVisit> route = {{"Start", false}};
    game::utils::CompleteArticleRoute(route, "Goal");
    assert(route.size() == 2);
    assert(route.front().title == "Start");
    assert(route.back().title == "Goal");
    game::utils::CompleteArticleRoute(route, "Goal");
    assert(route.size() == 2);
  }
  {
    std::vector<game::utils::ArticleVisit> route = {
        {"A", false}, {"B", false}, {"A", true}, {"C", false}};
    game::utils::CompleteArticleRoute(route, "Goal");
    assert(route.size() == 5);
    assert(route[2].returned);
    assert(route.back().title == "Goal");
  }
  {
    std::vector<std::string> history = {"A"};
    const auto previous = game::utils::ConsumePreviousPage(history);
    assert(!previous.has_value());
    assert(history.size() == 1);
    assert(history[0] == "A");
  }

  {
    std::vector<std::string> history = {"A", "B"};
    const auto previous = game::utils::ConsumePreviousPage(history);
    assert(previous.has_value());
    assert(*previous == "A");
    assert(history.empty());
  }

  {
    std::vector<std::string> history = {"A", "B", "C", "D"};
    const auto previous = game::utils::ConsumePreviousPage(history);
    assert(previous.has_value());
    assert(*previous == "C");
    assert(history.size() == 2);
    assert(history[0] == "A");
    assert(history[1] == "B");
  }

  return 0;
}
