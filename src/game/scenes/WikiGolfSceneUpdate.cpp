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
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>

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

  if (m_pageLoader) {
      PROFILE_SCOPE("WikiGolf.PageLoaderAsync");
      m_pageLoader->UpdateAsyncPathEvaluation(ctx);

      if (m_phase == ScenePhase::Playing) {
          if (auto* ballT = ctx.world.Get<game::components::Transform>(m_ballEntity)) {
              m_pageLoader->UpdateNearbyHoleSignboards(ctx, ballT->position, m_cameraEntity);
          }
      }
  }

  if (m_phase == ScenePhase::Transitioning && m_transitionController) {
      PROFILE_SCOPE("WikiGolf.Transition");
      bool finished = m_transitionController->Update(ctx);
      if (finished) {
          m_phase = ScenePhase::Playing;
          m_prevTutorialInputLocked = false;
          // 次のステージ開始時は前のホールで使っていたクラブ（パター等）を
          // 持ち越さず、ドライバーに戻す
          if (m_clubController) m_clubController->ResetToFirstClub(ctx);
          // ロード完了: HUD/ミニマップを再表示する
          if (m_hud) m_hud->SetVisible(ctx, true);
          if (m_minimapController) m_minimapController->SetVisible(ctx, true);
          if (m_cameraController) m_cameraController->Update(ctx);
          // チュートリアルオーバーレイはロード演出完了後に初期化する
          // （ロード中にUIが重なって表示されるのを防ぐ）
          if (m_isTutorial && !m_tutorialOverlay) {
              m_tutorialOverlay = std::make_unique<game::controllers::TutorialOverlayController>();
              m_tutorialOverlay->Initialize(ctx);
              CreateTutorialFlagSamples(ctx);

              std::vector<game::controllers::TutorialOverlayController::EventCameraTarget> targets = {
                  { {0.0f, 15.0f, -25.0f}, {0.0f, 0.0f, -10.0f}, L"Fairway (フェアウェイ)", L"ボールが転がりやすい標準的な地形です。" },
                  { {-15.0f, 15.0f, -15.0f}, {-15.0f, 0.0f, 0.0f}, L"Rough (ラフ)", L"草が深く、ボールの転がりが少し悪くなります。" },
                  { {24.0f, 15.0f, -18.0f}, {24.0f, 0.0f, -3.0f}, L"Bunker (バンカー)", L"砂地です。転がりにくく、パワーも落ちやすくなります。" },
                  { {0.0f, 15.0f, 37.0f}, {0.0f, 0.0f, 52.0f}, L"Green (グリーン)", L"カップ周りの滑らかな地形です。よく転がります。" },
                  { {-24.0f, 15.0f, 5.0f}, {-24.0f, 0.0f, 20.0f}, L"OB / Water / Lava", L"水や溶岩などの危険エリア。入るとペナルティで1打戻されます。" }
              };
              std::vector<game::controllers::TutorialOverlayController::EventCameraTarget> flagTargets = {
                  { {-37.5f, 10.0f, 18.0f}, {-37.5f, 1.4f, 30.0f}, L"赤い旗 / 目的地", L"赤はターゲット記事へのホールです。ここに入れるとチュートリアルクリアです。" },
                  { {-15.0f, 11.0f, 16.0f}, {-15.0f, 1.4f, 30.0f}, L"黄・橙の旗", L"黄は目的地まで1リンク、橙は2リンク先のホールです。近道候補になります。" },
                  { {15.0f, 11.0f, 16.0f}, {15.0f, 1.4f, 30.0f}, L"白・灰・青の旗", L"白は3〜5リンク、灰は6リンク以上、青は距離が未解析です。赤に近い色ほど有利です。" }
              };
              m_tutorialOverlay->SetEventCameraTargets(m_cameraEntity,
                                                       std::move(targets),
                                                       std::move(flagTargets));
          }
      }
      return;
  }

  auto mousePos = ctx.input.GetMousePosition();
  int mouseX = mousePos.x;
  int mouseY = mousePos.y;

  bool tutorialInputLocked = false;
  if (m_isTutorial && m_tutorialOverlay) {
    PROFILE_SCOPE("WikiGolf.TutorialOverlay");
    m_tutorialOverlay->Update(ctx, m_cameraController.get(), m_clubController.get(), m_shotController.get(), m_minimapController.get());
    if (m_tutorialOverlay->IsDone()) {
      // チュートリアル終了後、タイトルへ戻る処理など
      // （フラグの保存はTitleScene側か、ここで行う）
      std::string path = "save_tutorial_done.flag";
      std::ofstream ofs(path);
      ofs << "done";
      ofs.close();

      auto loadingScene = std::make_unique<LoadingScene>([]() { return std::make_unique<TitleScene>(); });
      ctx.sceneManager->ChangeScene(std::move(loadingScene));
      return;
    }
    tutorialInputLocked = m_tutorialOverlay->IsInputLocked();
  }

  // マップビュー更新
  bool isMapView = false;
  bool wasMapView = m_minimapController && m_minimapController->IsMapView();
  if (m_minimapController && !tutorialInputLocked) {
      PROFILE_SCOPE("WikiGolf.Minimap");
      float fieldW = 80.0f;
      float fieldD = 120.0f;
      if (m_pageLoader) {
          fieldW = m_pageLoader->GetFieldWidth();
          fieldD = m_pageLoader->GetFieldDepth();
      }
      m_minimapController->ProcessInput(ctx, mouseX, mouseY, fieldW, fieldD, m_skyboxEntity);
      DirectX::XMFLOAT3 shotDir{0, 0, 1};
      if (m_cameraController) {
          shotDir = m_cameraController->GetShotDirection();
      }
      const bool mapViewNow = m_minimapController->IsMapView();
      if (mapViewNow) {
          // 全体マップビューは実カメラの透視投影でマーカーを再計算するため、
          // カメラ位置を確定させてから(=UpdateMapCameraを先に)投影しないと、
          // カメラのLERP移動中(パン/ズーム操作中)に1フレーム分古い位置で
          // 投影してしまい、動かすほどズレが目立つ。間引きも行わず毎フレーム
          // 追従させる。
          m_minimapController->UpdateMapCamera(ctx, fieldW, fieldD);
          m_minimapController->UpdateMinimap(ctx, fieldW, fieldD, shotDir);
      } else {
          constexpr float kMinimapInterval = 1.0f / 30.0f;
          m_minimapUpdateTimer += dt;
          if (m_minimapUpdateTimer >= kMinimapInterval) {
              m_minimapController->UpdateMinimap(ctx, fieldW, fieldD, shotDir);
              m_minimapUpdateTimer = 0.0f;
          }
      }
      isMapView = m_minimapController->IsMapView();
  }

  // クラブ選択パネル横: 着弾点プレビュー(トップビュー)トグルボタン
  if (m_hud && m_minimapController && !tutorialInputLocked) {
      PROFILE_SCOPE("WikiGolf.LandingPreviewButton");
      const bool btnEnabled = !isMapView && state->canShoot &&
          shot->phase == game::components::ShotState::Phase::Idle;
      const bool hovered =
          mouseX >= game::ui::kLandingPreviewBtnX &&
          mouseX <= game::ui::kLandingPreviewBtnX + game::ui::kLandingPreviewBtnW &&
          mouseY >= game::ui::kLandingPreviewBtnY &&
          mouseY <= game::ui::kLandingPreviewBtnY + game::ui::kLandingPreviewBtnH;

      if ((btnEnabled || isMapView) && hovered && ctx.input.GetMouseButtonDown(0)) {
          m_minimapController->ToggleMapView(ctx, m_skyboxEntity);
          isMapView = m_minimapController->IsMapView();
          if (isMapView) {
              RefreshLandingPreview(ctx);
          } else {
              m_minimapController->SetLandingPreview(ctx, {0, 0, 0}, 0.0f, false);
          }
      }

      m_hud->UpdateLandingPreviewButton(ctx, hovered, isMapView, btnEnabled || isMapView);
  }

  const bool escapeHandledByMapView =
      wasMapView && ctx.input.GetKeyDown(VK_ESCAPE);
  if (!tutorialInputLocked && !escapeHandledByMapView &&
      ctx.input.GetKeyDown(VK_ESCAPE)) {
    OpenPauseScene(ctx);
    return;
  }

  if (!tutorialInputLocked && !isMapView && ctx.input.GetKeyDown(VK_BACK) &&
      ReturnToPreviousPage(ctx)) {
    return;
  }

  if (m_prevTutorialInputLocked && !tutorialInputLocked && m_cameraController) {
      m_cameraController->ResetForTransition(scene_detail::kFieldScale);
  }
  m_prevTutorialInputLocked = tutorialInputLocked;

  // エイムピン設置（マップ中クリック／三人称視点中クリック）
  // 全体マップビューも実カメラ(傾いた透視投影)による俯瞰のため、レイキャストは
  // 両モード共通で扱える。設置されたら、ボールからの距離に最も飛距離が
  // 近いクラブへ自動的に切り替える。
  if (m_aimPinController && m_clubController && !tutorialInputLocked &&
      shot->phase == game::components::ShotState::Phase::Idle) {
      PROFILE_SCOPE("WikiGolf.AimPin");
      game::controllers::AimPinController::UpdateParams pinParams;
      pinParams.mouseX = mouseX;
      pinParams.mouseY = mouseY;
      pinParams.allowInput = state->canShoot;
      pinParams.ballEntity = m_ballEntity;
      pinParams.cameraEntity = m_cameraEntity;
      pinParams.terrainSystem = m_terrainSystem.get();
      auto pinResult = m_aimPinController->Update(ctx, pinParams);
      if (pinResult.pinPlaced) {
          m_clubController->SelectClubForDistance(ctx, pinResult.distance);
          if (m_cameraController) {
              m_cameraController->SetTargetDistanceAndHeight(
                  m_clubController->GetRecommendedCameraDistance(4.0f),
                  m_clubController->GetRecommendedCameraHeight(4.0f));

              // ピンを狙った方向へ視線(ショット方向)も合わせる。
              const auto *aimPin =
                  ctx.world.GetGlobal<game::components::AimPinState>();
              auto *ballTransform =
                  ctx.world.Get<game::components::Transform>(m_ballEntity);
              if (aimPin && ballTransform) {
                  m_cameraController->AimYawTowards(ballTransform->position,
                                                    aimPin->worldPosition);
              }
          }
      }
  }

  // クラブ更新 (Q/Eはマップビュー中も操作できる。マップの他の操作キーとは
  // 重複しないため、ここでは isMapView による制限をかけない)
  if (m_clubController && !tutorialInputLocked &&
      shot->phase == game::components::ShotState::Phase::Idle) {
      PROFILE_SCOPE("WikiGolf.ClubInput");
      game::controllers::ClubController::InputParams cParams;
      cParams.allowInput = state->canShoot;
      auto cResult = m_clubController->UpdateInput(ctx, cParams);
      if (cResult.clubChanged && m_cameraController) {
          m_cameraController->SetTargetDistanceAndHeight(
              m_clubController->GetRecommendedCameraDistance(4.0f),
              m_clubController->GetRecommendedCameraHeight(4.0f));
      }
  }

  // カメラ更新
  if (m_cameraController && !tutorialInputLocked && !isMapView) {
      PROFILE_SCOPE("WikiGolf.Camera");
      m_cameraController->ProcessInput(ctx, mouseX, mouseY);
  }

  // ショット処理
  if (m_shotController && !tutorialInputLocked && !isMapView) {
      PROFILE_SCOPE("WikiGolf.Shot");
      auto event = m_shotController->ProcessShot(ctx, state->canShoot, m_hud.get(), m_clubController.get());
      if (event.shotFired) {
          state->canShoot = false;
          if (m_aimPinController) m_aimPinController->ClearPin(ctx);
          if (m_cameraController) m_cameraController->OnShotStart(ctx, shot->confirmedPower);
          if (m_clubController) {
              DirectX::XMFLOAT3 shotDir{0, 0, 1};
              if (m_cameraController) {
                  shotDir = m_cameraController->GetShotDirection();
              }
              m_shotController->ExecuteShot(ctx, m_ballEntity, shotDir, m_clubController->GetCurrentClub(), &m_timeOfDay, m_hud.get());
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

  // クラブアニメーション更新
  if (m_clubController && !tutorialInputLocked) {
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
  const float fastForwardMultiplier = m_fastForwardTimer.Update(ctx, dt);
  game::systems::PhysicsSystem(ctx, dt * fastForwardMultiplier);
  m_fastForwardIndicator.Update(ctx, dt, m_fastForwardTimer.GetCurrentTier());

  // 物理後のボール位置を使い、追従カメラの1フレーム遅延を防ぐ。
  if (m_cameraController && !tutorialInputLocked && !isMapView) {
      PROFILE_SCOPE("WikiGolf.CameraFollow");
      m_cameraController->Update(ctx);
  }

  if (m_terrainSystem && !isMapView) {
      PROFILE_SCOPE("WikiGolf.SurfaceGrassStreaming");
      m_terrainSystem->UpdateSurfaceGrass(ctx, m_cameraEntity);
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
                  auto *ballT = ctx.world.Get<game::components::Transform>(m_ballEntity);
                  if (ballT) {
                      ballT->position = state->lastShotPosition;
                      ballT->position.y += 0.5f;
                  }
                  state->isOB = false;

                  if (ctx.audio) {
                      if (std::filesystem::exists("Assets/sounds/se_OB.wav")) {
                          ctx.audio->PlaySE(ctx, "se_OB.wav", 0.8f);
                      } else {
                          ctx.audio->PlaySE(ctx, "se_judge_ob.mp3", 0.8f);
                      }
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
                      auto *ballT = ctx.world.Get<game::components::Transform>(m_ballEntity);
                      if (ballT) {
                          ballT->position = state->lastShotPosition;
                          ballT->position.y += 0.5f;
                      }
                      if (ctx.audio) {
                          if (std::filesystem::exists("Assets/sounds/se_OB.wav")) {
                              ctx.audio->PlaySE(ctx, "se_OB.wav", 0.8f);
                          } else {
                              ctx.audio->PlaySE(ctx, "se_judge_ob.mp3", 0.8f);
                          }
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
                              if (std::filesystem::exists("Assets/sounds/se_Fairway.wav")) {
                                  seName = "se_Fairway.wav";
                              } else {
                                  seName = "se_Fairway.mp3";
                              }
                              break;
                          case game::components::TerrainMaterial::Rough:
                              if (std::filesystem::exists("Assets/sounds/se_Rough.wav")) {
                                  seName = "se_Rough.wav";
                              } else {
                                  seName = "se_Rough.mp3";
                              }
                              break;
                          case game::components::TerrainMaterial::Bunker:
                              if (std::filesystem::exists("Assets/sounds/se_Bunker_new.mp3")) {
                                  seName = "se_Bunker_new.mp3";
                              } else {
                                  seName = "se_Bunker.mp3";
                              }
                              break;
                          case game::components::TerrainMaterial::Green:
                              if (std::filesystem::exists("Assets/sounds/se_Green.mp3")) {
                                  seName = "se_Green.mp3";
                              } else if (std::filesystem::exists("Assets/sounds/se_Fairway.wav")) {
                                  seName = "se_Fairway.wav";
                              } else {
                                  seName = "se_Fairway.mp3";
                              }
                              break;
                          default: break;
                          }

                          if (!seName.empty() && std::filesystem::exists("Assets/sounds/" + seName)) {
                              ctx.audio->PlaySE(ctx, seName, 0.8f);
                          } else {
                              // フォールバック: 新SEが見つからない場合のみ従来音を検討
                              bool isGood = (state->currentMaterial == game::components::TerrainMaterial::Fairway ||
                                             state->currentMaterial == game::components::TerrainMaterial::Green);
                              if (!isGood) ctx.audio->PlaySE(ctx, "judge_Bad.wav", 0.8f);
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
          m_screenFade.SetCenter(0.5f, 0.5f);
          m_screenFade.FadeOut(0.4f, game::utils::FadeType::CircleWipe, {0, 0, 0});
      }
  }

  // カメラフェード復帰
  if (shot->phase == game::components::ShotState::Phase::RestoringCamera && !m_screenFade.IsFading()) {
      if (m_cameraController) m_cameraController->RestoreAfterFade(ctx);
      shot->Reset();
      if (m_hud) m_hud->ResetShotUI(ctx);
      m_screenFade.FadeIn(0.4f, game::utils::FadeType::CircleWipe, {0, 0, 0});
  }

  UpdateTrajectoryAndGuide(ctx, *state, *shot, dt, tutorialInputLocked, isMapView);

  UpdateHudAndEffects(ctx, *state, *shot, dt);

  UpdateResultVisuals(ctx, dt);
}


} // namespace game::scenes
