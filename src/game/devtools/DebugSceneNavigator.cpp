#include "DebugSceneNavigator.h"

#include "../../core/GameContext.h"
#include "../../core/SceneManager.h"
#include "../scenes/LoadingScene.h"
#include "../scenes/ResultScene.h"
#include "../scenes/SettingsScene.h"
#include "../scenes/TitleScene.h"
#include "../scenes/TitleSceneSupport.h"
#include "../scenes/WikiGolfScene.h"
#include <memory>

namespace game::debug {
namespace {

std::unique_ptr<core::Scene> CreateScene(DebugSceneTarget target) {
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

} // namespace

void DebugSceneNavigator::Navigate(core::GameContext &ctx,
                                   DebugSceneTarget target,
                                   bool replaceCurrent) {
  if (!ctx.sceneManager) {
    return;
  }
  if (target == DebugSceneTarget::Loading ||
      target == DebugSceneTarget::Golf) {
    game::scenes::title_scene_detail::ResetStandardStartData(ctx);
  }

  auto scene = CreateScene(target);
  if (target == DebugSceneTarget::Settings && !replaceCurrent) {
    ctx.sceneManager->PushScene(std::move(scene));
  } else if (replaceCurrent) {
    ctx.sceneManager->ChangeScene(std::move(scene));
  } else {
    ctx.sceneManager->ResetToScene(std::move(scene));
  }
}

} // namespace game::debug
