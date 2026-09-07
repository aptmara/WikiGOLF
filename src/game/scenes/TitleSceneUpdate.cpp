/**
 * @file TitleSceneUpdate.cpp
 * @brief TitleSceneUpdate の実装
*/

#include "ResultScene.h"
#include "TitleScene.h"
#include "TitleSceneSupport.h"
#include "../../audio/AudioSystem.h"
#include "../../core/GameContext.h"
#include "../../core/Input.h"
#include "../../core/SceneManager.h"
#include "../../core/StringUtils.h"
#include "../../graphics/GraphicsDevice.h"
#include "../../graphics/VideoPlayer.h"
#include "../../graphics/TextRenderer.h"
#include "../../graphics/SkyboxTextureGenerator.h"
#include "../components/Camera.h"
#include "../components/MeshRenderer.h"
#include "../components/PhysicsComponents.h"
#include "../components/Skybox.h"
#include "../components/Transform.h"
#include "../components/UIButton.h"
#include "../components/UIImage.h"
#include "../components/UIText.h"
#include "../components/UIText.h"
#include "../components/WikiComponents.h"
#include "../systems/SkyboxRenderSystem.h"
#include "../systems/TerrainGenerator.h"
#include "../systems/WikiClient.h"
#include "../../core/StringUtils.h"
#include "LoadingScene.h"
#include "SettingsScene.h"
#include "WikiGolfScene.h"
#include <filesystem>
#include <fstream>
#include <thread>
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <windows.h>
#include <shellapi.h> // ShellExecuteA用


namespace game::scenes {

using namespace DirectX;

/**
 * @brief シーンの毎フレーム更新処理を行います。
*/
void TitleScene::OnUpdate(core::GameContext &ctx) {
  // Cheat code to force transition to ResultScene
  if (ctx.input.GetKey(VK_CONTROL) &&
      ctx.input.GetKey('Z') &&
      ctx.input.GetKey('X') &&
      ctx.input.GetKey('C') &&
      ctx.input.GetKey('V'))
  {
      ResultData debugData;
      debugData.targetPage = "Debug forced transition page";
      debugData.shotCount = 5;
      debugData.par = 4;
      debugData.pathHistory = { "TitleScene", "CheatJumper" };
      debugData.isNewRecord = true;
      ctx.sceneManager->ChangeScene(std::make_unique<ResultScene>(debugData));
      return;
  }

  if (m_state == TitleState::IntroVideo) {
    if (m_videoPlayer) {
      m_videoPlayer->Update(ctx.graphics.GetContext(), ctx.dt);

      // 動画再生とロードが完了したか確認
      bool loadReady = true;
      if (m_startupLoadTask.valid()) {
        loadReady = m_startupLoadTask.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
      }
      if (m_videoPlayer->IsFinished() && loadReady) {
        m_state = TitleState::MainMenu;
        StopIntroAudio(ctx);
        m_videoPlayer->Stop();
        m_videoPlayer.reset();
        FinalizeStartupLoad(ctx);
      }
    } else {
      // 動画再生に失敗した場合のフォールバック
      bool loadReady = true;
      if (m_startupLoadTask.valid()) {
        loadReady = m_startupLoadTask.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
      }
      if (loadReady) {
        m_state = TitleState::MainMenu;
        StopIntroAudio(ctx);
        FinalizeStartupLoad(ctx);
      }
    }
    return;
  }

  m_time += ctx.dt;

  auto *skybox = ctx.world.Get<components::Skybox>(m_skyboxEntity);
  if (skybox) {
    skybox->time = m_time;
  }

  auto *globeTr = ctx.world.Get<components::Transform>(m_globeEntity);
  if (globeTr) {
    XMVECTOR gq = XMQuaternionRotationRollPitchYaw(XMConvertToRadians(15.0f), m_time * 0.25f, 0.0f);
    XMStoreFloat4(&globeTr->rotation, gq);
  }

  // UIButton の状態をポーリングしてアクションを処理
  bool newGame = false;
  bool startTutorial = false;
  bool exitGame = false;
  bool prevHoveredAny = false;

  if (m_startConnectionChecking) {
    m_startConnectionElapsed += ctx.dt;
  }

  const bool startConnectionCompleted =
      m_startConnectionChecking && m_startConnectionState &&
      m_startConnectionState->completed.load(std::memory_order_acquire);
  const bool startConnectionTimedOut =
      m_startConnectionChecking &&
      m_startConnectionElapsed >= title_scene_detail::kStartConnectionTimeoutSeconds;

  if (startConnectionCompleted || startConnectionTimedOut) {
    std::string res;
    if (startConnectionCompleted) {
      res = m_startConnectionState->title;
    }
    m_startConnectionState.reset();
    m_startConnectionChecking = false;
    m_startConnectionElapsed = 0.0f;

    if (startConnectionCompleted && res != "Error" && !res.empty()) {
      LOG_INFO("TitleScene", "Wikipedia connection test passed. Title: {}",
               res);
      if (ctx.audio)
        ctx.audio->PlaySE(ctx, "se_shot_hard.mp3", 0.5f);
      newGame = true;
    } else {
      if (startConnectionTimedOut) {
        LOG_WARN("TitleScene", "Wikipedia connection test timed out.");
      } else {
        LOG_WARN("TitleScene", "Wikipedia connection test failed.");
      }
      if (ctx.audio)
        ctx.audio->PlaySE(ctx, "se_cancel.mp3", 0.5f);
      auto *ptxt = ctx.world.Get<components::UIText>(m_popupTextEntity);
      if (ptxt) {
        ptxt->text = L"Connection Failed\n\nWikiへの接続に失敗しました";
      }
      m_popupTimer = 2.0f;
    }
  }

  if (m_state == TitleState::CourseSelect) {
    UpdateCourseSelect(ctx);
  } else if (!newGame) {
    // メインメニュー状態の更新
    ctx.world.Query<components::UIButton>().Each([&](ecs::Entity, components::UIButton &btn) {
    if (!btn.visible) return;

    bool isHovered = (btn.state == components::ButtonState::Hovered ||
                     btn.state == components::ButtonState::Pressed);

    // ホバー内入時にSE
    if (isHovered && !prevHoveredAny) {
      prevHoveredAny = true;
    }

    // ホバー状態に応じてリンクのテキスト色を変更
    if (btn.action == "wikipedia") {
      if (isHovered) {
        btn.textStyle.color = DirectX::XMFLOAT4{0.2f, 0.5f, 1.0f, 1.0f}; // ホバー時: 明るい青
      } else {
        btn.textStyle.color = DirectX::XMFLOAT4{0.4f, 0.6f, 0.9f, 1.0f}; // 通常時: 青
      }
    } else {
      // ホバー時は黒文字、通常時は白文字
      if (isHovered) {
        btn.textStyle.color = DirectX::XMFLOAT4{0.05f, 0.05f, 0.05f, 1.0f}; // 黒
      } else {
        btn.textStyle.color = DirectX::XMFLOAT4{0.95f, 0.95f, 0.95f, 1.0f}; // 白
      }
    }

    // クリック判定 (Pressed 状態の瞬間をトリガーとする)
    if (btn.state == components::ButtonState::Pressed && ctx.input.GetMouseButtonDown(0)) {
      if (m_startConnectionChecking && btn.action != "new_game") {
        return;
      }

      if (btn.action == "new_game") {
        if (!m_startConnectionChecking) {
          LOG_INFO("TitleScene", "Testing Wikipedia connection...");
          m_startConnectionChecking = true;
          m_startConnectionElapsed = 0.0f;
          m_startConnectionState = std::make_shared<StartConnectionState>();
          auto state = m_startConnectionState;
          std::thread([state]() {
            game::systems::WikiClient client;
            state->title = client.FetchRandomPageTitle();
            state->completed.store(true, std::memory_order_release);
          }).detach();
          if (ctx.audio)
            ctx.audio->PlaySE(ctx, "se_shot_soft.mp3", 0.5f);
          auto *ptxt = ctx.world.Get<components::UIText>(m_popupTextEntity);
          if (ptxt) {
            ptxt->text = L"Connecting...\n\nWikiへの接続を確認中です";
          }
          m_popupTimer = 0.8f;
        } else {
          auto *ptxt = ctx.world.Get<components::UIText>(m_popupTextEntity);
          if (ptxt) {
            ptxt->text = L"Connecting...\n\nWikiへの接続を確認中です";
          }
          m_popupTimer = std::max(m_popupTimer, 0.8f);
        }
      } else if (btn.action == "tutorial") {
        if (ctx.audio) ctx.audio->PlaySE(ctx, "se_shot_soft.mp3", 0.5f);
        startTutorial = true;
      } else if (btn.action == "course") {
        if (ctx.audio) ctx.audio->PlaySE(ctx, "se_shot_soft.mp3", 0.5f);
        m_state = TitleState::CourseSelect;
        SetMainMenuVisible(ctx, false);
        SetCourseSelectVisible(ctx, true);
        m_focusIndex = 0;
      } else if (btn.action == "option") {
        if (ctx.audio) ctx.audio->PlaySE(ctx, "se_shot_soft.mp3", 0.5f);
        ctx.sceneManager->PushScene(std::make_unique<SettingsScene>());
      } else if (btn.action == "daily" || btn.action == "ranking" || btn.action == "achievement") {
        if (ctx.audio) ctx.audio->PlaySE(ctx, "se_cancel.mp3", 0.5f);
        auto *ptxt = ctx.world.Get<components::UIText>(m_popupTextEntity);
        if (ptxt) {
            ptxt->text = L"Coming Soon...\n\n現在開発中です";
        }
        m_popupTimer = 2.0f; // 2秒間表示
      } else if (btn.action == "exit") {
        exitGame = true;
      } else if (btn.action == "wikipedia") {
        if (ctx.audio) ctx.audio->PlaySE(ctx, "se_shot_soft.mp3", 0.3f);
        ShellExecuteA(nullptr, "open", "https://ja.wikipedia.org/wiki/%E3%82%B4%E3%83%AB%E3%83%95",
                      nullptr, nullptr, SW_SHOW);
      }
    }
  });
  } // else (MainMenu)

  if (newGame) {
    StopIntroAudio(ctx);
    title_scene_detail::ResetStandardStartData(ctx);
    auto loadingScene = std::make_unique<LoadingScene>([]() { return std::make_unique<WikiGolfScene>(false); });
    ctx.sceneManager->ChangeScene(std::move(loadingScene));
  }
  if (startTutorial) {
    StopIntroAudio(ctx);
    ctx.sceneManager->ChangeScene(std::make_unique<WikiGolfScene>(true));
  }
  if (exitGame) {
    StopIntroAudio(ctx);
    ctx.shouldClose = true;
  }

  if (m_startConnectionChecking) {
    m_popupTimer = std::max(m_popupTimer, 0.8f);
  }

  // ポップアップの更新
  if (m_popupTimer > 0.0f) {
    m_popupTimer -= ctx.dt;
    float alpha = std::min(m_popupTimer * 2.0f, 1.0f); // 残り0.5秒でフェードアウト
    if (m_popupTimer > 1.5f) {
      alpha = (2.0f - m_popupTimer) * 2.0f; // 最初の0.5秒でフェードイン
    }
    alpha = std::clamp(alpha, 0.0f, 1.0f);

    auto *pbg = ctx.world.Get<components::UIText>(m_popupBgEntity);
    auto *ptxt = ctx.world.Get<components::UIText>(m_popupTextEntity);
    if (pbg && ptxt) {
      pbg->visible = true;
      ptxt->visible = true;
      pbg->style.bgColor.w = alpha * 0.95f;
      pbg->style.borderColor.w = alpha;
      ptxt->style.color.w = alpha;
    }
  } else {
    auto *pbg = ctx.world.Get<components::UIText>(m_popupBgEntity);
    auto *ptxt = ctx.world.Get<components::UIText>(m_popupTextEntity);
    if (pbg && ptxt) {
      pbg->visible = false;
      ptxt->visible = false;
    }
  }
}

} // namespace game::scenes
