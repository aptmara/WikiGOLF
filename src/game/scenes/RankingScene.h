#pragma once

#include "../../core/Scene.h"
#include "../systems/PlayFabClient.h"
#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace game::scenes {

class RankingScene : public core::Scene {
public:
  const char *GetName() const override { return "RankingScene"; }
  bool BlocksUnderlyingInput() const override { return true; }

  void OnEnter(core::GameContext &ctx) override;
  void OnUpdate(core::GameContext &ctx) override;
  void OnExit(core::GameContext &ctx) override;
  void Render(core::GameContext &ctx) override;

private:
  struct LoadData {
    game::systems::PlayFabLeaderboardResult strokes;
    game::systems::PlayFabLeaderboardResult clearTime;
    game::systems::PlayFabResult nameUpdate;
  };

  struct AsyncState {
    std::atomic_bool completed = false;
    LoadData data;
  };

  enum class View { Strokes, ClearTime };

  void StartLoad(const std::string &displayName = {});
  void RefreshUI(core::GameContext &ctx);

  ecs::Entity m_statusText = 0;
  ecs::Entity m_rowsText = 0;
  ecs::Entity m_nameInputText = 0;
  ecs::Entity m_nameSubmitButton = 0;
  ecs::Entity m_strokesButton = 0;
  ecs::Entity m_timeButton = 0;
  std::wstring m_nameInput;
  bool m_enteringName = false;
  bool m_loading = false;
  View m_view = View::Strokes;
  std::shared_ptr<AsyncState> m_asyncState;
  LoadData m_data;
};

} // namespace game::scenes
