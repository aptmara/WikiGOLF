/**
 * @file WikiGolfSceneUpdate.cpp
 * @brief WikiGolfシーンの毎フレーム更新を実装します。
*/

#include "WikiGolfScene.h"
#include "WikiGolfSceneSupport.h"
#include "../../audio/AudioSystem.h"
#include "../../core/GameContext.h"
#include "../../core/Input.h"
#include "../../core/Logger.h"
#include "../../core/Profiler.h"
#include "../../core/SceneManager.h"
#include "../../ecs/World.h"
#include "../components/Camera.h"
#include "../components/MeshRenderer.h"
#include "../components/PhysicsComponents.h"
#include "../components/Transform.h"
#include "../components/UIButton.h"
#include "../components/UIImage.h"
#include "../components/UIText.h"
#include "../components/WikiComponents.h"
#include "../controllers/AimPinController.h"
#include "../controllers/ClubController.h"
#include "../controllers/MinimapController.h"
#include "../controllers/TutorialOverlayController.h"
#include "../systems/PhysicsSystem.h"
#include "../utils/GameplayPhysicsConstants.h"
#include "../utils/JudgeFeedback.h"
#include "../utils/ProceduralFlag.h"
#include "../utils/TrajectorySimulation.h"
#include "../utils/UIConstants.h"
#include "LoadingScene.h"
#include "TitleScene.h"
#include "TitleSceneSupport.h"
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <thread>

#undef min
#undef max

namespace game::scenes {

using namespace DirectX;
using namespace game::components;

/**
 * @brief シーンの毎フレーム更新処理を行います。
*/
void WikiGolfScene::OnUpdate(core::GameContext &ctx) {
  PROFILE_SCOPE("WikiGolf.Update");
  const float dt = ctx.dt;
  m_screenFade.Update(dt);

  auto *state = ctx.world.GetGlobal<game::components::GolfGameState>();
  auto *shot = ctx.world.GetGlobal<game::components::ShotState>();
  if (!state || !shot) return;

  // デイリーの時間は打てる状態(Playing)の間だけ数える。記事の読み込みや
  // 遷移・喜びの演出中は止め、回線や演出の長さで差がつかないようにする。
  if (state->isDailyChallenge && !state->gameCleared &&
      m_phase == ScenePhase::Playing) {
    state->elapsedTimeSeconds += dt;
  }

  if (m_pageLoader) {
      PROFILE_SCOPE("WikiGolf.PageLoaderAsync");
      m_pageLoader->UpdateAsyncPathEvaluation(ctx);

      if (m_phase == ScenePhase::Playing) {
          if (auto* ballT = ctx.world.Get<game::components::Transform>(m_ballEntity)) {
              DirectX::XMFLOAT3 golferCenter{};
              const DirectX::XMFLOAT3* golferPosition = nullptr;
              if (m_clubController) {
                  golferCenter = m_clubController->GetGolferPosition();
                  golferCenter.y += m_clubController->GetGolferHeight() * 0.5f;
                  golferPosition = &golferCenter;
              }
              m_pageLoader->UpdateNearbyHoleSignboards(
                  ctx, ballT->position, m_cameraEntity, golferPosition);
          }
      }
  }

  if (m_phase == ScenePhase::Transitioning && m_transitionController) {
      PROFILE_SCOPE("WikiGolf.Transition");
      bool finished = m_transitionController->Update(ctx);
      if (!finished && m_transitionController->IsCourseIntroductionActive() &&
          m_clubController) {
          const DirectX::XMFLOAT3 introductionShotDirection = {0.0f, 0.0f, 1.0f};
          m_clubController->UpdateAnimation(
              ctx, dt, m_ballEntity, introductionShotDirection);
      }
      if (finished) {
          m_phase = ScenePhase::Playing;
          m_prevTutorialInputLocked = false;
          // 次のステージ開始時は前のホールで使っていたクラブ（パター等）を
          // 持ち越さず、ドライバーに戻す
          if (m_clubController) m_clubController->ResetToFirstClub(ctx);
          // ロード完了: HUD/ミニマップを再表示する
          if (m_hud) m_hud->SetVisible(ctx, true);
          if (m_minimapController) m_minimapController->SetVisible(ctx, true);
          if (m_tutorialOverlay) m_tutorialOverlay->SetVisible(ctx, true);
          if (m_cameraController) m_cameraController->Update(ctx);
          // チュートリアルオーバーレイはロード演出完了後に初期化する
          // （ロード中にUIが重なって表示されるのを防ぐ）
          if (m_isTutorial && !m_tutorialOverlay) {
              m_tutorialOverlay = std::make_unique<game::controllers::TutorialOverlayController>();
              m_tutorialOverlay->Initialize(ctx);
              const auto tutorialPresentation =
                  m_tutorialOverlay->GetPresentationPolicy();
              if (m_hud) {
                  m_hud->SetTutorialPresentation(
                      ctx, true, tutorialPresentation.courseInfo,
                      tutorialPresentation.club,
                      tutorialPresentation.minimap);
              }
              if (m_minimapController) {
                  m_minimapController->SetTutorialHelpMode(ctx, true);
                  m_minimapController->SetVisible(
                      ctx, tutorialPresentation.minimap);
              }
              CreateTutorialFlagSamples(ctx);

              std::vector<game::controllers::TutorialOverlayController::EventCameraTarget> targets = {
                  { {0.0f, 15.0f, -25.0f}, {0.0f, 0.0f, -10.0f}, L"フェアウェイ", L"短く刈った芝です。まっすぐ、よく転がります。" },
                  { {-15.0f, 15.0f, -15.0f}, {-15.0f, 0.0f, 0.0f}, L"ラフ", L"深い草です。飛ぶ距離が落ち、あまり転がりません。" },
                  { {24.0f, 15.0f, -18.0f}, {24.0f, 0.0f, -3.0f}, L"バンカー", L"砂地です。ほとんど転がらず、出すには強く打つ必要があります。" },
                  { {0.0f, 15.0f, 37.0f}, {0.0f, 0.0f, 52.0f}, L"グリーン", L"カップのまわりの、なめらかな芝です。とてもよく転がるので、弱めに打ちます。" },
                  { {-24.0f, 15.0f, 5.0f}, {-24.0f, 0.0f, 20.0f}, L"池・溶岩・コースの外", L"入ると1打足されて、打つ前の場所からやり直しです。" }
              };
              std::vector<game::controllers::TutorialOverlayController::EventCameraTarget> flagTargets = {
                  { {-37.5f, 10.0f, 18.0f}, {-37.5f, 1.4f, 30.0f}, L"赤い旗", L"ゴールの記事へのリンクです。ここに入れるとクリアです。" },
                  { {-15.0f, 11.0f, 16.0f}, {-15.0f, 1.4f, 30.0f}, L"黄色・オレンジの旗", L"黄色はあと1記事、オレンジはあと2記事でゴールに着く近道です。" },
                  { {15.0f, 11.0f, 16.0f}, {15.0f, 1.4f, 30.0f}, L"白・灰色・青の旗", L"白と灰色は遠回りのリンクです。青は、近さをまだ調べている途中です。" }
              };
              m_tutorialOverlay->SetEventCameraTargets(m_cameraEntity,
                                                       std::move(targets),
                                                       std::move(flagTargets));
          }
      }
      return;
  }

  // カップイン後の喜び演出中は操作・物理を止め、演出だけを進める
  if (m_phase == ScenePhase::Celebrating) {
      PROFILE_SCOPE("WikiGolf.CupInCelebration");
      UpdateCupInCelebration(ctx);
      return;
  }

  auto mousePos = ctx.input.GetMousePosition();
  int mouseX = mousePos.x;
  int mouseY = mousePos.y;

  bool tutorialInputLocked = false;
  bool tutorialMinimapVisible = false;
  game::controllers::TutorialInputPolicy tutorialPolicy;
  if (m_isTutorial && m_tutorialOverlay) {
    PROFILE_SCOPE("WikiGolf.TutorialOverlay");
    m_tutorialOverlay->Update(ctx, m_cameraController.get(), m_clubController.get(),
                              m_shotController.get(), m_minimapController.get(),
                              m_skyboxEntity);
    if (m_tutorialOverlay->IsDone()) {
      if (!m_tutorialConnectionChecking) {
        std::ofstream ofs("save_tutorial_done.flag");
        ofs << "done";

        LOG_INFO("WikiGolf", "Tutorial completed. Testing Wikipedia connection...");
        m_tutorialConnectionChecking = true;
        m_tutorialConnectionElapsed = 0.0f;
        m_tutorialConnectionStatusEntity = CreateEntity(ctx.world);
        auto &status = ctx.world.Add<components::UIText>(
            m_tutorialConnectionStatusEntity);
        status.text = L"Wikipediaへの接続を確認中...";
        status.x = 440.0f;
        status.y = 330.0f;
        status.width = 400.0f;
        status.height = 60.0f;
        status.style.fontSize = 22.0f;
        status.style.align = graphics::TextAlign::Center;
        status.style.color = {1.0f, 1.0f, 1.0f, 1.0f};
        status.style.bgColor = {0.15f, 0.2f, 0.3f, 0.95f};
        status.style.cornerRadius = 16.0f;
        status.layer = 2000;
        status.visible = false;
        m_tutorialConnectionState =
            std::make_shared<TutorialConnectionState>();
        auto connectionState = m_tutorialConnectionState;
        std::thread([connectionState]() {
          game::systems::WikiClient client;
          connectionState->title = client.FetchRandomPageTitle();
          connectionState->completed.store(true, std::memory_order_release);
        }).detach();
      }

      m_tutorialConnectionElapsed += ctx.dt;
      // すぐ接続できたときは表示しない。
      if (m_tutorialConnectionElapsed >= 1.0f) {
        if (auto *status = ctx.world.Get<components::UIText>(
                m_tutorialConnectionStatusEntity)) {
          status->visible = true;
        }
      }
      const bool connectionCompleted =
          m_tutorialConnectionState &&
          m_tutorialConnectionState->completed.load(std::memory_order_acquire);
      const bool connectionTimedOut =
          m_tutorialConnectionElapsed >=
          title_scene_detail::kStartConnectionTimeoutSeconds;

      if (!connectionCompleted && !connectionTimedOut) return;

      std::string connectedTitle;
      if (connectionCompleted) {
        connectedTitle = m_tutorialConnectionState->title;
      }
      m_tutorialConnectionState.reset();
      m_tutorialConnectionChecking = false;
      m_tutorialConnectionElapsed = 0.0f;

      if (connectionCompleted && connectedTitle != "Error" &&
          !connectedTitle.empty()) {
        LOG_INFO("WikiGolf",
                 "Wikipedia connection test passed after tutorial. Title: {}",
                 connectedTitle);
        title_scene_detail::ResetStandardStartData(ctx);
        auto loadingScene = std::make_unique<LoadingScene>(
            []() { return std::make_unique<WikiGolfScene>(false); });
        ctx.sceneManager->ChangeScene(std::move(loadingScene));
      } else {
        if (connectionTimedOut) {
          LOG_WARN("WikiGolf",
                   "Wikipedia connection test timed out after tutorial.");
        } else {
          LOG_WARN("WikiGolf",
                   "Wikipedia connection test failed after tutorial.");
        }
        auto loadingScene = std::make_unique<LoadingScene>(
            []() {
              return std::make_unique<TitleScene>(
                  title_scene_detail::kConnectionFailedMessage);
            });
        ctx.sceneManager->ChangeScene(std::move(loadingScene));
      }
      return;
    }

    const auto guideTowardsHole = [&](const std::string& linkTarget,
                                      bool placeAimPin) {
      auto* ball = ctx.world.Get<game::components::Transform>(m_ballEntity);
      if (!ball) return false;
      for (const auto holeEntity : state->holes) {
        const auto* hole = ctx.world.Get<game::components::GolfHole>(holeEntity);
        const auto* holeTransform =
            ctx.world.Get<game::components::Transform>(holeEntity);
        if (!hole || !holeTransform || hole->linkTarget != linkTarget) continue;

        if (m_cameraController) {
          m_cameraController->AimYawTowards(ball->position,
                                            holeTransform->position);
        }
        if (placeAimPin && m_aimPinController) {
          const float distance = m_aimPinController->PlacePin(
              ctx, m_ballEntity, holeTransform->position);
          if (distance >= 0.0f && m_clubController) {
            RefreshAimPinSolution(ctx, true);
            if (m_cameraController) {
              m_cameraController->BeginTargetFraming(
                  ctx, holeTransform->position,
                  m_clubController->GetRecommendedCameraDistance(
                      scene_detail::kFieldScale));
            }
          }
        }
        return true;
      }
      return false;
    };

    if (m_tutorialOverlay->GetStep() ==
            game::controllers::TutorialStep::LinkCup &&
        !m_tutorialFairwayGuidanceApplied) {
      m_tutorialFairwayGuidanceApplied =
          guideTowardsHole("フェアウェイ", true);
    }
    if (m_tutorialOverlay->GetStep() ==
            game::controllers::TutorialStep::GoalCup &&
        !m_tutorialGoalGuidanceApplied) {
      if (m_aimPinController) m_aimPinController->ClearPin(ctx);
      m_tutorialGoalGuidanceApplied = guideTowardsHole("ゴール", false);
    }

    tutorialInputLocked = m_tutorialOverlay->IsInputLocked();
    tutorialPolicy = m_tutorialOverlay->GetInputPolicy();
    tutorialMinimapVisible =
        m_tutorialOverlay->GetPresentationPolicy().minimap;
    if (m_minimapController && tutorialMinimapVisible) {
      m_minimapController->SetVisible(ctx, true);
    }
  }

  // マップビュー更新
  bool isMapView = false;
  bool pointerOverHudControls = false;
  bool wasMapView = m_minimapController && m_minimapController->IsMapView();
  float fieldW = 80.0f;
  float fieldD = 120.0f;
  if (m_pageLoader) {
      fieldW = m_pageLoader->GetFieldWidth();
      fieldD = m_pageLoader->GetFieldDepth();
  }
  if (m_minimapController &&
      (!m_isTutorial || tutorialPolicy.map || tutorialMinimapVisible)) {
      PROFILE_SCOPE("WikiGolf.Minimap");
      if (!m_isTutorial || tutorialPolicy.map) {
          if (m_isTutorial && m_tutorialOverlay) {
              m_minimapController->ProcessInput(
                  ctx, mouseX, mouseY, fieldW, fieldD, m_skyboxEntity,
                  m_tutorialOverlay->GetMapInputPermissions());
          } else {
              m_minimapController->ProcessInput(
                  ctx, mouseX, mouseY, fieldW, fieldD, m_skyboxEntity);
          }
      }
      static const std::vector<ecs::Entity> noTrajectory;
      const auto &trajectoryEntities = m_trajectoryPredictor
                                           ? m_trajectoryPredictor->GetDots()
                                           : noTrajectory;
      const bool mapViewNow = m_minimapController->IsMapView();
      if (mapViewNow) {
          // 全体マップビューは実カメラの透視投影でマーカーを再計算するため、
          // カメラ位置を確定させてから(=UpdateMapCameraを先に)投影しないと、
          // カメラのLERP移動中(パン/ズーム操作中)に1フレーム分古い位置で
          // 投影してしまい、動かすほどズレが目立つ。間引きも行わず毎フレーム
          // 追従させる。
          m_minimapController->UpdateMapCamera(ctx, fieldW, fieldD);
          m_minimapController->UpdateMinimap(ctx, fieldW, fieldD,
                                              trajectoryEntities);
      } else {
          constexpr float kMinimapInterval = 1.0f / 30.0f;
          m_minimapUpdateTimer += dt;
          if (m_minimapUpdateTimer >= kMinimapInterval) {
              m_minimapController->UpdateMinimap(ctx, fieldW, fieldD,
                                                  trajectoryEntities);
              m_minimapUpdateTimer = 0.0f;
          }
      }
      isMapView = m_minimapController->IsMapView();
      pointerOverHudControls =
          m_minimapController->IsPointerOverFlagFilters(mouseX, mouseY);
  }
  // チュートリアルの目次の上で押したボタンは、ピン置き・視点・ショットへ渡さない。
  if (m_isTutorial && m_tutorialOverlay &&
      m_tutorialOverlay->IsPointerOnGuide()) {
      pointerOverHudControls = true;
  }

  const bool escapeHandledByMapView =
      wasMapView && ctx.input.GetKeyDown(VK_ESCAPE);
  // チュートリアル中も、終わって接続を確かめている間を除けば Esc でポーズを
  // 開ける（ポーズの「タイトルへ戻る」で、途中でやめられるようにする）。
  const bool pauseAllowed =
      m_isTutorial ? (m_tutorialOverlay && !m_tutorialOverlay->IsDone())
                   : !tutorialInputLocked;
  if (pauseAllowed && !escapeHandledByMapView &&
      ctx.input.GetKeyDown(VK_ESCAPE)) {
    OpenPauseScene(ctx);
    return;
  }

  if (!m_isTutorial && !tutorialInputLocked && !isMapView && ctx.input.GetKeyDown(VK_BACK) &&
      ReturnToPreviousPage(ctx)) {
    return;
  }

  if (m_prevTutorialInputLocked && !tutorialInputLocked && m_cameraController) {
      m_cameraController->ResetForTransition(scene_detail::kFieldScale);
  }
  m_prevTutorialInputLocked = tutorialInputLocked;

  // エイムピン設置（マップ中クリック／三人称視点中クリック）
  // 全体マップビューも実カメラ(傾いた透視投影)による俯瞰のため、レイキャストは
  // 両モード共通で扱える。設置されたら、ピンとの高低差を織り込んだ実効飛距離
  // (RefreshAimPinSolution)に最も近いクラブへ自動的に切り替える。
  if (m_aimPinController && m_clubController &&
      (!m_isTutorial || tutorialPolicy.aimPin) &&
      !pointerOverHudControls &&
      shot->phase == game::components::ShotState::Phase::Idle) {
      PROFILE_SCOPE("WikiGolf.AimPin");
      game::controllers::AimPinController::UpdateParams pinParams;
      pinParams.mouseX = mouseX;
      pinParams.mouseY = mouseY;
      pinParams.allowInput = state->canShoot;
      pinParams.ballEntity = m_ballEntity;
      pinParams.cameraEntity = m_cameraEntity;
      pinParams.terrainSystem = m_terrainSystem.get();
      if (!isMapView && m_minimapController) {
          pinParams.hasMappedWorldPosition =
              m_minimapController->TryGetHudMapWorldPosition(
                  ctx, mouseX, mouseY, fieldW, fieldD,
                  pinParams.mappedWorldPosition);
      }
      auto pinResult = m_aimPinController->Update(ctx, pinParams);
      if (pinResult.pinPlaced) {
          RefreshAimPinSolution(ctx, true);
          if (m_cameraController) {
              const auto *aimPin =
                  ctx.world.GetGlobal<game::components::AimPinState>();
              if (aimPin && aimPin->active) {
                  m_cameraController->BeginTargetFraming(
                      ctx, aimPin->worldPosition,
                      m_clubController->GetRecommendedCameraDistance(
                          scene_detail::kFieldScale));
              }
          }

          // マップ上でピンを設置したら、狙いを確認できたのでマップビューを
          // 自動的に閉じてショット画面へ戻す。チュートリアルのマップ説明中
          // (ピン設置後もヘルプ表示等の案内を続ける)は対象外とする。
          if (!m_isTutorial && isMapView && m_minimapController) {
              m_minimapController->ToggleMapView(ctx, m_skyboxEntity);
              isMapView = m_minimapController->IsMapView();
              m_minimapController->SetLandingPreview(ctx, {0, 0, 0}, 0.0f, false);
          }
      }
  }

  // クラブ更新 (Q/Eはマップビュー中も操作できる。マップの他の操作キーとは
  // 重複しないため、ここでは isMapView による制限をかけない)
  if (m_clubController && (!m_isTutorial || tutorialPolicy.club) &&
      shot->phase == game::components::ShotState::Phase::Idle) {
      PROFILE_SCOPE("WikiGolf.ClubInput");
      game::controllers::ClubController::InputParams cParams;
      cParams.allowInput = state->canShoot;
      auto cResult = m_clubController->UpdateInput(ctx, cParams);
      if (cResult.clubChanged) {
          // クラブが変われば同じピンでも必要なパワーが変わるため、
          // バーのピン位置表示を新しいクラブで解き直す。
          RefreshAimPinSolution(ctx, false);
          if (m_cameraController) {
              std::optional<DirectX::XMFLOAT3> framingTarget;
              const auto *aimPin =
                  ctx.world.GetGlobal<game::components::AimPinState>();
              if (aimPin && aimPin->active) {
                  framingTarget = aimPin->worldPosition;
              } else {
                  framingTarget = CalculateFullSwingLandingTarget(ctx);
              }
              if (framingTarget) {
                  m_cameraController->BeginTargetFraming(
                      ctx, *framingTarget,
                      m_clubController->GetRecommendedCameraDistance(
                          scene_detail::kFieldScale));
              }
          }
      }
  }

  // カメラ更新
  if (m_cameraController && (!m_isTutorial || tutorialPolicy.camera) &&
      !isMapView && !pointerOverHudControls) {
      PROFILE_SCOPE("WikiGolf.Camera");
      m_cameraController->ProcessInput(ctx, mouseX, mouseY);
  }

  // ショット処理
  if (m_shotController && (!m_isTutorial || tutorialPolicy.shot) &&
      !isMapView && !pointerOverHudControls) {
      PROFILE_SCOPE("WikiGolf.Shot");
      auto event = m_shotController->ProcessShot(ctx, state->canShoot, m_hud.get(), m_clubController.get());
      if (event.shotFired) {
          state->canShoot = false;
          m_cupApproachEffectTriggered = false;
          m_cupApproachZoomTimer = 0.0f;
          if (m_gameJuice) m_gameJuice->ResetFov();
          if (m_aimPinController) m_aimPinController->ClearPin(ctx);
          // スイング(Hit)開始。ボールはクラブが最下点に達した時点で発射する
          m_pendingShotDirection = {0, 0, 1};
          if (m_cameraController) {
              m_pendingShotDirection = m_cameraController->GetShotDirection();
          }
          m_pendingLaunchTimer =
              m_clubController ? m_clubController->GetImpactDelay() : 0.0f;
          if (m_cameraController) {
              if (m_clubController) {
                  m_cameraController->SetGolferAnchor(
                      m_clubController->GetGolferPosition(),
                      m_clubController->GetGolferHeight());
              }
              m_cameraController->OnShotStart(ctx);
          }

          // 打球判定の演出表示（着地地形の演出とは別エンティティ・別カーブ）
          auto feedback = game::utils::BuildJudgeFeedback(shot->judgement);
          if (feedback.HasVisual() && m_judgeImageEntity != UINT32_MAX) {
              auto* ui = ctx.world.Get<game::components::UIImage>(m_judgeImageEntity);
              if (ui) {
                  ui->texturePath = feedback.texturePath;
                  ui->visible = true;
                  ui->alpha = 0.0f;
                  ui->width = 0.0f;
                  ui->height = 0.0f;
                  ui->rotation = 0.0f;
                  ui->x = 1280.0f * 0.5f;
                  ui->y = game::ui::kJudgeImageCenterY;
                  m_judgeDisplayTimer = feedback.displaySeconds;
                  m_judgeDisplayTotal  = feedback.displaySeconds;
                  m_judgeDisplayTargetW = feedback.width;
                  m_judgeDisplayTargetH = feedback.height;
                  m_judgeDisplayJudgement = shot->judgement;
              }
          }
      }
  }

  // クラブが最下点に達したタイミングでボールを発射する
  if (m_pendingLaunchTimer >= 0.0f) {
      m_pendingLaunchTimer -= dt;
      if (m_pendingLaunchTimer <= 0.0f) {
          m_pendingLaunchTimer = -1.0f;
          if (m_shotController && m_clubController && shot->swingCommitted) {
              // スタートティーピンによる固定は、実際にボールへ初速が加わる
              // この瞬間まで維持する。
              if (m_pageLoader) m_pageLoader->ClearStartPin(ctx, m_ballEntity);
              m_shotController->ExecuteShot(ctx, m_ballEntity, m_pendingShotDirection,
                                            m_clubController->GetCurrentClub(),
                                            &m_timeOfDay, m_hud.get());
          }
      }
  }

  // クラブアニメーション更新
  if (m_clubController) {
      PROFILE_SCOPE("WikiGolf.ClubAnimation");
      DirectX::XMFLOAT3 shotDir{0, 0, 1};
      if (m_cameraController) {
          shotDir = m_cameraController->GetShotDirection();
      }
      m_clubController->UpdateAnimation(ctx, dt, m_ballEntity, shotDir);
  }

  // 物理更新
  // ショット実行中、ボール着地後に規定秒数が経過すると待機時間短縮のため
  // 物理シミュレーションを倍速で進める（詳細: BallFastForwardTimer）。
  UpdateCupApproachEffects(ctx, *state, *shot, dt);
  const float fastForwardMultiplier = m_fastForwardTimer.Update(ctx, dt);
  float simulationTimeScale = 1.0f;
  if (m_gameJuice) {
    simulationTimeScale = m_gameJuice->ConsumeTimeScale(dt);
  }
  float physicsTimeMultiplier = fastForwardMultiplier;
  if (simulationTimeScale < 1.0f) {
    physicsTimeMultiplier = simulationTimeScale;
  }
  game::systems::PhysicsSystem(
      ctx, dt * physicsTimeMultiplier);
  m_fastForwardIndicator.Update(ctx, dt, m_fastForwardTimer.GetCurrentTier());

  // 物理後のボール位置を使い、追従カメラの1フレーム遅延を防ぐ。
  if (m_cameraController &&
      (!m_isTutorial || tutorialPolicy.camera || tutorialPolicy.shot) &&
      !isMapView) {
      PROFILE_SCOPE("WikiGolf.CameraFollow");
      m_cameraController->Update(ctx);
  }

  // カップイン判定を地形判定の前に行う（遷移時は以降の処理をスキップ）
  if (CheckCupIn(ctx)) return;

  // ボール静止・OB・地形判定ロジック
  if (shot->phase == game::components::ShotState::Phase::Executing) {
      auto *rb = ctx.world.Get<game::components::RigidBody>(m_ballEntity);
      if (rb) {
          float speed = std::sqrt(rb->velocity.x * rb->velocity.x +
                                  rb->velocity.y * rb->velocity.y +
                                  rb->velocity.z * rb->velocity.z);
          if (speed < 0.1f && state->isBallGrounded) {
              rb->velocity = {0, 0, 0};
              std::string terrainTex = "";
              bool treatAsOB = false;
              const bool wasOB = state->isOB; // state->isOB はこの後リセットされるため先に控える

              auto GetTerrainTex = [](const std::string& preferred, const std::string& fallback) {
                  if (std::filesystem::exists("Assets/textures/" + preferred)) {
                      return preferred;
                  }
                  return fallback;
              };

              if (state->isOB) {
                  terrainTex = GetTerrainTex("ui_terrain_ob.png", "ui_judge_miss.png");
                  LOG_INFO("WikiGolf", "OB! Returning to last shot position");
                  state->shotCount++;
                  state->totalShotCount++;
                  auto *ballT = ctx.world.Get<game::components::Transform>(m_ballEntity);
                  if (ballT) {
                      ballT->position = state->lastShotPosition;
                      ballT->position.y += 0.5f;
                  }
                  state->isOB = false;

                  if (ctx.audio) {
                      ctx.audio->PlaySE(ctx, "se_OB.wav", 0.8f);
                  }
              } else {
                  switch (state->currentMaterial) {
                  case game::components::TerrainMaterial::Fairway:
                      terrainTex = GetTerrainTex("ui_terrain_fairway.png", "ui_judge_nice.png");
                      break;
                  case game::components::TerrainMaterial::Rough:
                      terrainTex = GetTerrainTex("ui_terrain_rough.png", "ui_judge_miss.png");
                      break;
                  case game::components::TerrainMaterial::Bunker:
                      terrainTex = GetTerrainTex("ui_terrain_bunker.png", "ui_judge_miss.png");
                      break;
                  case game::components::TerrainMaterial::Green:
                      terrainTex = GetTerrainTex("ui_terrain_green.png", "ui_judge_perfect.png");
                      break;
                  case game::components::TerrainMaterial::Ice:
                      terrainTex = GetTerrainTex("ui_terrain_fairway.png", "ui_judge_nice.png");
                      break;
                  case game::components::TerrainMaterial::Stone:
                      terrainTex = GetTerrainTex("ui_terrain_rough.png", "ui_judge_miss.png");
                      break;
                  case game::components::TerrainMaterial::Water:
                  case game::components::TerrainMaterial::Lava:
                      terrainTex = GetTerrainTex("ui_terrain_ob.png", "ui_judge_miss.png");
                      treatAsOB = true;
                      break;
                  default:
                      terrainTex = GetTerrainTex("ui_terrain_rough.png", "ui_judge_miss.png");
                      break;
                  }

                  if (treatAsOB) {
                      LOG_INFO("WikiGolf", "Out-of-bounds terrain. Material: {}", (int)state->currentMaterial);
                      state->shotCount++;
                      state->totalShotCount++;
                      auto *ballT = ctx.world.Get<game::components::Transform>(m_ballEntity);
                      if (ballT) {
                          ballT->position = state->lastShotPosition;
                          ballT->position.y += 0.5f;
                      }
                      if (ctx.audio) {
                          ctx.audio->PlaySE(ctx, "se_OB.wav", 0.8f);
                      }
                  }
              }

              if (!terrainTex.empty() && m_terrainImageEntity != UINT32_MAX) {
                  auto *ui = ctx.world.Get<game::components::UIImage>(m_terrainImageEntity);
                  if (ui) {
                      ui->texturePath = terrainTex;
                      ui->visible = true;
                      ui->alpha = 0.0f; // Updateでフェードイン
                      ui->width = 0.0f;
                      ui->height = 0.0f;
                      ui->rotation = 0.0f;
                      ui->x = 1280.0f * 0.5f;
                      ui->y = 720.0f * 0.5f;
                      m_terrainDisplayTimer = 2.0f;
                      m_terrainDisplayTotal  = 2.0f;
                      m_terrainDisplayTargetW = 512.0f;
                      m_terrainDisplayTargetH = 256.0f;
                      if (wasOB || treatAsOB) {
                          m_terrainDisplayTier = TerrainResultTier::Rough;
                      } else {
                          using Material = game::components::TerrainMaterial;
                          switch (state->currentMaterial) {
                          case Material::Green:                    m_terrainDisplayTier = TerrainResultTier::Perfect; break;
                          case Material::Fairway:
                          case Material::Ice:                      m_terrainDisplayTier = TerrainResultTier::Good;    break;
                          default:                                 m_terrainDisplayTier = TerrainResultTier::Rough;   break;
                          }
                      }

                      if (ctx.audio && !state->isOB && !treatAsOB) {
                          std::string seName = "";
                          switch (state->currentMaterial) {
                          case game::components::TerrainMaterial::Fairway:
                              seName = "se_Fairway.mp3";
                              break;
                          case game::components::TerrainMaterial::Rough:
                              seName = "se_Rough.mp3";
                              break;
                          case game::components::TerrainMaterial::Bunker:
                              seName = "se_Bunker.mp3";
                              break;
                          case game::components::TerrainMaterial::Green:
                              seName = "se_Green.mp3";
                              break;
                          default: break;
                          }

                          if (!seName.empty()) {
                              ctx.audio->PlaySE(ctx, seName, 0.8f);
                          }
                      }
                  }
              }

              shot->phase = game::components::ShotState::Phase::ShowResult;
              shot->resultDisplayTime = 1.0f;
              state->canShoot = true;
          }
      }
  }

  // 判定結果表示終了とカメラ復帰
  if (shot->phase == game::components::ShotState::Phase::ShowResult) {
      shot->resultDisplayTime -= dt;
      if (shot->resultDisplayTime <= 0.0f) {
          shot->phase = game::components::ShotState::Phase::RestoringCamera;
          shot->resultDisplayTime = 0.2f; // カメラ復帰までの間
      }
  } else if (shot->phase == game::components::ShotState::Phase::RestoringCamera) {
      shot->resultDisplayTime -= dt;
  }

  // カメラ復帰（暗転なし、少し間を置いてから）
  if (shot->phase == game::components::ShotState::Phase::RestoringCamera &&
      shot->resultDisplayTime <= 0.0f) {
      if (m_cameraController) m_cameraController->RestoreAfterFade(ctx);
      shot->Reset();
      if (m_hud) m_hud->ResetShotUI(ctx);
  }

  UpdateTrajectoryAndGuide(ctx, *state, *shot, dt, tutorialInputLocked, isMapView);

  UpdateHudAndEffects(ctx, *state, *shot, dt);

  UpdateResultVisuals(ctx, dt);
}


} // namespace game::scenes
