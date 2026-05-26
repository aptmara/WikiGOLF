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

ResultScene::ResultScene(const ResultData &data) : m_data(data) {}

ResultScene::~ResultScene() = default;

/**
 * @brief シーン開始時の初期化処理を行います。
 */
void ResultScene::OnEnter(core::GameContext &ctx) {
  LOG_INFO("ResultScene", "OnEnter (Luxury) - Target: {}, Score: {}",
           m_data.targetPage, m_data.shotCount);

  m_time = 0.0f;
  m_particleTimer = 0.0f;
  m_scoreDisplayValue = 0.0f;
  m_isScoreCountFinished = false;

  m_uiElements.clear();
  m_rings.clear();
  m_particles.clear();

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
  LOG_INFO("ResultScene", "OnEnter complete.");
}

/**
 * @brief 毎フレームの更新処理を行います。
 */
void ResultScene::OnUpdate(core::GameContext &ctx) {
  m_time += ctx.dt;

  // カメラを地球儀の周囲で回転させます。
  if (ctx.world.IsAlive(m_cameraEntity)) {
    auto *camTr = ctx.world.Get<Transform>(m_cameraEntity);
    if (camTr) {
      float orbitSpeed = 0.2f;
      float radius = 8.0f + std::sin(m_time * 0.5f) * 1.0f;
      float angle = m_time * orbitSpeed;
      float height = 4.0f + std::cos(m_time * 0.3f) * 0.5f;

      camTr->position = {
          std::sin(angle) * radius, height,
          std::cos(angle) * -radius
      };

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
      t->style.fontSize =
          (elem.text == L"STAGE CLEAR" ? 90.0f : (btn ? 28.0f : 30.0f)) *
          elem.currentScale;

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

        ctx.sceneManager->ChangeScene(std::make_unique<WikiGolfScene>());
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
    ctx.sceneManager->ChangeScene(std::make_unique<WikiGolfScene>());
    return;
  }
  LOG_DEBUG("ResultScene", "OnUpdate: Finished successfully");
}

/**
 * @brief 3Dオブジェクトのビジュアル更新を行います。
 */
void ResultScene::UpdateVisuals(core::GameContext &ctx) {
  // 地球儀を自転させながら上下に揺らします。
  if (ctx.world.IsAlive(m_globeEntity)) {
    auto *t = ctx.world.Get<Transform>(m_globeEntity);
    if (t) {
      t->position.y = 2.5f + std::sin(m_time * 0.8f) * 0.2f;
      XMVECTOR q = XMQuaternionRotationRollPitchYaw(XMConvertToRadians(23.5f),
                                                    m_time * 0.5f, 0.0f);
      XMStoreFloat4(&t->rotation, q);
    }
  }

  // 装飾リングの呼吸アニメーションと回転を更新します。
  for (size_t i = 0; i < m_rings.size(); ++i) {
    auto &ring = m_rings[i];
    if (!ctx.world.IsAlive(ring.entity))
      continue;
    auto *t = ctx.world.Get<Transform>(ring.entity);
    if (!t)
      continue;

    float currentAngle = ring.phase + m_time * ring.rotationSpeed;

    // リングの回転姿勢を設定します。
    XMVECTOR q = XMQuaternionRotationRollPitchYaw(
        m_time * 0.2f * (i % 2 == 0 ? 1 : -1),
        currentAngle, m_time * 0.1f);
    XMStoreFloat4(&t->rotation, q);

    // スケールの拡大縮小を行います。
    float scaleBase = 1.0f + 0.1f * std::sin(m_time + (float)i);
    t->scale = {scaleBase, 1.0f, scaleBase};
  }

  // パーティクルのタイマー更新を行います。
  m_particleTimer += ctx.dt;
  if (m_particleTimer > 0.05f) {
    m_particleTimer = 0.0f;

    // 紙吹雪を生成します。
    {
      LOG_DEBUG("ResultScene", "UpdateVisuals: Creating confetti entity");
      auto e = CreateEntity(ctx.world);
      auto &t = ctx.world.Add<Transform>(e);
      float x = (static_cast<float>(rand() % 200) / 10.0f) - 10.0f;
      float z = (static_cast<float>(rand() % 200) / 10.0f) - 10.0f;
      t.position = {x, 15.0f, z};
      t.scale = {0.15f, 0.15f, 0.15f};

      LOG_DEBUG("ResultScene", "UpdateVisuals: Adding MeshRenderer");
      auto &mr = ctx.world.Add<MeshRenderer>(e);
      LOG_DEBUG("ResultScene", "UpdateVisuals: Loading cube mesh");
      mr.mesh = ctx.resource.LoadMesh("builtin/cube");
      LOG_DEBUG("ResultScene", "UpdateVisuals: Loading confetti shader");
      mr.shader =
          ctx.resource.LoadShader("Confetti", L"Assets/shaders/BasicVS.hlsl",
                                  L"Assets/shaders/TransitionPS.hlsl");
      LOG_DEBUG("ResultScene", "UpdateVisuals: Confetti creation done");

      // ランダムな配色を設定します。
      int type = rand() % 3;
      if (type == 0)
        mr.color = {1.0f, 0.8f, 0.2f, 1.0f};
      else if (type == 1)
        mr.color = {0.9f, 0.9f, 0.95f, 1.0f};
      else
        mr.color = {0.0f, 0.8f, 1.0f, 1.0f};
      mr.isVisible = true;

      Particle p;
      p.entity = e;
      p.isConfetti = true;
      p.lifeTime = 0.0f;
      p.maxLife = 5.0f;
      p.velocity = {0.0f, -2.0f - (float)(rand() % 10) / 10.0f, 0.0f};
      m_particles.push_back(p);
    }
  }

  // 各パーティクルを更新および寿命管理します。
  for (auto it = m_particles.begin(); it != m_particles.end();) {
    auto &p = *it;
    p.lifeTime += ctx.dt;
    if (p.lifeTime >= p.maxLife || !ctx.world.IsAlive(p.entity)) {
      ctx.world.DestroyEntity(p.entity);
      it = m_particles.erase(it);
      continue;
    }

    auto *t = ctx.world.Get<Transform>(p.entity);
    if (t) {
      t->position.x += p.velocity.x * ctx.dt;
      t->position.y += p.velocity.y * ctx.dt;
      t->position.z += p.velocity.z * ctx.dt;

      // 紙吹雪をひらひらと回転させます。
      if (p.isConfetti) {
        XMVECTOR q = XMQuaternionRotationRollPitchYaw(p.lifeTime * 2.0f,
                                                      p.lifeTime, 0.0f);
        XMStoreFloat4(&t->rotation, q);
        t->position.x += std::sin(p.lifeTime * 3.0f) * 1.0f * ctx.dt;
      }
    }
    ++it;
  }
}

/**
 * @brief 3Dのビジュアル表示環境を生成します。
 */
void ResultScene::CreateVisualEnvironment(core::GameContext &ctx) {
  auto basicShader = ctx.resource.LoadShader(
      "Basic", L"Assets/shaders/BasicVS.hlsl", L"Assets/shaders/BasicPS.hlsl");
  auto globeMesh = ctx.resource.LoadMesh(
      "Assets/models/Wikipedia_puzzle_globe_3D_render.stl");
  auto planeMesh = ctx.resource.LoadMesh("builtin/plane");
  auto ringMesh = ctx.resource.LoadMesh(
      "builtin/torus");

  // 地面のエンティティを生成します。
  m_floorEntity = CreateEntity(ctx.world);
  auto &floorTr = ctx.world.Add<Transform>(m_floorEntity);
  floorTr.position = {0.0f, -2.0f, 0.0f};
  floorTr.scale = {200.0f, 1.0f, 200.0f};
  auto &floorMr = ctx.world.Add<MeshRenderer>(m_floorEntity);
  floorMr.mesh = planeMesh;
  floorMr.shader = basicShader;
  floorMr.color = {0.1f, 0.1f, 0.2f, 0.9f};
  floorMr.isTransparent = true;
  floorMr.isVisible = true;

  // 地球儀のエンティティを生成します。
  m_globeEntity = CreateEntity(ctx.world);
  auto &globeTr = ctx.world.Add<Transform>(m_globeEntity);
  globeTr.position = {0.0f, 2.5f, 0.0f};
  globeTr.scale = {2.5f, 2.5f, 2.5f};
  auto &globeMr = ctx.world.Add<MeshRenderer>(m_globeEntity);
  globeMr.mesh = globeMesh;
  globeMr.shader = basicShader;
  globeMr.color = {1.0f, 1.0f, 1.0f, 1.0f};
  globeMr.isVisible = true;

  // 3枚の装飾リングを生成します。
  for (int i = 0; i < 3; ++i) {
    auto e = CreateEntity(ctx.world);
    auto &t = ctx.world.Add<Transform>(e);
    t.position = globeTr.position;
    float r = 4.0f + i * 1.5f;
    t.scale = {r, 0.1f, r};

    auto &mr = ctx.world.Add<MeshRenderer>(e);
    mr.mesh = ringMesh;
    mr.shader = basicShader;
    mr.color = (i == 1) ? XMFLOAT4(1.0f, 0.84f, 0.0f, 0.8f)
                        : XMFLOAT4(0.0f, 0.8f, 1.0f, 0.5f);
    mr.isTransparent = true;
    mr.isVisible = true;

    RingObject ro;
    ro.entity = e;
    ro.baseRadius = r;
    ro.phase = (float)i * 1.5f;
    ro.rotationSpeed = 0.3f + (float)i * 0.1f;
    m_rings.push_back(ro);
  }

  // カメラを生成します。
  m_cameraEntity = CreateEntity(ctx.world);
  auto &cam = ctx.world.Add<Camera>(m_cameraEntity);
  cam.fov = XMConvertToRadians(60.0f);
  cam.nearZ = 0.1f;
  cam.farZ = 300.0f;
  cam.isMainCamera = true;
  auto &camTr = ctx.world.Add<Transform>(m_cameraEntity);
  camTr.position = {0.0f, 4.0f, -15.0f};
}

/**
 * @brief 豪華な演出のUI表示要素を生成します。
 */
void ResultScene::CreateLuxuryUI(core::GameContext &ctx) {
  // スタイルパラメータを設定します。
  auto titleStyle = graphics::TextStyle::LuxuryTitle();
  auto statStyle = graphics::TextStyle::Status();
  auto btnStyle = graphics::TextStyle::LuxuryButton();

  // UI追加用のローカルヘルパー関数です。
  auto addUI = [&](const std::wstring &text, float y,
                   const graphics::TextStyle &style, bool isBtn = false,
                   const std::string &btnId = "") {
    LOG_DEBUG("ResultScene", "addUI: Adding {}", core::ToString(text));
    auto e = CreateEntity(ctx.world);

    if (isBtn) {
      auto &btn = ctx.world.Add<UIButton>(e);
      float btnW = 260.0f;
      float btnX = (text == L"Play Again (R)" ? 420.0f : 780.0f) -
                   btnW / 2.0f;
      if (btnId == "center")
        btnX = 640.0f - btnW / 2.0f;

      btn = UIButton::Create(text, btnId, btnX, y, btnW, 60.0f);
      btn.textStyle = style;
      btn.textStyle.fontSize = 26.0f;
      btn.normalColor = {0.1f, 0.1f, 0.1f, 0.8f};
      btn.hoverColor = {0.2f, 0.2f, 0.3f, 0.9f};
      btn.visible = true;
    } else {
      auto &txt = ctx.world.Add<UIText>(e);
      txt.text = text;
      txt.style = style;
      txt.x = 40.0f;
      txt.y = y;
      txt.width = 1200.0f;
      txt.visible = true;
      txt.layer = 10;
    }

    UIElement elem;
    elem.entity = e;
    elem.baseX = 0.0f;
    elem.baseY = y;
    elem.currentScale = 1.0f;
    elem.targetScale = 1.0f;
    elem.text = text;
    elem.isHovered = false;
    elem.baseColor = {1, 1, 1, 1};
    m_uiElements.push_back(elem);
    LOG_DEBUG("ResultScene", "addUI: Added {} successfully", core::ToString(text));
  };

  // ステージクリアのメインタイトルを追加します。
  addUI(L"STAGE CLEAR", 120.0f, titleStyle);

  // 打数に基づきクリア評価ランクを決定します。
  std::wstring grade = L"EXPLORER";
  DirectX::XMFLOAT4 gradeColor = {0.9f, 0.9f, 0.95f, 1.0f};
  int diff = m_data.shotCount - m_data.par;
  if (m_data.par > 0) {
    if (diff <= -2) {
      grade = L"ALBATROSS";
      gradeColor = {1.0f, 0.92f, 0.55f, 1.0f};
    } else if (diff <= -1) {
      grade = L"EAGLE";
      gradeColor = {1.0f, 0.8f, 0.8f, 1.0f};
    }
    else if (diff <= 0) {
      grade = L"BIRDIE";
      gradeColor = {0.7f, 1.0f, 0.7f, 1.0f};
    } else if (diff <= 2) {
      grade = L"PAR SAVE";
      gradeColor = {0.6f, 0.85f, 1.0f, 1.0f};
    } else if (diff <= 5) {
      grade = L"BOGEY";
      gradeColor = {1.0f, 0.85f, 0.6f, 1.0f};
    } else {
      grade = L"KEEP SWINGING";
      gradeColor = {1.0f, 0.65f, 0.65f, 1.0f};
    }
  }

  // 評価ランクのバッジUIを生成します。
  auto badgeE = CreateEntity(ctx.world);
  auto &badge = ctx.world.Add<UIText>(badgeE);
  badge.text = grade;
  badge.style = statStyle;
  badge.style.fontSize = 48.0f;
  badge.style.align = graphics::TextAlign::Center;
  badge.style.color = gradeColor;
  badge.style.hasOutline = true;
  badge.style.outlineColor = {0.0f, 0.0f, 0.0f, 1.0f};
  badge.style.outlineWidth = 3.0f;
  badge.x = 0.0f;
  badge.width = 1280.0f;
  badge.y = 220.0f;
  badge.visible = true;
  badge.layer = 11;

  UIElement bElem;
  bElem.entity = badgeE;
  bElem.baseX = 0.0f;
  bElem.baseY = 220.0f;
  bElem.currentScale = 0.0f;
  bElem.targetScale = 1.0f;
  bElem.text = grade;
  bElem.isHovered = false;
  bElem.baseColor = gradeColor;
  m_uiElements.push_back(bElem);

  // 目的地ページ名を表示します。
  auto subStyle = graphics::TextStyle::ModernBlack();
  subStyle.color = {0.8f, 0.8f, 0.9f, 1.0f};
  subStyle.hasShadow = true;
  subStyle.align = graphics::TextAlign::Center;
  addUI(L"Target: " + core::ToWString(m_data.targetPage), 290.0f, subStyle);

  // 移動経路履歴を文字列に合成します。
  std::wstring routeStr = L"Route: ";
  size_t hops = m_data.pathHistory.empty() ? 0 : m_data.pathHistory.size() - 1;
  size_t start =
      m_data.pathHistory.size() > 4 ? m_data.pathHistory.size() - 4 : 0;
  if (m_data.pathHistory.size() > 4)
    routeStr += L"... ";
  for (size_t i = start; i < m_data.pathHistory.size(); ++i) {
    if (i != start)
      routeStr += L" > ";
    routeStr += core::ToWString(m_data.pathHistory[i]);
  }

  // スコア統計値テキストを追加します。
  statStyle.align = graphics::TextAlign::Center;
  std::wstring stats = L"Shots: " + std::to_wstring(m_data.shotCount) + L"  |  Hops: " + std::to_wstring(hops);
  addUI(stats, 340.0f, statStyle);

  // 遷移経路文字列のテキストを追加します。
  auto routeStyle = statStyle;
  routeStyle.align = graphics::TextAlign::Center;
  routeStyle.fontSize = 22.0f;
  addUI(routeStr, 390.0f, routeStyle);

  // 操作ボタンを追加します。
  addUI(L"Play Again (R)", 550.0f, btnStyle, true, "retry");
  addUI(L"Title Screen", 550.0f, btnStyle, true, "title");
}

/**
 * @brief シーン終了時のクリーンアップ処理を行います。
 */
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

  for (const auto &p : m_particles) {
    if (ctx.world.IsAlive(p.entity))
      ctx.world.DestroyEntity(p.entity);
  }
  m_particles.clear();

  LOG_INFO("ResultScene", "OnExit: Cleanup complete");
}

/**
 * @brief 描画処理を行います（実描画はECSシステムが担当）。
 */
void ResultScene::Render(core::GameContext &ctx) {
  LOG_DEBUG("ResultScene", "Render: START");
  LOG_DEBUG("ResultScene", "Render: FINISHED");
}

} // namespace game::scenes
