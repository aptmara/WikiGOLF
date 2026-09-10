/**
 * @file ResultScene.cpp
 * @brief ResultScene の実装
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
#include "../systems/PlayFabClient.h"
#include "TitleScene.h"
#include "TitleSceneSupport.h"
#include "LoadingScene.h"
#include "WikiGolfScene.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <random>
#include <thread>

namespace game::scenes {

using namespace game::components;
using namespace DirectX;

ResultScene::ResultScene(const ResultData &data) : m_data(data) {}

ResultScene::~ResultScene() = default;

/**
 * @brief シーン開始時の初期化処理を行います。
*/
void ResultScene::OnEnter(core::GameContext &ctx) {
  LOG_INFO("ResultScene", "OnEnter (Luxury) - Target: {}, Score: {}",
           m_data.targetPage, m_data.shotCount);

  m_time = 0.0f;
  m_volleyTimer = 1.0f;
  m_scoreDisplayValue = 0.0f;
  m_isScoreCountFinished = false;

  m_uiElements.clear();
  m_rings.clear();
  m_shells.clear();
  m_sparks.clear();

  // マウスカーソルの設定を行います。
  ctx.input.SetMouseCursorVisible(true);
  ctx.input.SetMouseCursorLocked(false);

  // BGMとファンファーレの再生を行います。
  if (ctx.audio) {
    ctx.audio->PlayBGM(ctx, "bgm_title.mp3", 0.4f);
    ctx.audio->PlayOneShotFile(ctx, "ResultFanfare", "Assets/sounds/se_holeInOne.mp3");
  }

  // 3Dビジュアル環境を生成します。
  LOG_INFO("ResultScene", "Creating Visual Environment...");
  CreateVisualEnvironment(ctx);

  // 豪華なUIを生成します。
  LOG_INFO("ResultScene", "Creating Luxury UI...");
  CreateLuxuryUI(ctx);
  if (m_data.isDailyChallenge) {
    m_rankingUploadState = std::make_shared<RankingUploadState>();
    const auto uploadState = m_rankingUploadState;
    const int strokes = m_data.shotCount;
    const int clearTimeMs = m_data.clearTimeMs;
    std::thread([uploadState, strokes, clearTimeMs]() {
      game::systems::PlayFabClient client;
      const auto result = client.SubmitDailyResult(strokes, clearTimeMs);
      uploadState->success = result.success;
      uploadState->errorMessage = result.errorMessage;
      uploadState->completed.store(true, std::memory_order_release);
    }).detach();
  }
  LOG_INFO("ResultScene", "OnEnter complete.");
}

/**
 * @brief 毎フレームの更新処理を行います。
*/
void ResultScene::OnUpdate(core::GameContext &ctx) {
  m_time += ctx.dt;

  if (m_rankingUploadState &&
      m_rankingUploadState->completed.load(std::memory_order_acquire)) {
    if (auto *status =
            ctx.world.Get<components::UIText>(m_rankingStatusEntity)) {
      status->text = m_rankingUploadState->success
                         ? L"オンラインランキングへ記録を送信しました"
                         : L"ランキング送信失敗: " + core::ToWString(
                               m_rankingUploadState->errorMessage);
    }
    m_rankingUploadState.reset();
  }

  // カメラを地球儀の周囲で回転させます。
  // Update Camera shake
  if (m_cameraShake > 0.0f) {
      m_cameraShake -= ctx.dt * 2.0f;
      if (m_cameraShake < 0.0f) m_cameraShake = 0.0f;
  }

  // カメラが地球儀の周りを公転します。
  if (ctx.world.IsAlive(m_cameraEntity)) {
    auto *camTr = ctx.world.Get<Transform>(m_cameraEntity);
    if (camTr) {
      float orbitSpeed = 0.2f;
      float radius = 8.0f + std::sin(m_time * 0.5f) * 1.0f;
      float angle = m_time * orbitSpeed;
      float height = 4.0f + std::cos(m_time * 0.3f) * 0.5f;

      DirectX::XMFLOAT3 basePos = {
          std::sin(angle) * radius, height,
          std::cos(angle) * -radius
      };

      // Apply shake to camera position
      if (m_cameraShake > 0.0f) {
          float intensity = m_cameraShake * 0.5f;
          basePos.x += (static_cast<float>(rand() % 100) / 100.0f - 0.5f) * intensity;
          basePos.y += (static_cast<float>(rand() % 100) / 100.0f - 0.5f) * intensity;
          basePos.z += (static_cast<float>(rand() % 100) / 100.0f - 0.5f) * intensity;
      }

      camTr->position = basePos;

      // 注視点の設定とカメラの姿勢更新を行います。
      XMVECTOR eye = XMLoadFloat3(&camTr->position);
      XMVECTOR focus = XMVectorSet(0.0f, 2.5f, 0.0f, 0.0f);
      XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
      XMMATRIX view = XMMatrixLookAtLH(eye, focus, up);
      XMMATRIX invView = XMMatrixInverse(nullptr, view);
      XMStoreFloat4(&camTr->rotation, XMQuaternionRotationMatrix(invView));
    }
  }

  // 3Dビジュアルの更新処理を行います。
  UpdateVisuals(ctx);

  // UIのインタラクションとアニメーション処理を行います。
  auto mousePos = ctx.input.GetMousePosition();

  for (auto &elem : m_uiElements) {
    if (!ctx.world.IsAlive(elem.entity))
      continue;

    // UI要素の矩形範囲を設定します。
    float w = 300.0f;
    float h = 60.0f;
    float x = elem.baseX - w / 2.0f;
    float y = elem.baseY;

    // ボタンの場合はボタンサイズに合わせます。
    auto *btn = ctx.world.Get<UIButton>(elem.entity);
    if (btn) {
      w = btn->width;
      h = btn->height;
      x = btn->x;
      y = btn->y;
    }

    bool hover = (mousePos.x >= x && mousePos.x <= x + w && mousePos.y >= y &&
                  mousePos.y <= y + h);

    // ホバー時にスケールを変更しSEを再生します。
    if (hover && !elem.isHovered) {
      elem.targetScale = 1.15f;
      elem.isHovered = true;
      if (ctx.audio)
        ctx.audio->PlaySE(ctx, "se_shot_soft.mp3", 0.3f);
    } else if (!hover && elem.isHovered) {
      elem.targetScale = 1.0f;
      elem.isHovered = false;
    }

    elem.currentScale +=
        (elem.targetScale - elem.currentScale) * (15.0f * ctx.dt);

    // テキストサイズにスケールを適用します。
    auto *t = ctx.world.Get<UIText>(elem.entity);
    if (t) {
      float baseFontSize = 30.0f;
      if (btn) {
        baseFontSize = 28.0f;
      }
      if (elem.text == L"STAGE CLEAR") {
        baseFontSize = 90.0f;
      }
      t->style.fontSize = baseFontSize * elem.currentScale;

      // タイトル文字列のカラーパルス演出を行います。
      if (elem.text == L"STAGE CLEAR") {
        float pulse = std::sin(m_time * 3.0f);
        t->style.color = {1.0f, 0.9f + pulse * 0.1f, 0.6f + pulse * 0.2f,
                          1.0f};
        t->style.shadowOffsetX = 2.0f + pulse;
        t->style.shadowOffsetY = 2.0f + pulse;
      }
    }

    // ボタンのクリック操作を処理します。
    if (btn && hover && ctx.input.GetMouseButtonDown(0)) {
      // プレイし直すボタンの処理を行います。
      if (elem.text == L"Play Again (R)") {
        std::vector<ecs::Entity> toDestroy;
        ctx.world.Query<game::components::TerrainObject>().Each(
            [&](ecs::Entity e, game::components::TerrainObject &) {
              toDestroy.push_back(e);
            });

        for (auto e : toDestroy) {
          ctx.world.DestroyEntity(e);
        }

        if (m_data.isDailyChallenge) {
          title_scene_detail::ResetDailyChallengeStartData(ctx);
          auto loadingScene = std::make_unique<LoadingScene>(
              []() { return std::make_unique<WikiGolfScene>(false); });
          ctx.sceneManager->ChangeScene(std::move(loadingScene));
        } else {
          ctx.sceneManager->ChangeScene(std::make_unique<WikiGolfScene>());
        }
        return;
      }
      // タイトルへ戻るボタンの処理を行います。
      if (elem.text == L"Title Screen") {
        ctx.sceneManager->ChangeScene(std::make_unique<TitleScene>());
        return;
      }
    }
  }

  // ショートカットキー入力を処理します。
  if (ctx.input.GetKeyDown('R')) {
    if (m_data.isDailyChallenge) {
      title_scene_detail::ResetDailyChallengeStartData(ctx);
      auto loadingScene = std::make_unique<LoadingScene>(
          []() { return std::make_unique<WikiGolfScene>(false); });
      ctx.sceneManager->ChangeScene(std::move(loadingScene));
    } else {
      ctx.sceneManager->ChangeScene(std::make_unique<WikiGolfScene>());
    }
    return;
  }
  LOG_DEBUG("ResultScene", "OnUpdate: Finished successfully");
}

/**
 * @brief 3Dオブジェクトのビジュアル更新を行います。
*/

} // namespace game::scenes
