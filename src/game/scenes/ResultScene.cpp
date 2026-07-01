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
  LOG_INFO("ResultScene", "OnEnter complete.");
}

/**
 * @brief 毎フレームの更新処理を行います。
 */
void ResultScene::OnUpdate(core::GameContext &ctx) {
  m_time += ctx.dt;

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

  // --- Firework ECS Integration ---
  m_volleyTimer -= ctx.dt;
  if (m_volleyTimer <= 0.0f) {
      m_volleyTimer = m_volleyInterval + (static_cast<float>(rand() % 200) / 100.0f - 1.0f);
      LaunchVolley();
  }

  std::vector<HanabiSpark> newTrails;

  // Update Shells
  for (auto it = m_shells.begin(); it != m_shells.end(); ) {
      auto& sh = *it;
      sh.age += ctx.dt;
      
      if (sh.phase == HanabiShell::Phase::Ascending) {
          sh.vel.y -= 9.8f * ctx.dt;
          sh.vel.x *= 1.0f - (0.5f * ctx.dt);
          sh.vel.z *= 1.0f - (0.5f * ctx.dt);
          sh.pos.x += sh.vel.x * ctx.dt;
          sh.pos.y += sh.vel.y * ctx.dt;
          sh.pos.z += sh.vel.z * ctx.dt;
          
          // Spawn shell ascending trail
          if (rand() % 2 == 0) {
              HanabiSpark trail;
              trail.pos = sh.pos;
              trail.vel = {
                  (static_cast<float>(rand() % 20) / 10.0f - 1.0f) * 0.5f,
                  -2.0f,
                  (static_cast<float>(rand() % 20) / 10.0f - 1.0f) * 0.5f
              };
              trail.color = { 2.5f, 1.8f, 0.5f, 1.0f }; // Glowing gold
              trail.age = 0.0f;
              trail.lifeTime = 0.4f + (static_cast<float>(rand() % 20) / 100.0f);
              trail.size = 0.15f + (static_cast<float>(rand() % 10) / 100.0f);
              trail.drag = 0.95f;
              
              trail.entity = CreateEntity(ctx.world);
              auto& t = ctx.world.Add<Transform>(trail.entity);
              t.position = trail.pos;
              t.scale = { trail.size, trail.size, trail.size };
              
              auto& mr = ctx.world.Add<MeshRenderer>(trail.entity);
              mr.mesh = ctx.resource.LoadMesh("builtin/sphere");
              mr.shader = ctx.resource.LoadShader("Basic", L"Assets/shaders/BasicVS.hlsl", L"Assets/shaders/UnlitPS.hlsl");
              mr.color = trail.color;
              mr.isTransparent = true;
              mr.blendMode = components::BlendMode::Add;
              mr.isVisible = true;

              newTrails.push_back(trail);
          }

          if (sh.vel.y < 0.0f) {
              sh.phase = HanabiShell::Phase::FlashFrame;
              sh.age = 0.0f;
              sh.flashRadius = 1.0f;
              // Camera shake on burst!
              m_cameraShake = 1.0f;

              // Spawn ECS sparks (Organic Flash Core)
              int coreSparks = 40 + (rand() % 20);
              for (int i = 0; i < coreSparks; ++i) {
                  float theta = (static_cast<float>(rand() % 628) / 100.0f);
                  float phi = acosf((static_cast<float>(rand() % 200) / 100.0f) - 1.0f);
                  float spd = 2.0f + (static_cast<float>(rand() % 130) / 10.0f); // 2~15
                  
                  HanabiSpark sp;
                  sp.pos = sh.pos;
                  sp.vel = {
                      sinf(phi) * cosf(theta) * spd,
                      sinf(phi) * sinf(theta) * spd,
                      cosf(phi) * spd
                  };
                  sp.color = { 3.0f, 3.0f, 2.5f, 1.0f }; // HDR Bloom overdrive
                  sp.age = 0.0f;
                  sp.lifeTime = 0.15f + (static_cast<float>(rand() % 10) / 100.0f);
                  sp.size = 0.4f + (static_cast<float>(rand() % 30) / 100.0f);
                  sp.drag = 0.85f;
                  
                  // ECS Entity
                  sp.entity = CreateEntity(ctx.world);
                  auto& t = ctx.world.Add<Transform>(sp.entity);
                  t.position = sp.pos;
                  t.scale = { sp.size, sp.size, sp.size };
                  
                  auto& mr = ctx.world.Add<MeshRenderer>(sp.entity);
                  mr.mesh = ctx.resource.LoadMesh("builtin/sphere");
                  mr.shader = ctx.resource.LoadShader("Basic", L"Assets/shaders/BasicVS.hlsl", L"Assets/shaders/UnlitPS.hlsl");
                  mr.color = sp.color;
                  mr.isTransparent = true;
                  mr.blendMode = components::BlendMode::Add;
                  mr.isVisible = true;

                  m_sparks.push_back(sp);
              }
              
              // Outer Sparks
              int outerSparks = 180 + (rand() % 80);
              DirectX::XMFLOAT4 shellColor;
              int type = rand() % 5;
              if(type == 0) shellColor = {3.0f, 0.5f, 0.5f, 1.0f};
              else if(type == 1) shellColor = {0.5f, 3.0f, 0.5f, 1.0f};
              else if(type == 2) shellColor = {0.5f, 0.5f, 3.0f, 1.0f};
              else if(type == 3) shellColor = {3.0f, 2.0f, 0.5f, 1.0f};
              else shellColor = {0.5f, 3.0f, 3.0f, 1.0f};

              for (int i = 0; i < outerSparks; ++i) {
                  float theta = (static_cast<float>(rand() % 628) / 100.0f);
                  float phi = acosf((static_cast<float>(rand() % 200) / 100.0f) - 1.0f);
                  float spd = 4.0f + (static_cast<float>(rand() % 200) / 10.0f); // 4~24
                  
                  HanabiSpark sp;
                  sp.pos = sh.pos;
                  sp.vel = {
                      sinf(phi) * cosf(theta) * spd,
                      sinf(phi) * sinf(theta) * spd,
                      cosf(phi) * spd
                  };
                  sp.color = shellColor;
                  sp.age = 0.0f;
                  sp.lifeTime = 1.5f + (static_cast<float>(rand() % 100) / 100.0f);
                  sp.size = 0.12f + (static_cast<float>(rand() % 12) / 100.0f);
                  sp.drag = 0.96f;
                  
                  // ECS Entity
                  sp.entity = CreateEntity(ctx.world);
                  auto& t = ctx.world.Add<Transform>(sp.entity);
                  t.position = sp.pos;
                  t.scale = { sp.size, sp.size, sp.size };
                  
                  auto& mr = ctx.world.Add<MeshRenderer>(sp.entity);
                  mr.mesh = ctx.resource.LoadMesh("builtin/sphere");
                  mr.shader = ctx.resource.LoadShader("Basic", L"Assets/shaders/BasicVS.hlsl", L"Assets/shaders/UnlitPS.hlsl");
                  mr.color = sp.color;
                  mr.isTransparent = true;
                  mr.blendMode = components::BlendMode::Add;
                  mr.isVisible = true;

                  m_sparks.push_back(sp);
              }
          }
          ++it;
      } else if (sh.phase == HanabiShell::Phase::FlashFrame) {
          sh.phase = HanabiShell::Phase::Burst;
          ++it;
      } else if (sh.phase == HanabiShell::Phase::Burst) {
          sh.phase = HanabiShell::Phase::Fading;
          sh.age = 0.0f;
          sh.lifeTime = 3.0f; // Track when to remove shell object
          ++it;
      } else if (sh.phase == HanabiShell::Phase::Fading) {
          if (sh.age >= sh.lifeTime) {
              it = m_shells.erase(it);
          } else {
              ++it;
          }
      }
  }

  // Update Sparks & spawn active trails
  for (auto it = m_sparks.begin(); it != m_sparks.end(); ) {
      auto& sp = *it;
      sp.age += ctx.dt;
      
      if (sp.age >= sp.lifeTime || !ctx.world.IsAlive(sp.entity)) {
          if(ctx.world.IsAlive(sp.entity)) ctx.world.DestroyEntity(sp.entity);
          it = m_sparks.erase(it);
          continue;
      }
      
      // Spawn trail particle behind the spark if it's moving fast
      if (sp.drag < 0.99f && sp.age < sp.lifeTime * 0.7f && (rand() % 4 == 0)) {
          HanabiSpark trail;
          trail.pos = sp.pos;
          trail.vel = {
              sp.vel.x * 0.1f,
              sp.vel.y * 0.1f,
              sp.vel.z * 0.1f
          };
          trail.color = sp.color;
          trail.color.w = sp.color.w * 0.6f;
          trail.age = 0.0f;
          trail.lifeTime = 0.15f + (static_cast<float>(rand() % 10) / 100.0f);
          trail.size = sp.size * 0.6f;
          trail.drag = 0.99f; // static trail, slowly fading
          
          trail.entity = CreateEntity(ctx.world);
          auto& t = ctx.world.Add<Transform>(trail.entity);
          t.position = trail.pos;
          t.scale = { trail.size, trail.size, trail.size };
          
          auto& mr = ctx.world.Add<MeshRenderer>(trail.entity);
          mr.mesh = ctx.resource.LoadMesh("builtin/sphere");
          mr.shader = ctx.resource.LoadShader("Basic", L"Assets/shaders/BasicVS.hlsl", L"Assets/shaders/UnlitPS.hlsl");
          mr.color = trail.color;
          mr.isTransparent = true;
          mr.blendMode = components::BlendMode::Add;
          mr.isVisible = true;

          newTrails.push_back(trail);
      }

      sp.vel.y -= 9.8f * ctx.dt * 0.5f; // reduced gravity for sparks
      sp.vel.x *= (1.0f - (1.0f - sp.drag) * ctx.dt * 60.0f);
      sp.vel.y *= (1.0f - (1.0f - sp.drag) * ctx.dt * 60.0f);
      sp.vel.z *= (1.0f - (1.0f - sp.drag) * ctx.dt * 60.0f);
      
      sp.pos.x += sp.vel.x * ctx.dt;
      sp.pos.y += sp.vel.y * ctx.dt;
      sp.pos.z += sp.vel.z * ctx.dt;

      // Sync with ECS
      auto* t = ctx.world.Get<Transform>(sp.entity);
      if (t) {
          t->position = sp.pos;
      }
      
      auto* mr = ctx.world.Get<MeshRenderer>(sp.entity);
      if (mr) {
          float fade = 1.0f - (sp.age / sp.lifeTime);
          mr->color.w = fade; // Fade alpha
      }
      
      ++it;
  }

  // Insert new trails into m_sparks vector
  if (!newTrails.empty()) {
      m_sparks.insert(m_sparks.end(), newTrails.begin(), newTrails.end());
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

