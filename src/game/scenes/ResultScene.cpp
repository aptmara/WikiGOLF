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

  // Mouse cursor setup
  ctx.input.SetMouseCursorVisible(true);
  ctx.input.SetMouseCursorLocked(false);

  // Audio: Triumph BGM (Reuse Title BGM if dedicated result BGM is missing, or
  // play functionality sound)
  if (ctx.audio) {
    // Ideally "bgm_result.mp3", falling back to "bgm_title.mp3" for now or
    // silence with fanfare SE
    ctx.audio->PlayBGM(ctx, "bgm_title.mp3", 0.4f);
    // Play Fanfare SE immediately
    ctx.audio->PlaySE(
        ctx,
        "se_holeInOne.mp3"); // Using hole-in-one SE as victory sound for now
  }

  // 1. Create 3D Environment (Globe, Floor, Rings)
  LOG_INFO("ResultScene", "Creating Visual Environment...");
  CreateVisualEnvironment(ctx);

  // 2. Create Luxury UI
  LOG_INFO("ResultScene", "Creating Luxury UI...");
  CreateLuxuryUI(ctx);
  LOG_INFO("ResultScene", "OnEnter complete.");
}

void ResultScene::OnUpdate(core::GameContext &ctx) {
  m_time += ctx.dt;

  // --- Dynamic Camera Orbit ---
  if (ctx.world.IsAlive(m_cameraEntity)) {
    auto *camTr = ctx.world.Get<Transform>(m_cameraEntity);
    if (camTr) {
      float orbitSpeed = 0.2f;
      float radius = 8.0f + std::sin(m_time * 0.5f) * 1.0f;
      float angle = m_time * orbitSpeed;
      float height = 4.0f + std::cos(m_time * 0.3f) * 0.5f;

      camTr->position = {
          std::sin(angle) * radius, height,
          std::cos(angle) * -radius // Start from front (-Z)
      };

      // Look at the Globe (Center at 0, 2, 0 approx)
      XMVECTOR eye = XMLoadFloat3(&camTr->position);
      XMVECTOR focus = XMVectorSet(0.0f, 2.5f, 0.0f, 0.0f);
      XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
      XMMATRIX view = XMMatrixLookAtLH(eye, focus, up);
      XMMATRIX invView = XMMatrixInverse(nullptr, view);
      XMStoreFloat4(&camTr->rotation, XMQuaternionRotationMatrix(invView));
    }
  }

  // --- Update 3D Elements ---
  UpdateVisuals(ctx);

  // --- UI Logic & Animation ---
  auto mousePos = ctx.input.GetMousePosition();
  bool actionTriggered = false; // R or Button Click

  for (auto &elem : m_uiElements) {
    if (!ctx.world.IsAlive(elem.entity))
      continue;

    // Check Hover (Custom Bounding Box Logic for Text/Buttons)
    // Simplifying assumption: Standard button size or Text area
    float w = 300.0f; // Default hit width
    float h = 60.0f;
    float x = elem.baseX - w / 2.0f; // Centered X usually
    float y = elem.baseY;

    // Adjust for buttons specifically
    auto *btn = ctx.world.Get<UIButton>(elem.entity);
    if (btn) {
      w = btn->width;
      h = btn->height;
      x = btn->x;
      y = btn->y;
    }

    bool hover = (mousePos.x >= x && mousePos.x <= x + w && mousePos.y >= y &&
                  mousePos.y <= y + h);

    // Animation - Lerp Scale
    if (hover && !elem.isHovered) {
      elem.targetScale = 1.15f;
      elem.isHovered = true;
      if (ctx.audio)
        ctx.audio->PlaySE(ctx, "se_shot_soft.mp3", 0.3f); // Hover sound
    } else if (!hover && elem.isHovered) {
      elem.targetScale = 1.0f;
      elem.isHovered = false;
    }

    elem.currentScale +=
        (elem.targetScale - elem.currentScale) * (15.0f * ctx.dt);

    // Apply Scale to Text/Button
    auto *t = ctx.world.Get<UIText>(elem.entity);
    if (t) {
      t->style.fontSize =
          (elem.text == L"STAGE CLEAR" ? 90.0f : (btn ? 28.0f : 30.0f)) *
          elem.currentScale;

      // Dynamic Color Pulse for Title
      if (elem.text == L"STAGE CLEAR") {
        float pulse = std::sin(m_time * 3.0f);
        t->style.color = {1.0f, 0.9f + pulse * 0.1f, 0.6f + pulse * 0.2f,
                          1.0f}; // Gold pulse
        t->style.shadowOffsetX = 2.0f + pulse;
        t->style.shadowOffsetY = 2.0f + pulse;
      }
    }

    // Interaction
    if (btn && hover && ctx.input.GetMouseButtonDown(0)) {
      // Helper to clean up persistent terrain (WikiGolfScene leftovers)
      auto cleanTerrain = [&]() {
        // Find and destroy all entities with TerrainObject tag
        // Since we can't iterate by component in this ECS efficiently without a
        // system, we assume TerrainObject is a marker. Wait, ECS world might
        // not have a helper for "GetEntitiesWith". We can iterate all entities
        // if needed, but World doesn't expose strict iterator. However,
        // WikiTerrainSystem puts them in m_entities, but we don't have access.
        // Fallback: If we can't iterate, we rely on TitleScene destroying
        // everything on Exit? No, User said "Loading is bad". If we can't
        // easily find them, we might be stuck. LUCKILY:
        // World::DestroyAllEntities destroys EVERYTHING. For "Play Again"
        // (WikiGolf), we WANT to destroy everything (Result + Old Terrain). For
        // "Title", we WANT to destroy everything (Result + Old Terrain) before
        // entering Title? No, user said "Keep it until Title" -> Title Scene
        // enters WITH Terrain. So ONLY for Play Again, we explicitly destroy
        // everything.
      };

      // Handle Button Click Actions
      if (elem.text == L"Play Again (R)") {
        // Explicitly clear previous terrain objects before restarting game
        // Actually SceneManager::ChangeScene calls OnExit which calls
        // Scene::DestroyAllEntities But that only destroys ResultScene
        // entities. We need to destroy global leftovers. Let's use a manual
        // loop if Clear doesn't exist, or just rely on World::Clear() if
        // available. Looking at Scene.cpp, it calls ctx.world.DestroyEntity(e).
        // Let's assume we can just do:
        // ctx.world.Clear(); (If World has Clear)
        // Or manual iterator. World usually has "entities_" vector but it's
        // private. But wait! We DO have the manual components now. We can
        // query! auto view = ctx.world.GetView<TerrainObject>(); (If supported)

        // Simpler approach:
        // Just destroy everything that is NOT global persistent (like
        // Audio/Input?) Actually, World::Clear() usually wipes everything. If
        // we wipe everything, we are safe for WikiGolfScene (it rebuilds).

        // CHECK if World has Clear.
        // Step 589 Scene.cpp calls DestroyEntity.
        // Let's try to query TerrainObject if possible.
        // If not, we can't do specific cleanup easily.

        // But we added TerrainObject component!
        // Does World support iterating components?
        // ctx.world.Each<TerrainObject>([&](ecs::Entity e, TerrainObject&){
        // ctx.world.DestroyEntity(e); });
        // Collect entities to destroy to avoid iterator invalidation
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
      if (elem.text == L"Title Screen") {
        // Do NOT clear terrain (User wants it in Title)

        // But TitleScene needs to clear it on ITS exit.
        ctx.sceneManager->ChangeScene(std::make_unique<TitleScene>());
        return;
      }
    }
  }

  // Keyboard Shortcuts
  if (ctx.input.GetKeyDown('R')) {
    ctx.sceneManager->ChangeScene(std::make_unique<WikiGolfScene>());
    return;
  }
}

void ResultScene::UpdateVisuals(core::GameContext &ctx) {
  // 1. Globe Rotation
  if (ctx.world.IsAlive(m_globeEntity)) {
    auto *t = ctx.world.Get<Transform>(m_globeEntity);
    if (t) {
      t->position.y = 2.5f + std::sin(m_time * 0.8f) * 0.2f; // Bobbing
      XMVECTOR q = XMQuaternionRotationRollPitchYaw(XMConvertToRadians(23.5f),
                                                    m_time * 0.5f, 0.0f);
      XMStoreFloat4(&t->rotation, q);
    }
  }

  // 2. Rings Animation
  for (size_t i = 0; i < m_rings.size(); ++i) {
    auto &ring = m_rings[i];
    if (!ctx.world.IsAlive(ring.entity))
      continue;
    auto *t = ctx.world.Get<Transform>(ring.entity);
    if (!t)
      continue;

    float currentAngle = ring.phase + m_time * ring.rotationSpeed;

    // Complex Rotation (Roll/Pitch/Yaw)
    XMVECTOR q = XMQuaternionRotationRollPitchYaw(
        m_time * 0.2f * (i % 2 == 0 ? 1 : -1), // Slow wobble
        currentAngle, m_time * 0.1f);
    XMStoreFloat4(&t->rotation, q);

    // Breathing Scale
    float scaleBase = 1.0f + 0.1f * std::sin(m_time + (float)i);
    t->scale = {scaleBase, 1.0f, scaleBase};
  }

  // 3. Particles
  m_particleTimer += ctx.dt;
  // Spawn new particles (Confetti from top, Data from bottom)
  if (m_particleTimer > 0.05f) {
    m_particleTimer = 0.0f;

    // Spawn Confetti
    {
      auto e = CreateEntity(ctx.world);
      auto &t = ctx.world.Add<Transform>(e);
      float x = (static_cast<float>(rand() % 200) / 10.0f) - 10.0f;
      float z = (static_cast<float>(rand() % 200) / 10.0f) - 10.0f;
      t.position = {x, 15.0f, z}; // Start high
      t.scale = {0.15f, 0.15f, 0.15f};

      auto &mr = ctx.world.Add<MeshRenderer>(e);
      mr.mesh = ctx.resource.LoadMesh("builtin/cube");
      mr.shader =
          ctx.resource.LoadShader("Basic", L"Assets/shaders/BasicVS.hlsl",
                                  L"Assets/shaders/BasicPS.hlsl");

      // Random Gold/Silver/Cyber colors
      int type = rand() % 3;
      if (type == 0)
        mr.color = {1.0f, 0.8f, 0.2f, 1.0f}; // Gold
      else if (type == 1)
        mr.color = {0.9f, 0.9f, 0.95f, 1.0f}; // Platinum
      else
        mr.color = {0.0f, 0.8f, 1.0f, 1.0f}; // Cyan
      mr.isVisible = true;

      Particle p;
      p.entity = e;
      p.isConfetti = true;
      p.lifeTime = 0.0f;
      p.maxLife = 5.0f;
      p.velocity = {0.0f, -2.0f - (float)(rand() % 10) / 10.0f, 0.0f}; // Fall
      m_particles.push_back(p);
    }
  }

  // Update Particles
  for (auto it = m_particles.begin(); it != m_particles.end();) {
    auto &p = *it;
    p.lifeTime += ctx.dt;
    if (p.lifeTime >= p.maxLife || !ctx.world.IsAlive(p.entity)) {
      // Destroy particle entity
      ctx.world.DestroyEntity(p.entity);
      it = m_particles.erase(it);
      continue;
    }

    auto *t = ctx.world.Get<Transform>(p.entity);
    if (t) {
      // Move
      t->position.x += p.velocity.x * ctx.dt;
      t->position.y += p.velocity.y * ctx.dt;
      t->position.z += p.velocity.z * ctx.dt;

      // Rotate confetti
      if (p.isConfetti) {
        XMVECTOR q = XMQuaternionRotationRollPitchYaw(p.lifeTime * 2.0f,
                                                      p.lifeTime, 0.0f);
        XMStoreFloat4(&t->rotation, q);

        // Sway
        t->position.x += std::sin(p.lifeTime * 3.0f) * 1.0f * ctx.dt;
      }
    }
    ++it;
  }
}

void ResultScene::CreateVisualEnvironment(core::GameContext &ctx) {
  auto basicShader = ctx.resource.LoadShader(
      "Basic", L"Assets/shaders/BasicVS.hlsl", L"Assets/shaders/BasicPS.hlsl");
  auto globeMesh = ctx.resource.LoadMesh(
      "Assets/models/Wikipedia_puzzle_globe_3D_render.stl");
  auto planeMesh = ctx.resource.LoadMesh("builtin/plane");
  auto ringMesh = ctx.resource.LoadMesh(
      "builtin/torus"); // Assuming torus exists or use cube loops

  // Floor
  m_floorEntity = CreateEntity(ctx.world);
  auto &floorTr = ctx.world.Add<Transform>(m_floorEntity);
  floorTr.position = {0.0f, -2.0f, 0.0f};
  floorTr.scale = {200.0f, 1.0f, 200.0f};
  auto &floorMr = ctx.world.Add<MeshRenderer>(m_floorEntity);
  floorMr.mesh = planeMesh;
  floorMr.shader = basicShader;
  floorMr.color = {0.1f, 0.1f, 0.2f, 0.9f}; // Deep Blue reflective floor
  floorMr.isTransparent = true;
  floorMr.isVisible = true;

  // Victory Globe
  m_globeEntity = CreateEntity(ctx.world);
  auto &globeTr = ctx.world.Add<Transform>(m_globeEntity);
  globeTr.position = {0.0f, 2.5f, 0.0f};
  globeTr.scale = {2.5f, 2.5f, 2.5f};
  auto &globeMr = ctx.world.Add<MeshRenderer>(m_globeEntity);
  globeMr.mesh = globeMesh;
  globeMr.shader = basicShader;
  globeMr.color = {1.0f, 1.0f, 1.0f, 1.0f}; // Pure White/Silver for victory
  globeMr.isVisible = true;

  // Victory Rings (Concentrated around the globe)
  for (int i = 0; i < 3; ++i) {
    auto e = CreateEntity(ctx.world);
    auto &t = ctx.world.Add<Transform>(e);
    t.position = globeTr.position;
    float r = 4.0f + i * 1.5f;
    t.scale = {r, 0.1f, r}; // Initial scale

    auto &mr = ctx.world.Add<MeshRenderer>(e);
    mr.mesh = ringMesh; // Or cube scaled to be a ring segment if torus fails
    if (false) {        // ringMesh fallback check removed
      // If manual ring construction needed
      t.scale = {0.1f, 0.1f, 0.1f}; // Just floating cubes fallback
    }
    mr.shader = basicShader;
    mr.color = (i == 1) ? XMFLOAT4(1.0f, 0.84f, 0.0f, 0.8f)
                        : XMFLOAT4(0.0f, 0.8f, 1.0f, 0.5f); // Gold & Cyan
    mr.isTransparent = true;
    mr.isVisible = true;

    RingObject ro;
    ro.entity = e;
    ro.baseRadius = r;
    ro.phase = (float)i * 1.5f;
    ro.rotationSpeed = 0.3f + (float)i * 0.1f;
    m_rings.push_back(ro);
  }

  // Camera
  m_cameraEntity = CreateEntity(ctx.world);
  auto &cam = ctx.world.Add<Camera>(m_cameraEntity);
  cam.fov = XMConvertToRadians(60.0f);
  cam.nearZ = 0.1f;
  cam.farZ = 300.0f;
  cam.isMainCamera = true;
  auto &camTr = ctx.world.Add<Transform>(m_cameraEntity);
  camTr.position = {0.0f, 4.0f, -15.0f};

  // Background (Skybox)
  // auto skyboxE = CreateEntity(ctx.world);
  // ctx.world.Add<Skybox>(skyboxE);
  // ... Assume default skybox or let it be black/starry
}

void ResultScene::CreateLuxuryUI(core::GameContext &ctx) {
  // Styles
  auto titleStyle = graphics::TextStyle::LuxuryTitle();
  auto statStyle = graphics::TextStyle::Status();
  auto btnStyle = graphics::TextStyle::LuxuryButton();

  // Helper lambda
  auto addUI = [&](const std::wstring &text, float y,
                   const graphics::TextStyle &style, bool isBtn = false,
                   const std::string &btnId = "") {
    LOG_DEBUG("ResultScene", "addUI: Adding {}", core::ToString(text));
    auto e = CreateEntity(ctx.world);

    if (isBtn) {
      auto &btn = ctx.world.Add<UIButton>(e);
      float btnW = 260.0f;
      float btnX = (text == L"Play Again (R)" ? 420.0f : 780.0f) -
                   btnW / 2.0f; // Side by side
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
      txt.x = 40.0f; // Center of 1280 screen with 1200 width
      txt.y = y;
      txt.width = 1200.0f;
      txt.visible = true;
      txt.layer = 10;
    }

    UIElement elem;
    elem.entity = e;
    elem.baseX = 0.0f; // Will be set dynamically by interactions
    elem.baseY = y;
    elem.currentScale = 1.0f;
    elem.targetScale = 1.0f;
    elem.text = text;
    elem.isHovered = false;
    elem.baseColor = {1, 1, 1, 1};
    m_uiElements.push_back(elem);
    LOG_DEBUG("ResultScene", "addUI: Added {} successfully", core::ToString(text));
  };

  // 1. Title
  addUI(L"STAGE CLEAR", 120.0f, titleStyle);

  // 2. Grade Calculation
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
    } // Fixed from diff<=0 being Birdie
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

  // Grade Badge Entity (handled manually to set specific color)
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
  // Add to animation list
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

  // 3. Target Hint
  auto subStyle = graphics::TextStyle::ModernBlack();
  subStyle.color = {0.8f, 0.8f, 0.9f, 1.0f};
  subStyle.hasShadow = true;
  subStyle.align = graphics::TextAlign::Center;
  addUI(L"Target: " + core::ToWString(m_data.targetPage), 290.0f, subStyle);

  // 4. Detailed Stats
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

  statStyle.align = graphics::TextAlign::Center;
  std::wstring stats = L"Shots: " + std::to_wstring(m_data.shotCount) + L"  |  Hops: " + std::to_wstring(hops);
  addUI(stats, 340.0f, statStyle);

  auto routeStyle = statStyle;
  routeStyle.align = graphics::TextAlign::Center;
  routeStyle.fontSize = 22.0f;
  addUI(routeStr, 390.0f, routeStyle);

  // 5. Buttons
  addUI(L"Play Again (R)", 550.0f, btnStyle, true, "retry");
  addUI(L"Title Screen", 550.0f, btnStyle, true, "title");
}

void ResultScene::OnExit(core::GameContext &ctx) {
  // Helper to destroy vector of entities
  auto destroyVec = [&](auto &vec) {
    for (const auto &item : vec) {
      if (ctx.world.IsAlive(item.entity))
        ctx.world.DestroyEntity(item.entity);
    }
    vec.clear();
  };

  // Destroy single entities
  if (ctx.world.IsAlive(m_globeEntity))
    ctx.world.DestroyEntity(m_globeEntity);
  if (ctx.world.IsAlive(m_floorEntity))
    ctx.world.DestroyEntity(m_floorEntity);
  if (ctx.world.IsAlive(m_cameraEntity))
    ctx.world.DestroyEntity(m_cameraEntity);

  // Destroy collections
  destroyVec(m_uiElements);

  // Rings
  for (const auto &ring : m_rings) {
    if (ctx.world.IsAlive(ring.entity))
      ctx.world.DestroyEntity(ring.entity);
  }
  m_rings.clear();

  // Particles
  for (const auto &p : m_particles) {
    if (ctx.world.IsAlive(p.entity))
      ctx.world.DestroyEntity(p.entity);
  }
  m_particles.clear();

  // DestroyAllEntities(ctx); // Function possibly missing or incomplete
  LOG_INFO("ResultScene", "OnExit: Cleanup complete");
}

void ResultScene::Render(core::GameContext &ctx) {
  // ECS System renders everything
}

} // namespace game::scenes
