/**
 * @file TitleScene.cpp
 * @brief TitleScene の実装
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
 * @brief シーンに侵入した際の初期化処理を行います。
*/
void TitleScene::OnEnter(core::GameContext &ctx) {
  LOG_INFO("TitleScene", "OnEnter (WIKI GOLF High-End UI Style)");

  m_time = 0.0f;
  m_menuEntries.clear();

  // マウスカーソル表示
  ctx.input.SetMouseCursorVisible(true);
  ctx.input.SetMouseCursorLocked(false);


  m_videoPlayer = std::make_unique<graphics::VideoPlayer>();
  if (!m_videoPlayer->Initialize(ctx.graphics.GetDevice(), "Assets/videos/aptma_intro.mp4")) {
      LOG_ERROR("TitleScene", "Failed to load intro video");
      m_videoPlayer.reset();
      m_startupLoadTask = std::async(std::launch::async, [](){});
  } else if (ctx.audio) {
      ctx.audio->PlayOneShotFile(ctx, kIntroAudioLabel, "Assets/videos/aptma_intro.mp4", title_scene_detail::kIntroAudioVolume);
  }

  m_startupLoadTask = std::async(std::launch::async, [&ctx]() {
      LOG_INFO("TitleScene", "Async load task started on thread!");
      try {
        HRESULT hrCom = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        LOG_INFO("TitleScene", "CoInitializeEx returned: {:08X}", static_cast<uint32_t>(hrCom));

        LOG_INFO("TitleScene", "Step 1: LoadAudio");
        if (ctx.audio) {
            LOG_INFO("TitleScene", "Calling ctx.resource.LoadAudio bgm_title.mp3...");
            ctx.resource.LoadAudio("Assets/sounds/bgm_title.mp3");
            LOG_INFO("TitleScene", "LoadAudio bgm_title.mp3 finished successfully!");
        }

        LOG_INFO("TitleScene", "Step 2: LoadShader Basic");
        ctx.resource.LoadShader("Basic", L"Assets/shaders/BasicVS.hlsl", L"Assets/shaders/BasicPS.hlsl");
        LOG_INFO("TitleScene", "Step 3: LoadShader Skybox");
        ctx.resource.LoadShader("Skybox", L"Assets/shaders/SkyboxVS.hlsl", L"Assets/shaders/SkyboxPS.hlsl");

        LOG_INFO("TitleScene", "Step 4: LoadMesh sphere");
        ctx.resource.LoadMesh("builtin/sphere");

        LOG_INFO("TitleScene", "Step 5: LoadMesh globe");
        ctx.resource.LoadMesh("Assets/models/Wikipedia_puzzle_globe_3D_render.stl");

        LOG_INFO("TitleScene", "Step 6: LoadTextureSRV");
        ctx.resource.LoadTextureSRV("Assets/textures/GRASS_BASE.png");

        LOG_INFO("TitleScene", "Step 7: GenerateTerrain");
        game::systems::TerrainConfig tconf;
        tconf.worldWidth = 150.0f; tconf.worldDepth = 150.0f;
        tconf.resolutionX = 64; tconf.resolutionZ = 64;
        tconf.baseHeight = 0.0f; tconf.heightScale = 2.5f; tconf.biome = 0;
        auto tdata = game::systems::TerrainGenerator::GenerateTerrain("TitleSeed", {}, tconf);

        LOG_INFO("TitleScene", "Step 8: CreateDynamicMesh");
        ctx.resource.CreateDynamicMesh("TitleTerrain", tdata.vertices, tdata.indices);

        LOG_INFO("TitleScene", "Step 9: LoadCubemapFromSingleFile");
        graphics::SkyboxTextureGenerator gen;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> cubemapSRV;
        gen.LoadCubemapFromSingleFile(ctx.graphics.GetDevice(), L"Assets/textures/skybox_default_px_1767953230432.png", cubemapSRV);

        LOG_INFO("TitleScene", "Async load finished");
        CoUninitialize();
      } catch (const std::exception& e) {
        LOG_ERROR("TitleScene", "Exception caught in async load task: {}", e.what());
      } catch (...) {
        LOG_ERROR("TitleScene", "Unknown exception caught in async load task!");
      }
  });
}

} // namespace game::scenes
