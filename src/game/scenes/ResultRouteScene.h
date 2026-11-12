#pragma once

#include "ResultScene.h"
#include <array>

namespace game::scenes {

class ResultRouteScene : public core::Scene {
public:
  explicit ResultRouteScene(const ResultData &data) : m_data(data) {}
  const char *GetName() const override { return "ResultRouteScene"; }
  bool BlocksUnderlyingInput() const override { return true; }
  void OnEnter(core::GameContext &ctx) override;
  void OnUpdate(core::GameContext &ctx) override;
  void Render(core::GameContext &ctx) override;

private:
  struct Article {
    std::string title;
    std::vector<std::wstring> lines;
    float x = 0;
    float y = 0;
    float height = 0;
    bool returned = false;
  };
  struct Route {
    std::vector<Article> articles;
    float height = 0;
    float scroll = 0;
  };
  void BuildRoute(core::GameContext &ctx, Route &route,
                  const std::vector<game::utils::ArticleVisit> &visits);
  void ConsumeSearch(core::GameContext &ctx);
  ResultData m_data;
  std::array<Route, 2> m_routes;
  bool m_searchConsumed = false;
  bool m_searchSucceeded = false;
  int m_draggingRoute = -1;
  float m_dragOffset = 0;
};

} // namespace game::scenes
