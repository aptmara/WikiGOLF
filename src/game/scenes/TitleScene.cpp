/**
 * @file TitleScene.cpp
 * @brief タイトル画面シーンの実装
 */

#include "TitleScene.h"
#include "../../audio/AudioSystem.h"
#include "../../core/GameContext.h"
#include "../../core/Input.h"
#include "../../core/SceneManager.h"
#include "../components/Transform.h"
#include "../components/UIButton.h"
#include "../components/UIImage.h"
#include "../components/UIText.h"
#include "LoadingScene.h"
#include "WikiGolfScene.h"

namespace game::scenes {

void TitleScene::OnEnter(core::GameContext &ctx) {
  LOG_INFO("TitleScene", "OnEnter");

  // 背景: 今は不要（必要に応じて専用の背景画像を追加）
  m_bgEntity = ecs::Entity(); // 無効なエンティティ

  // タイトルテキスト
  auto title = ctx.world.CreateEntity();
  auto &tText = ctx.world.Add<components::UIText>(title);
  tText.text = L"WIKI GOLF";
  tText.x = 400.0f;
  tText.y = 200.0f;
  tText.style = graphics::TextStyle::
      ModernBlack(); // フォントサイズ調整機能がないので位置で調整
  tText.visible = true;
  m_titleTextEntity = title;

  // スタートボタンテキスト
  auto start = ctx.world.CreateEntity();
  auto &sText = ctx.world.Add<components::UIText>(start);
  sText.text = L"CLICK TO START";
  sText.x = 450.0f;
  sText.y = 400.0f;
  sText.style = graphics::TextStyle::ModernBlack();
  sText.visible = true;
  m_startEntity = start;

  // BGM再生
  if (ctx.audio) {
    ctx.audio->PlayBGM(ctx, "bgm_title.mp3");
  }

  // マウスカーソル表示
  ctx.input.SetMouseCursorVisible(true);

  LOG_INFO("TitleScene", "OnEnter complete");
}

void TitleScene::OnUpdate(core::GameContext &ctx) {
  // クリックでゲーム開始
  if (ctx.input.GetMouseButtonDown(0)) {
    if (ctx.audio) {
      ctx.audio->PlaySE(ctx, "se_decide.mp3");
    }

    if (ctx.sceneManager) {
      // LoadingSceneを経由してWikiGolfSceneへ遷移
      auto loadingScene = std::make_unique<LoadingScene>(
          []() { return std::make_unique<WikiGolfScene>(); });
      ctx.sceneManager->ChangeScene(std::move(loadingScene));
    }
  }

  // ESCで終了
  if (ctx.input.GetKeyDown(VK_ESCAPE)) {
    ctx.shouldClose = true;
  }
}

void TitleScene::OnExit(core::GameContext &ctx) {
  LOG_INFO("TitleScene", "OnExit");
  // m_bgEntityは使用していないため破棄不要
  ctx.world.DestroyEntity(m_titleTextEntity);
  ctx.world.DestroyEntity(m_startEntity);
  ctx.world.DestroyEntity(m_exitEntity);
}

} // namespace game::scenes
