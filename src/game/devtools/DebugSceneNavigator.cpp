#include "DebugSceneNavigator.h"

#include "../../core/GameContext.h"
#include "../../core/Scene.h"
#include "../../core/SceneManager.h"
#include "../../ecs/World.h"
#include "../scenes/LoadingScene.h"
#include "../scenes/ResultScene.h"
#include "../scenes/SettingsScene.h"
#include "../scenes/TitleScene.h"
#include "../scenes/TitleSceneSupport.h"
#include "../scenes/WikiGolfScene.h"
#include "../systems/PostProcessSystem.h"
#include <memory>

namespace game::debug {
namespace {

std::unique_ptr<core::Scene> CreateScene(DebugSceneTarget target) {
  target = EntryTarget(target);
  using namespace game::scenes;
  switch (target) {
  case DebugSceneTarget::Title:
    return std::make_unique<TitleScene>();
  case DebugSceneTarget::Loading:
    return std::make_unique<LoadingScene>(
        []() { return std::make_unique<WikiGolfScene>(false); });
  case DebugSceneTarget::Golf:
    return std::make_unique<WikiGolfScene>(false);
  case DebugSceneTarget::Result: {
    ResultData data;
    data.targetPage = "Debug forced transition page";
    data.shotCount = 5;
    data.par = 4;
    data.pathHistory = {"TitleScene", "Debug Scene Selector"};
    data.isNewRecord = true;
    return std::make_unique<ResultScene>(data);
  }
  case DebugSceneTarget::Settings:
    return std::make_unique<SettingsScene>();
  }
  return nullptr;
}

class CleanSceneTransition final : public core::Scene {
public:
  explicit CleanSceneTransition(DebugSceneTarget target) : m_target(target) {}

  const char *GetName() const override { return "DebugSceneTransition"; }

  void OnEnter(core::GameContext &ctx) override {
    ctx.world.Reset();
    if (ctx.postProcess) {
      ctx.postProcess->ResetToDefaults();
    }
    if (EntryTarget(m_target) == DebugSceneTarget::Loading) {
      game::scenes::title_scene_detail::ResetStandardStartData(ctx);
    }
  }

  void OnUpdate(core::GameContext &ctx) override {
    if (m_requested || !ctx.sceneManager) {
      return;
    }
    m_requested = true;
    ctx.sceneManager->ChangeScene(CreateScene(m_target));
  }

private:
  DebugSceneTarget m_target;
  bool m_requested = false;
};

} // namespace

void DebugSceneNavigator::Navigate(core::GameContext &ctx,
                                   DebugSceneTarget target,
                                   bool replaceCurrent) {
  if (!ctx.sceneManager) {
    return;
  }
  if (RequiresCleanReset(target, replaceCurrent)) {
    ctx.sceneManager->ResetToScene(
        std::make_unique<CleanSceneTransition>(target));
  } else if (!replaceCurrent) {
    ctx.sceneManager->PushScene(CreateScene(target));
  } else {
    ctx.sceneManager->ChangeScene(CreateScene(target));
  }
}

} // namespace game::debug
