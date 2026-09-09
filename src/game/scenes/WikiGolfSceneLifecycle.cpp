/**
 * @file WikiGolfSceneLifecycle.cpp
 * @brief WikiGolfシーンの遷移と後処理を実装します。
*/

#include "WikiGolfScene.h"
#include "WikiGolfSceneSupport.h"
#include "../../audio/AudioSystem.h"
#include "../../core/GameContext.h"
#include "../../core/Logger.h"
#include "../../core/SceneManager.h"
#include "../../ecs/World.h"
#include "../components/MeshRenderer.h"
#include "../components/PhysicsComponents.h"
#include "../components/Transform.h"
#include "../components/UIText.h"
#include "../components/UIImage.h"
#include "../components/WikiComponents.h"
#include "../controllers/MinimapController.h"
#include "../systems/GameJuiceSystem.h"
#include "../systems/WikiClient.h"
#include "../systems/WikiShortestPath.h"
#include "../utils/GameplayPhysicsConstants.h"
#include "../utils/PageHistoryUtils.h"
#include "../utils/ProceduralFlag.h"
#include "../utils/TrajectorySimulation.h"
#include "PauseScene.h"

namespace game::scenes {

using namespace game::components;

/**
 * @brief プロシージャル旗のなびきと旗粒子を更新します。
*/
void WikiGolfScene::UpdateProceduralFlagEffects(core::GameContext &ctx,
                                                float dt) {
  m_flagEffectTimer += dt;
}

/**
 * @brief トップビューの着弾点プレビューを、現在選択中クラブのフルスイングで
 * 計算し直し、MinimapControllerへ反映します。
 * @details 実際の地形・風を考慮した弾道シミュレーション(TrajectoryPredictorと
 * 同じ物理ロジック)を使うため、着弾点は実際のショット結果と整合する。
 * クラブ切り替えはマップビュー中は行えないため、マップビューに入った
 * 瞬間に1回計算すれば十分。
*/
void WikiGolfScene::RefreshLandingPreview(core::GameContext &ctx) {
  if (!m_minimapController) return;

  if (!m_clubController || !ctx.world.IsAlive(m_ballEntity)) {
    m_minimapController->SetLandingPreview(ctx, {0, 0, 0}, 0.0f, false);
    return;
  }

  const auto &club = m_clubController->GetCurrentClub();
  auto *ballT = ctx.world.Get<game::components::Transform>(m_ballEntity);
  if (!ballT || club.baseCarryDistance <= 0.0f) {
    m_minimapController->SetLandingPreview(ctx, {0, 0, 0}, 0.0f, false);
    return;
  }

  DirectX::XMFLOAT3 shotDir{0, 0, 1};
  if (m_cameraController) {
    shotDir = m_cameraController->GetShotDirection();
  }

  game::physics::BallPhysicsParams ballParams;
  ballParams.rollingFrictionScale = club.rollingFrictionScale;

  game::physics::WindParams wind;
  if (auto *golfState =
          ctx.world.GetGlobal<game::components::GolfGameState>()) {
    wind.windSpeed = golfState->windSpeed;
    wind.windDirection = golfState->windDirection;
  }

  game::physics::FlatGroundParams flatGroundUnused; // enabled=false: 実地形を使う

  const auto result = game::physics::SimulateCarryDistance(
      club.maxPower, club.launchAngle, shotDir, ballT->position,
      m_terrainSystem.get(), flatGroundUnused, ballParams, wind);

  // ばらつき範囲は基準飛距離の一定割合(ミート精度による左右・距離ブレの
  // ざっくりした目安)とする。
  const float dispersionRadius = club.baseCarryDistance * 0.06f;
  m_minimapController->SetLandingPreview(ctx, result.landingPosition,
                                         dispersionRadius, true);
}

/**
 * @brief フィールド（床・壁）を作成します。
*/
void WikiGolfScene::CreateField(core::GameContext &ctx) {
  m_floorEntity = CreateEntity(ctx.world);
  auto &ft = ctx.world.Add<Transform>(m_floorEntity);
  ft.position = {0.0f, 0.0f, 0.0f};
  ft.scale = {20.0f * scene_detail::kFieldScale,
              0.5f * scene_detail::kFieldScale,
              30.0f * scene_detail::kFieldScale};

  // 地形下の白いプレーン表示を防ぐため床のコンポーネント付与を廃止
}

/**
 * @brief チュートリアルの旗色解説用に、一時的な実旗モデルを配置します。
 * @details ホールやコライダーは付けず、描画専用にしてショット判定へ干渉させません。
*/
void WikiGolfScene::CreateTutorialFlagSamples(core::GameContext &ctx) {
  ClearTutorialFlagSamples(ctx);

  struct FlagSample {
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT4 color;
  };

  const FlagSample samples[] = {
      {{-37.5f, 0.0f, 30.0f}, {1.0f, 0.2f, 0.2f, 1.0f}},   // 目的地
      {{-22.5f, 0.0f, 30.0f}, {1.0f, 0.85f, 0.0f, 1.0f}},  // 1リンク
      {{-7.5f, 0.0f, 30.0f}, {1.0f, 0.6f, 0.2f, 1.0f}},    // 2リンク
      {{7.5f, 0.0f, 30.0f}, {0.95f, 0.95f, 0.95f, 1.0f}},  // 3〜5リンク
      {{22.5f, 0.0f, 30.0f}, {0.6f, 0.6f, 0.6f, 1.0f}},    // 6リンク以上
      {{37.5f, 0.0f, 30.0f}, {0.25f, 0.65f, 1.0f, 1.0f}},  // 未解析
  };

  constexpr size_t sampleCount = sizeof(samples) / sizeof(samples[0]);
  m_tutorialFlagSampleEntities.reserve(sampleCount * 10);
  for (size_t i = 0; i < sampleCount; ++i) {
    const auto &sample = samples[i];
    float terrainH = 0.0f;
    if (m_terrainSystem) {
      terrainH = m_terrainSystem->GetHeight(sample.position.x, sample.position.z);
    }

    game::utils::ProceduralFlagOptions options;
    options.holeEntity = UINT32_MAX;
    options.large = (i == 0);
    options.createParticles = (i <= 1);
    options.animationWeight = 0.72f;
    if (i == 0) {
      options.animationWeight = 1.0f;
    }
    auto result = game::utils::CreateProceduralFlag(
        ctx, {sample.position.x, terrainH + 0.05f, sample.position.z},
        sample.color, options);
    for (auto entity : result.allEntities) {
      m_tutorialFlagSampleEntities.push_back(entity);
    }
  }
}

/**
 * @brief チュートリアルの旗色解説用に配置した一時旗モデルを破棄します。
*/
void WikiGolfScene::ClearTutorialFlagSamples(core::GameContext &ctx) {
  for (auto entity : m_tutorialFlagSampleEntities) {
    if (ctx.world.IsAlive(entity)) {
      ctx.world.DestroyEntity(entity);
    }
  }
  m_tutorialFlagSampleEntities.clear();
}

/**
 * @brief ボールをスポーンします。
*/
void WikiGolfScene::SpawnBall(core::GameContext &ctx) {
  if (ctx.world.IsAlive(m_ballEntity)) {
    ctx.world.DestroyEntity(m_ballEntity);
  }

  m_ballEntity = CreateEntity(ctx.world);
  auto &t = ctx.world.Add<Transform>(m_ballEntity);
  t.position = {0.0f, game::physics::kBallRadius,
                -8.0f * scene_detail::kFieldScale};

  auto ballMeshHandle = ctx.resource.LoadMesh("Assets/models/golfball.glb");

  // golfball.glb と builtin/sphere ではメッシュの内部スケールが異なるため、
  // 決め打ちの係数ではなく、ロード済みメッシュの実際のバウンディング半径から
  // 見た目の半径(kBallVisualScale基準)に必要な Transform.scale を逆算する。
  const float ballVisualRadius = game::physics::kBallVisualScale * 0.5f;
  float ballGlbScale = ballVisualRadius; // メッシュ取得に失敗した場合の保険
  float debugNativeRadius = -1.0f;
  DirectX::XMFLOAT3 debugBoundsCenter = {0.0f, 0.0f, 0.0f};
  if (auto *ballMesh = ctx.resource.GetMesh(ballMeshHandle)) {
    debugNativeRadius = ballMesh->GetBounds().Radius;
    debugBoundsCenter = ballMesh->GetBounds().Center;
    if (debugNativeRadius > 0.0001f) {
      ballGlbScale = ballVisualRadius / debugNativeRadius;
    }
  }
  t.scale = {ballGlbScale, ballGlbScale, ballGlbScale};
  LOG_INFO("WikiGolf",
           "Ball spawned at: ({}, {}, {}) scale={} nativeRadius={} "
           "boundsCenter=({}, {}, {})",
           t.position.x, t.position.y, t.position.z, ballGlbScale,
           debugNativeRadius, debugBoundsCenter.x, debugBoundsCenter.y,
           debugBoundsCenter.z);

  auto &mr = ctx.world.Add<MeshRenderer>(m_ballEntity);
  mr.mesh = ballMeshHandle;
  // ディンプルを強調した専用ピクセルシェーダーを使う（BasicPS.hlsl 共有だと
  // ロード画面の演出用ボールや建物にも強調が及んでしまうため分離している）。
  // 注意: RenderSystem.cpp 側のインスタンス化対応シェーダー一覧にも
  // "GolfBall" を登録しておく必要がある（そうしないと非インスタンス経路に
  // 落ちて実質描画されない）。
  mr.shader = ctx.resource.LoadShader("GolfBall", L"Assets/shaders/BasicVS.hlsl",
                                      L"Assets/shaders/GolfBallPS.hlsl");
  mr.color = {1.0f, 1.0f, 1.0f, 1.0f};
  mr.normalMapSRV = ctx.resource.LoadTextureSRV("Assets/models/golfball_n.png");
  mr.hasNormalMap = static_cast<bool>(mr.normalMapSRV);

  auto &rb = ctx.world.Add<RigidBody>(m_ballEntity);
  rb.isStatic = false;
  rb.mass = 0.0459f;      // 規定質量 45.9g
  rb.restitution = 0.35f; // 反発係数 (現実の芝との衝突)
  rb.drag = 0.30f;        // 空気抵抗係数 (Cd値)
  rb.rollingFriction = 0.25f; // 転がり抵抗を設定
  rb.velocity = {0, 0, 0};

  auto &c = ctx.world.Add<Collider>(m_ballEntity);
  c.type = ColliderType::Sphere;
  c.radius = game::physics::kBallRadius;

  auto *state = ctx.world.GetGlobal<GolfGameState>();
  if (state)
    state->ballEntity = m_ballEntity;
}

/**
 * @brief 指定したページへ遷移します。
*/
void WikiGolfScene::TransitionToPage(core::GameContext &ctx,
                                     const std::string &pageName) {
  LOG_INFO("WikiGolf", "Transitioning to page: {}", pageName);

  auto *state = ctx.world.GetGlobal<GolfGameState>();
  if (!state)
    return;

  auto *shot = ctx.world.GetGlobal<ShotState>();

  state->moveCount++;
  state->shotCount = 0;
  state->canShoot = true;
  m_tutorialCupInFired = false;

  if (shot) {
    shot->Reset();
  }
  if (m_hud) {
    m_hud->ResetShotUI(ctx);
  }

  // ページ移動時間進行 (2時間)
  m_timeOfDay.OnPageTransition(2.0f);

  // カメラリセット
  if (m_cameraController) {
    m_cameraController->ResetForTransition(scene_detail::kFieldScale);
  }

  // トレイルリセット（遷移時）
  if (m_gameJuice) {
    m_gameJuice->ResetTrail();
  }

  if (m_transitionController && m_pageLoader) {
    m_phase = ScenePhase::Transitioning;
    // ロード中は地球儀のみ表示するためHUD/ミニマップを非表示
    if (m_hud) m_hud->SetVisible(ctx, false);
    if (m_minimapController) m_minimapController->SetVisible(ctx, false);
    // 方向ガイドセグメントを非表示
    for (auto segE : m_guideSegments) {
      if (auto* mr = ctx.world.Get<MeshRenderer>(segE)) mr->isVisible = false;
    }
    m_transitionController->StartTransition(ctx, pageName, m_pageLoader.get(),
                                            m_ballEntity, m_cameraEntity,
                                            m_skyboxEntity,
                                            m_minimapController.get());
  } else if (m_pageLoader) {
    m_pageLoader->LoadPage(ctx, pageName, m_ballEntity, m_cameraEntity,
                           m_skyboxEntity, m_minimapController.get());
  }

  if (ctx.audio) {
    ctx.audio->PlaySE(ctx, "se_warp.mp3");
  }
}

bool WikiGolfScene::CanReturnToPreviousPage(core::GameContext &ctx) const {
  if (m_isTutorial || m_phase != ScenePhase::Playing) {
    return false;
  }

  const auto* state = ctx.world.GetGlobal<GolfGameState>();
  return state && state->pathHistory.size() >= 2;
}

bool WikiGolfScene::ReturnToPreviousPage(core::GameContext &ctx) {
  if (!CanReturnToPreviousPage(ctx)) {
    return false;
  }

  auto* state = ctx.world.GetGlobal<GolfGameState>();
  if (!state) {
    return false;
  }

  const auto previousPage =
      game::utils::ConsumePreviousPage(state->pathHistory);
  if (!previousPage.has_value()) {
    return false;
  }

  LOG_INFO("WikiGolf", "Returning to previous page: {}",
           previousPage.value());
  TransitionToPage(ctx, previousPage.value());
  return true;
}

void WikiGolfScene::OpenPauseScene(core::GameContext &ctx) {
  if (!ctx.sceneManager || m_phase != ScenePhase::Playing) {
    return;
  }

  ctx.sceneManager->PushScene(std::make_unique<PauseScene>(
      CanReturnToPreviousPage(ctx),
      [this](core::GameContext& innerCtx) { this->ReturnToPreviousPage(innerCtx); }));
}

/**
 * @brief シーンを抜ける際の後処理を行います。
*/
void WikiGolfScene::OnExit(core::GameContext &ctx) {
  if (m_pageLoader) {
    m_pageLoader->CancelAsyncPathEvaluations();
  }

  // ゲーム終了（タイトルへ戻る等）時に、ターゲット記事単位の最短経路
  // プロセス内キャッシュ（逆方向BFS到達済みノード・確定済み距離）を破棄する。
  // 次のゲームで別のターゲットに切り替わっても自動的に破棄されるが、
  // 明示的に後始末しておく。
  game::systems::WikiShortestPath::ClearProcessCaches();

  // シーン離脱時にゲーム中BGMを停止
  if (ctx.audio) {
    ctx.audio->StopBGM();
  }

  if (m_tutorialOverlay) {
    m_tutorialOverlay->Shutdown(ctx);
    m_tutorialOverlay.reset();
  }

  if (m_transitionController) {
    m_transitionController->Cleanup(ctx);
    m_transitionController.reset();
  }

  if (m_pageLoader) {
    m_pageLoader->Shutdown(ctx, m_minimapController.get());
  }

  if (m_terrainSystem) {
    m_terrainSystem->Clear(ctx);
  }

  m_slopeVisualization.Shutdown(ctx);

  // HUDは生成元でEntityを破棄し、再入場時に状態を持ち越さない。
  if (m_hud) {
    m_hud->Shutdown(ctx);
  }

  if (m_minimapController) {
    m_minimapController->Shutdown(ctx);
  }

  if (m_aimPinController) {
    m_aimPinController->Shutdown(ctx);
  }

  if (m_trajectoryPredictor) {
    m_trajectoryPredictor->Shutdown(ctx);
  }

  if (m_clubController) {
    m_clubController->Shutdown(ctx);
  }

  if (m_gameJuice) {
    m_gameJuice->Shutdown(ctx);
  }

  m_fastForwardIndicator.Shutdown();
  m_fastForwardTimer.Reset();

  ClearTutorialFlagSamples(ctx);

  m_screenFade.Shutdown(ctx);

  // 全エンティティの強制クリーンアップ（このシーンで作成されたもの以外も含む）
  std::vector<ecs::Entity> allEntities;
  ctx.world.Query<components::Transform>().Each([&](ecs::Entity e, components::Transform &) {
    allEntities.push_back(e);
  });
  // UIText や UIImage だけ持っているものも対象にする必要がある
  ctx.world.Query<components::UIText>().Each([&](ecs::Entity e, components::UIText &) {
    allEntities.push_back(e);
  });
  ctx.world.Query<components::UIImage>().Each([&](ecs::Entity e, components::UIImage &) {
    allEntities.push_back(e);
  });

  // 重複排除
  std::sort(allEntities.begin(), allEntities.end());
  allEntities.erase(std::unique(allEntities.begin(), allEntities.end()), allEntities.end());

  for (auto e : allEntities) {
    if (ctx.world.IsAlive(e)) {
      ctx.world.DestroyEntity(e);
    }
  }

  DestroyAllEntities(ctx);

  // グローバルデータから演出システムを削除（ダングリングポインタ防止）
  ctx.world.SetGlobal<game::systems::GameJuiceSystem *>(nullptr);

  LOG_INFO("WikiGolf", "Exiting WikiGolfScene (Cleaned up all entities)");
  Scene::OnExit(ctx);
}



} // namespace game::scenes
