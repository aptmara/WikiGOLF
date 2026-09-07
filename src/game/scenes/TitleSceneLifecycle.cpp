/**
 * @file TitleSceneLifecycle.cpp
 * @brief TitleSceneLifecycle の実装
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
 * @brief シーンの描画処理を行います。
*/
void TitleScene::Render(core::GameContext &ctx) {
  if (m_state != TitleState::IntroVideo || !m_videoPlayer || !ctx.textRenderer) {
    return;
  }

  auto *srv = m_videoPlayer->GetSRV();
  if (!srv) {
    return;
  }

  ctx.textRenderer->BeginDraw();
  D2D1_RECT_F rect = {0, 0, ctx.textRenderer->GetWidth(), ctx.textRenderer->GetHeight()};
  ctx.textRenderer->RenderImage(srv, rect);
  ctx.textRenderer->EndDraw();
}

/**
 * @brief シーンを抜ける際の後処理を行います。
*/
void TitleScene::OnExit(core::GameContext &ctx) {
  LOG_INFO("TitleScene", "OnExit");
  if (ctx.audio) {
    ctx.audio->StopBGM();
  }
  StopIntroAudio(ctx);
  if (m_videoPlayer) {
    m_videoPlayer->Stop();
    m_videoPlayer.reset();
  }
  DestroyAllEntities(ctx);
  m_menuEntries.clear();
}

void TitleScene::StopIntroAudio(core::GameContext &ctx) {
  if (ctx.audio) {
    ctx.audio->StopOneShot(kIntroAudioLabel);
  }
}

} // namespace game::scenes

