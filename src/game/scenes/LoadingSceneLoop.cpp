/**
 * @file LoadingSceneLoop.cpp
 * @brief LoadingSceneの責務別実装です。
*/

#include "LoadingScene.h"
#include "../../core/GameContext.h"
#include "../../core/Input.h"
#include "../../core/Logger.h"
#include "../../core/Profiler.h"
#include "../../core/SceneManager.h"
#include "../../graphics/TextRenderer.h"
#include "../../graphics/WikiTextureGenerator.h"
#include "../components/Camera.h"
#include "../components/MeshRenderer.h"
#include "../components/Transform.h"
#include "../components/UIText.h"
#include "../systems/WikiClient.h"
#include "../systems/WikiShortestPath.h"
#include "LoadingSceneUtils.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <d2d1_1.h>
#include <filesystem>
#include <random>
#include <thread>

namespace game::scenes {

void LoadingScene::OnUpdate(core::GameContext &ctx) {
  PROFILE_SCOPE("LoadingScene.OnUpdate");
  float dt = ctx.dt;
  m_sceneTime += dt;

  // ゲームプレイ用アセットを1フレームに1件だけ先行ロード
  if (m_preloadIndex < m_preloadTasks.size()) {
    PROFILE_SCOPE("LoadingScene.PreloadTask");
    m_preloadTasks[m_preloadIndex](ctx);
    ++m_preloadIndex;
    if (m_preloadIndex >= m_preloadTasks.size() && !m_preloadComplete) {
      m_preloadComplete = true;
      LOG_INFO("LoadingScene", "Gameplay asset preload complete: {} tasks",
               m_preloadTasks.size());
    }
  } else if (!m_preloadComplete) {
    m_preloadComplete = true;
  }

  // ボールをスポーン
  if (m_spawnedCount < TOTAL_BALLS) {
    m_spawnTimer += dt;
    if (m_spawnTimer >= SPAWN_INTERVAL) {
      SpawnBall(ctx);
      m_spawnTimer = 0.0f;
    }
  } else if (!m_spawnFinishedLogged) {
    LOG_INFO("LoadingScene", "All balls spawned: {}", TOTAL_BALLS);
    m_spawnFinishedLogged = true;
  }

  // 物理更新
  UpdatePhysics(ctx, dt);

  // 停滞監視（静止数が増えない状態が続いたら詳細ログ）
  if (m_settledCount != m_lastSettledCount) {
    m_stuckTimer = 0.0f;
    m_lastSettledCount = m_settledCount;
  } else if (m_spawnFinishedLogged && m_settledCount < TOTAL_BALLS) {
    m_stuckTimer += dt;
    if (m_stuckTimer > 3.0f) {
      if (m_hasMovingSample) {
        LOG_INFO("LoadingScene",
                 "phase=WAIT_SETTLE stuck settled={}/{} moving={} "
                 "maxSpeed={:.2f} avgSpeed={:.2f} "
                 "samplePos=({:.1f},{:.1f},{:.1f}) floorY={:.1f}",
                 m_settledCount, TOTAL_BALLS, m_movingCount, m_maxSpeed,
                 m_avgSpeed, m_lastMovingPos.x, m_lastMovingPos.y,
                 m_lastMovingPos.z, FLOOR_Y + BALL_RADIUS * BALL_MODEL_SCALE);
      } else {
        LOG_INFO("LoadingScene",
                 "phase=WAIT_SETTLE stuck settled={}/{} moving={} "
                 "maxSpeed={:.2f} avgSpeed={:.2f}",
                 m_settledCount, TOTAL_BALLS, m_movingCount, m_maxSpeed,
                 m_avgSpeed);
      }
      m_stuckTimer = 0.0f;
    }
  }

  // カメラ演出とUI更新
  UpdateCamera(ctx, dt);
  UpdateUI(ctx);

  // ロード完了時または一定時間経過後に次のシーンへの遷移フェードを開始
  bool triggerFade = false;

  // ロード完了チェック
  if (m_isLoading && m_loadTask.valid()) {
    auto status = m_loadTask.wait_for(std::chrono::milliseconds(0));
    if (status == std::future_status::ready) {
      try {
        m_loadedData = m_loadTask.get();
        m_loadCompleted = static_cast<bool>(m_loadedData);
        if (m_loadProgress) {
          m_loadProgress->store(1.0f, std::memory_order_relaxed);
        }
        LOG_INFO("LoadingScene", "Asset Load Completed!");
      } catch (const std::exception &e) {
        m_loadedData = std::make_unique<game::components::WikiGlobalData>();
        m_loadedData->startPage = "ゴルフ";
        m_loadedData->targetPage = "日本";
        m_loadedData->targetPageId = -1;
        m_loadedData->isUserOverride = false;
        m_loadCompleted = true;
        LOG_ERROR("LoadingScene", "Load task failed: {}", e.what());
      }
      m_isLoading = false;
    }
  }

  if (m_loadCompleted && m_preloadComplete) {
    triggerFade = true; // Wikiデータとゲームプレイアセットの両方が揃ったら終了
  }

  // タイムアウト安全装置
  if (m_spawnedCount >= TOTAL_BALLS) {
    m_forceFinishTimer += dt;
    if (m_forceFinishTimer > 4.0f && !m_loadCompleted) {
      if (!m_fadeStarted && !m_fadeLogged) {
        LOG_INFO("LoadingScene",
                 "Visual load finished; waiting for async data before fade");
        m_fadeLogged = true;
      }
    }
  }

  if (triggerFade) {
    UpdateFade(ctx, dt);
  }

  // ESCで強制スキップ
  if (ctx.input.GetKeyDown(VK_ESCAPE)) {
    LOG_INFO("LoadingScene", "Skip requested via ESC");
    if (ctx.sceneManager && m_nextSceneFactory) {
      ctx.sceneManager->ChangeScene(m_nextSceneFactory());
    }
  }
}

void LoadingScene::Render(core::GameContext &ctx) {
  if (!ctx.textRenderer) {
    return;
  }

  const float overlayAlpha =
      loading_detail::FadeOverlayAlpha(m_fadeAlpha, m_fadeStarted);
  if (overlayAlpha <= 0.0f) {
    return;
  }

  ctx.textRenderer->BeginDraw();
  // レターボックス（縦横比維持のためのUI縮小表示）で生じる余白も含めて、
  // 画面全体を確実に覆う（仮想解像度基準だと余白部分が暗転しない）。
  ctx.textRenderer->FillFullScreenRect({0.0f, 0.0f, 0.0f, overlayAlpha});
  ctx.textRenderer->EndDraw();
}

void LoadingScene::OnExit(core::GameContext &ctx) {
  LOG_INFO("LoadingScene", "OnExit");

  m_balls.clear();
  m_wallEntities.clear();

  // 基底クラスのクリーンアップ（全エンティティ破棄）
  Scene::OnExit(ctx);
}

} // namespace game::scenes

