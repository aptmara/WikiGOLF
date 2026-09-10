/**
 * @file WikiGolfScene.cpp
 * @brief WikiGolfScene の実装
*/

#include "WikiGolfScene.h"
#include "WikiGolfSceneSupport.h"
#include "../../core/Profiler.h"
#include "../../audio/AudioSystem.h"
#include "../../core/GameContext.h"
#include "../../core/Input.h"
#include "../../core/Logger.h"
#include "../../core/SceneManager.h"
#include "../../core/StringUtils.h"
#include "../../ecs/World.h"
#include "../../graphics/GraphicsDevice.h"
#include "../../graphics/TextRenderer.h"
#include "../../graphics/WikiTextureGenerator.h"
#include "../components/Camera.h"
#include "../components/EnvironmentPresets.h"
#include "../components/MeshRenderer.h"
#include "../components/PhysicsComponents.h"
#include "../components/Skybox.h"
#include "../components/Transform.h"
#include "../components/UIButton.h"
#include "../components/UIImage.h"
#include "../components/UIText.h"
#include "../components/WikiComponents.h"
#include "../systems/PhysicsFriction.h"
#include "../systems/PhysicsSystem.h"
#include "../systems/SkyboxRenderSystem.h"
#include "../systems/WikiClient.h"
#include "../utils/JudgeFeedback.h"
#include "../utils/GameplayPhysicsConstants.h"
#include "../utils/TrajectorySimulation.h"
#include "../utils/PageHistoryUtils.h"
#include "../utils/UIConstants.h"
#include "../utils/ProceduralFlag.h"
#include "PauseScene.h"
#include "CupInUtils.h"
#include "ResultScene.h"
#include "TitleScene.h"
#include "LoadingScene.h"
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>

#undef min
#undef max

namespace game::scenes {

using namespace DirectX;
using namespace game::components;

WikiGolfScene::WikiGolfScene(bool isTutorial) : m_isTutorial(isTutorial) {}
WikiGolfScene::~WikiGolfScene() = default;

/**
 * @brief シーンの描画処理を行います。
*/
void WikiGolfScene::Render(core::GameContext &ctx) {
  PROFILE_SCOPE("WikiGolf.SceneOverlay");
  m_screenFade.Render(ctx);
}

void WikiGolfScene::RenderOffscreen(core::GameContext &ctx) {
  if (m_minimapController) {
    m_minimapController->RenderPendingMinimap(ctx);
  }
}

/**
 * @brief カップイン判定を行います。
 * @return 遷移が発生した場合はtrue
*/
bool WikiGolfScene::CheckCupIn(core::GameContext &ctx) {
  // チュートリアル中のカップイン音連続再生防止
  // gameCleared セット後はオーバーレイが Done になるまでこの関数をスキップする。
  if (m_isTutorial && m_tutorialCupInFired) return false;

  auto *rb = ctx.world.Get<RigidBody>(m_ballEntity);
  auto *t = ctx.world.Get<Transform>(m_ballEntity);
  if (!rb || !t)
    return false;

  float speedSq = rb->velocity.x * rb->velocity.x +
                  rb->velocity.y * rb->velocity.y +
                  rb->velocity.z * rb->velocity.z;

  // カップ判定
  auto *state = ctx.world.GetGlobal<GolfGameState>();
  if (!state)
    return false;

  for (auto holeEntity : state->holes) {
    auto *holeT = ctx.world.Get<Transform>(holeEntity);
    auto *hole = ctx.world.Get<GolfHole>(holeEntity);
    if (!holeT || !hole)
      continue;

    bool readyForCupIn = cupin::IsBallReadyForCupIn(
        t->position, holeT->position, hole->radius, speedSq);

    if (readyForCupIn) {
      if (m_isTutorial && m_tutorialOverlay &&
          !m_tutorialOverlay->CanAcceptCupIn(hole->linkTarget,
                                             hole->isTarget)) {
        LOG_INFO("WikiGolf", "[Tutorial] Locked cup ignored: {}",
                 hole->linkTarget);
        float terrainHeight = 0.0f;
        if (m_terrainSystem) {
          terrainHeight = m_terrainSystem->GetHeight(0.0f, -32.0f);
        }
        t->position = {
            0.0f,
            game::physics::ToVisualSurfaceHeight(terrainHeight) +
                game::physics::kBallRadius,
            -32.0f};
        rb->velocity = {0.0f, 0.0f, 0.0f};
        rb->angularVelocity = {0.0f, 0.0f, 0.0f};
        state->isBallGrounded = true;
        state->canShoot = true;
        if (auto* shot = ctx.world.GetGlobal<ShotState>()) shot->Reset();
        if (m_hud) m_hud->ResetShotUI(ctx);
        if (m_aimPinController) m_aimPinController->ClearPin(ctx);
        if (m_cameraController) {
          m_cameraController->ResetForTransition(scene_detail::kFieldScale);
        }
        return true;
      }

      // カップイン！
      LOG_INFO("WikiGolf", "Cup In! Target: {}", hole->linkTarget);

      // カップイン後にボールが跳ねてホールから出てしまい、判定が
      // ちらつく（出入りで再判定される）のを防ぐため速度を即座に停止する。
      rb->velocity = {0.0f, 0.0f, 0.0f};
      rb->angularVelocity = {0.0f, 0.0f, 0.0f};

      // 遷移前に地形判定UI/スイング判定UIを非表示にする（次のコースに持ち越さない）
      if (m_terrainImageEntity != UINT32_MAX) {
          auto* ui = ctx.world.Get<game::components::UIImage>(m_terrainImageEntity);
          if (ui) ui->visible = false;
      }
      m_terrainDisplayTimer = 0.0f;
      if (m_judgeImageEntity != UINT32_MAX) {
          auto* ui = ctx.world.Get<game::components::UIImage>(m_judgeImageEntity);
          if (ui) ui->visible = false;
      }
      m_judgeDisplayTimer = 0.0f;

      // カップイン時間進行 (1時間)
      m_timeOfDay.OnCupIn(1.0f);

      // ホールインワン演出の実行
      if (m_gameJuice) {
        // 巨大カメラシェイク
        m_gameJuice->TriggerCameraShake(0.8f, 0.5f);

        // 複数回のパーティクル爆発（タイミングをずらして）
        XMFLOAT3 effectPos = holeT->position;
        effectPos.y += 0.5f; // ホールの上から発火

        m_gameJuice->TriggerSlowMotion(0.8f, 0.25f);

        // 1回目：中央大爆発
        m_gameJuice->TriggerImpactEffect(ctx, effectPos, 1.0f);

        // 2回目以降は高さを変えて（連続爆発風）
        effectPos.y += 1.0f;
        m_gameJuice->TriggerImpactEffect(ctx, effectPos, 0.8f);

        effectPos.y += 1.0f;
        m_gameJuice->TriggerImpactEffect(ctx, effectPos, 0.6f);

        // FOV変化（ズームイン→アウト）
        m_gameJuice->SetTargetFov(40.0f); // 一瞬ズームイン
        m_gameJuice->TriggerConfetti(ctx, effectPos, 1.0f);
      }

      // 音楽と効果音
      if (ctx.audio) {
        ctx.audio->PlaySE(ctx, "se_cupin.mp3");

        // ホールインワン判定（1打目でターゲット到達）
        if (hole->isTarget && state->shotCount == 1) {
            ctx.audio->PlaySE(ctx, "se_holeInOne.mp3", 1.0f);
        } else if (hole->isTarget) {
            ctx.audio->PlaySE(ctx, "se_shot_hard.mp3", 0.8f); // ターゲット到達音
        }
      }

      // ターゲット到達時は超派手に追加演出
      if (hole->isTarget && m_gameJuice) {
        // 追加のカメラシェイク
        m_gameJuice->TriggerCameraShake(1.0f, 1.0f);

        // 四方向にパーティクル発射
        XMFLOAT3 pos = holeT->position;
        pos.y += 0.5f;
        for (int i = 0; i < 4; ++i) {
          XMFLOAT3 offset = {std::cos((float)i * XM_PIDIV2) * 2.0f, 0.0f,
                             std::sin((float)i * XM_PIDIV2) * 2.0f};
          XMFLOAT3 effectPosExtra = {pos.x + offset.x, pos.y + 1.5f,
                                     pos.z + offset.z};
          m_gameJuice->TriggerImpactEffect(ctx, effectPosExtra, 1.0f);
        }
        m_gameJuice->TriggerConfetti(ctx, pos, 1.2f);
      }

      // ターゲットホールに入った場合はリザルト画面へ遷移
      if (hole->isTarget) {
        LOG_INFO("WikiGolf", "GAME CLEAR! Reached target page!");

        // gameCleared をセット: TutorialStep::CupIn の完了判定が依存する
        state->gameCleared = true;

        if (ctx.audio) {
          ctx.audio->PlaySE(ctx, "se_goal.mp3", 1.0f);
        }

        // チュートリアル中はゴール検知をオーバーレイに委ねてリザルトへは進まない
        // フラグ設定により後続のカップイン判定をスキップ
        if (m_isTutorial) {
          m_tutorialCupInFired = true;
          return true;
        }

        // ResultSceneへ遷移
        ResultData data;
        data.targetPage = state->targetPage;
        data.shotCount = state->shotCount;
        data.clearTimeMs =
            static_cast<int>(state->elapsedTimeSeconds * 1000.0f + 0.5f);
        data.isDailyChallenge = state->isDailyChallenge;
        data.par = state->par;
        data.pathHistory = state->pathHistory;
        data.isNewRecord = false;

        if (ctx.sceneManager) {
          ctx.sceneManager->ChangeScene(std::make_unique<ResultScene>(data));
        }
        return true;
      }

      if (m_isTutorial) {
        m_tutorialOverlay->NotifyLinkCupIn(ctx);
        ClearTutorialFlagSamples(ctx);
        if (m_pageLoader) {
          std::vector<game::WikiLink> links = {
              {"フェアウェイ", "フェアウェイ"},
              {"ラフ", "ラフ"},
              {"バンカー", "バンカー"},
              {"グリーン", "グリーン"},
              {"ウォーターハザード", "ウォーターハザード"},
              {"ゴール", "ゴール"},
          };
          m_pageLoader->SetPreloadedData(
              std::move(links),
              "フェアウェイの記事へ移動しました。記事中のリンクが旗になり、"
              "カップインするたびに別の記事コースへ進みます。"
              "赤いゴールの旗を目指しましょう。",
              true);
        }
        TransitionToPage(ctx, hole->linkTarget);
        return true;
      }

      TransitionToPage(ctx, hole->linkTarget);
      return true; // 1フレームに1回だけ遷移
    }
  }
  return false;
}

} // namespace game::scenes
