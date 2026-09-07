/**
 * @file LoadingScenePresentation.cpp
 * @brief LoadingSceneの責務別実装です。
*/

#include "LoadingScene.h"
#include "../../core/GameContext.h"
#include "../../core/Input.h"
#include "../../core/Logger.h"
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

void LoadingScene::UpdateFade(core::GameContext &ctx, float dt) {
  if (!m_fadeStarted) {
    m_fadeStarted = true;
    LOG_INFO("LoadingScene", "Fade started");
    return;
  }

  m_fadeAlpha += FADE_SPEED * dt;
  if (m_fadeAlpha >= 1.0f) {
    m_fadeAlpha = 1.0f;

    if (!m_loadCompleted || !m_loadedData) {
      static auto s_lastWaitLogAt = std::chrono::steady_clock::time_point::min();
      const auto now = std::chrono::steady_clock::now();
      if (s_lastWaitLogAt == std::chrono::steady_clock::time_point::min() ||
          now - s_lastWaitLogAt >= std::chrono::seconds(1)) {
        LOG_INFO("LoadingScene",
                 "Fade complete, waiting for async load without blocking render thread");
        s_lastWaitLogAt = now;
      }
      return;
    }

    LOG_INFO("LoadingScene", "Async load completed. Start: {}, Target: {}",
             m_loadedData->startPage, m_loadedData->targetPage);
    ctx.world.SetGlobal(std::move(*m_loadedData));
    m_loadedData.reset();

    // 次のシーンへ遷移
    if (ctx.sceneManager && m_nextSceneFactory) {
      LOG_INFO("LoadingScene", "Explosion finished, switching scene");
      ctx.sceneManager->ChangeScene(m_nextSceneFactory());
    }
  }
}

void LoadingScene::UpdateCamera(core::GameContext &ctx, float dt) {
  auto *tr = ctx.world.Get<components::Transform>(m_cameraEntity);
  if (!tr)
    return;

  m_cameraTime += dt;

  const float sway = std::sin(m_cameraTime * 0.55f) * 6.0f;
  const float bob = std::sin(m_cameraTime * 1.1f) * 1.6f;
  const float dolly = std::cos(m_cameraTime * 0.35f) * 2.5f;

  tr->position.x = sway * 0.6f;
  tr->position.y = 6.0f + bob;
  tr->position.z = -68.0f + dolly;

  auto camRot = DirectX::XMQuaternionRotationRollPitchYaw(-0.12f + bob * 0.01f,
                                                          sway * 0.003f, 0.0f);
  DirectX::XMStoreFloat4(&tr->rotation, camRot);
}

void LoadingScene::UpdateUI(core::GameContext &ctx) {
  const std::array<std::wstring, 8> tips = {
      L"サーバー室で羊にゴルフを教えています...",
      L"「要出典」タグをバンカーに埋設中...",
      L"ナレッジグラフの芝目を精密スキャン中...",
      L"ハイパーリンクの張力をティーアップ中...",
      L"パズルピースの球体を高光沢研磨中...",
      L"Wiki記法をゴルフ場の等高線に変換中...",
      L"カテゴリツリーの枝をアイアンで剪定中...",
      L"リダイレクトの嵐をフェアウェイに誘導中..."};

  const float spawnRatio =
      static_cast<float>(m_spawnedCount) / static_cast<float>(TOTAL_BALLS);
  float settledRatio = 0.0f;
  if (m_spawnedCount > 0) {
    settledRatio = static_cast<float>(m_settledCount) /
                   static_cast<float>(TOTAL_BALLS);
  }

  const float blended = loading_detail::BlendProgress(spawnRatio, settledRatio);
  const float visualProgress = loading_detail::EaseOutCubic(blended);
  float asyncProgress = 0.0f;
  if (m_loadProgress) {
    asyncProgress = m_loadProgress->load(std::memory_order_relaxed);
  }
  const float combined =
      loading_detail::CombineLoadingProgress(asyncProgress, visualProgress);

  // UI表示は単調増加させ、ロード完了なら必ず100%
  m_uiProgress = std::clamp(std::max(m_uiProgress, combined), 0.0f, 1.0f);
  const int percent = static_cast<int>(std::round(m_uiProgress * 100.0f));

  const int dotCount =
      static_cast<int>(std::fmod(m_sceneTime * 1.6f, 3.0f)) + 1;
  const std::wstring dots(static_cast<size_t>(dotCount), L'.');
  float fade = 1.0f;
  if (m_fadeStarted) {
    fade = std::clamp(1.0f - m_fadeAlpha, 0.0f, 1.0f);
  }
  const char *phase = "READY";
  if (m_fadeStarted) {
    phase = "FADE";
  } else if (m_spawnedCount < TOTAL_BALLS) {
    phase = "SPAWNING";
  } else if (m_settledCount < TOTAL_BALLS) {
    phase = "WAIT_SETTLE";
  }

  if (auto *title = ctx.world.Get<components::UIText>(m_textEntity)) {
    auto style = m_primaryStyle;
    style.color.w *= fade;
    style.outlineColor.w *= fade;
    title->style = style;
  }

  if (auto *progress =
          ctx.world.Get<components::UIText>(m_progressTextEntity)) {
    auto style = m_progressStyle;
    style.color.w *= fade;
    style.shadowColor.w *= fade;
    progress->style = style;
    progress->text = L"LOADING " + std::to_wstring(percent) + L"%" + dots;
  }

  m_tipTimer += ctx.dt;
  if (m_tipTimer > 2.8f) {
    m_tipTimer = 0.0f;
    m_tipIndex = (m_tipIndex + 1) % tips.size();
  }

  if (auto *caption = ctx.world.Get<components::UIText>(m_captionTextEntity)) {
    auto style = m_captionStyle;
    style.color.w *= fade;
    style.shadowColor.w *= fade;
    caption->style = style;
    caption->text = tips[m_tipIndex];
  }

  m_logTimer += ctx.dt;
  if (m_logTimer > 1.5f) {
    LOG_INFO("LoadingScene",
             "phase={} progress={}%, spawned={}/{} settled={} moving={} "
             "maxSpeed={:.2f} "
             "avgSpeed={:.2f} fade={}",
             phase, percent, m_spawnedCount, TOTAL_BALLS, m_settledCount,
             m_movingCount, m_maxSpeed, m_avgSpeed, m_fadeAlpha);
    m_logTimer = 0.0f;
  }
}

void LoadingScene::ApplyFadeToScene(core::GameContext &ctx) {
  if (!m_fadeStarted)
    return;

  // フェード中のアルファ変更は行わず、純粋に時間経過で遷移する
}

} // namespace game::scenes
