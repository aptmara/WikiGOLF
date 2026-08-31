#include "PauseScene.h"
#include "../../audio/AudioSystem.h"
#include "../../core/Input.h"
#include "../../core/SceneManager.h"
#include "../components/UIButton.h"
#include "../components/UIText.h"
#include "TitleScene.h"

namespace game::scenes {

namespace {
constexpr int kOverlayLayer = 900;
constexpr float kButtonX = 490.0f;
constexpr float kButtonWidth = 300.0f;
constexpr float kButtonHeight = 56.0f;
} // namespace

PauseScene::PauseScene(bool canReturnToPreviousPage,
                       ActionCallback returnToPreviousPage)
    : m_canReturnToPreviousPage(canReturnToPreviousPage),
      m_returnToPreviousPage(std::move(returnToPreviousPage)) {}

void PauseScene::OnEnter(core::GameContext& ctx) {
  auto overlayEntity = CreateEntity(ctx.world);
  auto& overlay = ctx.world.Add<components::UIText>(overlayEntity);
  overlay.text = L"";
  overlay.x = 0.0f;
  overlay.y = 0.0f;
  overlay.width = 1280.0f;
  overlay.height = 720.0f;
  overlay.style.bgColor = {0.02f, 0.03f, 0.06f, 0.68f};
  overlay.visible = true;
  overlay.layer = static_cast<int>(kOverlayLayer);
  overlay.fullScreenCover = true; // レターボックス余白も含めて画面全体を暗転させる

  auto panelEntity = CreateEntity(ctx.world);
  auto& panel = ctx.world.Add<components::UIText>(panelEntity);
  panel.text = L"";
  panel.x = 400.0f;
  panel.y = 120.0f;
  panel.width = 480.0f;
  panel.height = 420.0f;
  panel.style.bgColor = {0.08f, 0.12f, 0.20f, 0.92f};
  panel.style.borderColor = {0.72f, 0.82f, 0.95f, 0.95f};
  panel.style.borderWidth = 2.0f;
  panel.style.cornerRadius = 18.0f;
  panel.visible = true;
  panel.layer = static_cast<int>(kOverlayLayer) + 1;

  auto titleEntity = CreateEntity(ctx.world);
  auto& title = ctx.world.Add<components::UIText>(titleEntity);
  title.text = L"PAUSE";
  title.x = 400.0f;
  title.y = 160.0f;
  title.width = 480.0f;
  title.height = 60.0f;
  title.style.fontSize = 40.0f;
  title.style.align = graphics::TextAlign::Center;
  title.style.color = {1.0f, 0.97f, 0.90f, 1.0f};
  title.style.hasShadow = true;
  title.visible = true;
  title.layer = static_cast<int>(kOverlayLayer) + 2;

  auto helpEntity = CreateEntity(ctx.world);
  auto& help = ctx.world.Add<components::UIText>(helpEntity);
  help.text =
      L"ESC: 再開\nBackspace: 前のページへ戻る\nM: マップ中は先にマップを閉じます";
  help.x = 440.0f;
  help.y = 220.0f;
  help.width = 400.0f;
  help.height = 80.0f;
  help.style.fontSize = 18.0f;
  help.style.align = graphics::TextAlign::Center;
  help.style.color = {0.86f, 0.90f, 0.98f, 1.0f};
  help.visible = true;
  help.layer = static_cast<int>(kOverlayLayer) + 2;

  CreateButton(ctx, L"再開", "resume", kButtonX, 320.0f, kButtonWidth, true);
  CreateButton(ctx, L"前のページへ戻る", "back_page", kButtonX, 392.0f,
               kButtonWidth, m_canReturnToPreviousPage);
  CreateButton(ctx, L"タイトルへ戻る", "goto_title", kButtonX, 464.0f,
               kButtonWidth, true);
}

void PauseScene::OnUpdate(core::GameContext& ctx) {
  if (!ctx.sceneManager) {
    return;
  }

  if (ctx.input.GetKeyDown(VK_ESCAPE)) {
    if (ctx.audio) {
      ctx.audio->PlaySE(ctx, "se_cancel.mp3", 0.45f);
    }
    ctx.sceneManager->PopScene();
    return;
  }

  if (m_canReturnToPreviousPage && ctx.input.GetKeyDown(VK_BACK)) {
    if (m_returnToPreviousPage) {
      m_returnToPreviousPage(ctx);
    }
    if (ctx.audio) {
      ctx.audio->PlaySE(ctx, "se_warp.mp3", 0.75f);
    }
    ctx.sceneManager->PopScene();
    return;
  }

  ctx.world.Query<components::UIButton>().Each(
      [&](ecs::Entity, components::UIButton& button) {
        if (!button.visible || button.state != components::ButtonState::Pressed ||
            !ctx.input.GetMouseButtonDown(0)) {
          return;
        }

        if (button.action == "resume") {
          if (ctx.audio) {
            ctx.audio->PlaySE(ctx, "se_cancel.mp3", 0.45f);
          }
          ctx.sceneManager->PopScene();
          return;
        }

        if (button.action == "back_page" && m_canReturnToPreviousPage) {
          if (m_returnToPreviousPage) {
            m_returnToPreviousPage(ctx);
          }
          if (ctx.audio) {
            ctx.audio->PlaySE(ctx, "se_warp.mp3", 0.75f);
          }
          ctx.sceneManager->PopScene();
          return;
        }

        if (button.action == "goto_title") {
          if (ctx.audio) {
            ctx.audio->PlaySE(ctx, "se_cancel.mp3", 0.55f);
          }
          ctx.sceneManager->ResetToScene(std::make_unique<TitleScene>());
        }
      });
}

void PauseScene::CreateButton(core::GameContext& ctx, const std::wstring& label,
                              const std::string& action, float x, float y,
                              float width, bool enabled) {
  auto buttonEntity = CreateEntity(ctx.world);
  auto& button = ctx.world.Add<components::UIButton>(buttonEntity);
  button = components::UIButton::Create(label, action, x, y, width,
                                        kButtonHeight);
  button.textStyle.fontSize = 24.0f;
  button.normalColor = {0.15f, 0.21f, 0.34f, 0.96f};
  button.hoverColor = {0.24f, 0.36f, 0.56f, 0.98f};
  button.pressedColor = {0.12f, 0.18f, 0.28f, 1.0f};
  button.disabledColor = {0.15f, 0.15f, 0.18f, 0.55f};
  button.visible = true;
  if (!enabled) {
    button.state = components::ButtonState::Disabled;
    button.textStyle.color = {0.60f, 0.64f, 0.70f, 1.0f};
  }
}

} // namespace game::scenes
