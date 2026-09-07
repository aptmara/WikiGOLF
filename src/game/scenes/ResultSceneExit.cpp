/**
 * @file ResultSceneExit.cpp
 * @brief ResultSceneの責務別実装です。
*/

#define NOMINMAX
#include "ResultScene.h"
#include "../../audio/AudioSystem.h"
#include "../../core/GameContext.h"
#include "../../core/Input.h"
#include "../../core/Logger.h"
#include "../../core/SceneManager.h"
#include "../../core/StringUtils.h"
#include "../../ecs/World.h"
#include "../../graphics/GraphicsDevice.h"
#include "../../graphics/TextRenderer.h"
#include "../components/Camera.h"
#include "../components/MeshRenderer.h"
#include "../components/Skybox.h"
#include "../components/Transform.h"
#include "../components/UIButton.h"
#include "../components/UIImage.h"
#include "../components/UIText.h"
#include "../components/WikiComponents.h"
#include "../systems/SkyboxRenderSystem.h"
#include "TitleScene.h"
#include "WikiGolfScene.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <random>

namespace game::scenes {

using namespace game::components;
using namespace DirectX;

void ResultScene::OnExit(core::GameContext &ctx) {
  if (ctx.audio) {
    ctx.audio->StopBGM();
    ctx.audio->StopOneShot("ResultFanfare");
  }

  // エンティティリストの一括破棄用ラムダ関数です。
  auto destroyVec = [&](auto &vec) {
    for (const auto &item : vec) {
      if (ctx.world.IsAlive(item.entity))
        ctx.world.DestroyEntity(item.entity);
    }
    vec.clear();
  };

  // 主要な3Dオブジェクトエンティティを破棄します。
  if (ctx.world.IsAlive(m_globeEntity))
    ctx.world.DestroyEntity(m_globeEntity);
  if (ctx.world.IsAlive(m_floorEntity))
    ctx.world.DestroyEntity(m_floorEntity);
  if (ctx.world.IsAlive(m_cameraEntity))
    ctx.world.DestroyEntity(m_cameraEntity);

  // UI表示要素およびリング、パーティクルエンティティを破棄します。
  destroyVec(m_uiElements);

  for (const auto &ring : m_rings) {
    if (ctx.world.IsAlive(ring.entity))
      ctx.world.DestroyEntity(ring.entity);
  }
  m_rings.clear();

  for (const auto &sp : m_sparks) {
    if (ctx.world.IsAlive(sp.entity))
      ctx.world.DestroyEntity(sp.entity);
  }
  m_sparks.clear();
  m_shells.clear();

  LOG_INFO("ResultScene", "OnExit: Cleanup complete");
}

void ResultScene::LaunchVolley() {
    for (int i = 0; i < m_shellsPerVolley; ++i) {
        HanabiShell shell;
        shell.pos = {
            (static_cast<float>(rand() % 200) / 10.0f) - 10.0f,
            0.0f,
            (static_cast<float>(rand() % 200) / 10.0f) - 10.0f
        };

        // Launch up
        shell.vel = {
            (static_cast<float>(rand() % 40) / 10.0f) - 2.0f,
            5.0f + (static_cast<float>(rand() % 40) / 10.0f),
            (static_cast<float>(rand() % 40) / 10.0f) - 2.0f
        };
        m_shells.push_back(shell);
    }
}

/**
 * @brief 描画処理を行います（実描画はECSシステムが担当）。
*/
void ResultScene::Render(core::GameContext &ctx) {
  LOG_DEBUG("ResultScene", "Render: START");
  LOG_DEBUG("ResultScene", "Render: FINISHED");
}

} // namespace game::scenes

