#include "TitleScene.h"
#include "../../audio/AudioSystem.h"
#include "../../core/GameContext.h"
#include "../../core/Input.h"
#include "../../core/SceneManager.h"
#include "../../core/StringUtils.h"
#include "../../graphics/GraphicsDevice.h"
#include "../../graphics/SkyboxTextureGenerator.h"
#include "../components/Camera.h"
#include "../components/MeshRenderer.h"
#include "../components/PhysicsComponents.h"
#include "../components/Skybox.h"
#include "../components/Transform.h"
#include "../components/UIButton.h"
#include "../components/UIImage.h"
#include "../components/UIText.h"
#include "../systems/SkyboxRenderSystem.h"
#include "LoadingScene.h"
#include "WikiGolfScene.h"
#include <DirectXMath.h>
#include <cmath>
#include <random>

namespace game::scenes {

using namespace DirectX;

void TitleScene::OnEnter(core::GameContext &ctx) {
  LOG_INFO("TitleScene", "OnEnter (Luxury Mode)");

  m_time = 0.0f;
  m_uiElements.clear();
  m_particles.clear();
  m_clubs.clear();
  m_ringObjects.clear();
  m_reflections.clear();

  // マウスカーソル表示
  ctx.input.SetMouseCursorVisible(true);
  ctx.input.SetMouseCursorLocked(false);

  // BGM再生
  if (ctx.audio) {
    ctx.audio->PlayBGM(ctx, "bgm_title.mp3", 0.5f);
  }

  // --- リソースロード ---
  auto basicShader = ctx.resource.LoadShader(
      "Basic", L"Assets/shaders/BasicVS.hlsl", L"Assets/shaders/BasicPS.hlsl");
  auto ballMesh = ctx.resource.LoadMesh("Assets/models/golfball.fbx");
  auto clubMesh = ctx.resource.LoadMesh("Assets/models/golf_club.fbx");
  auto cubeMesh = ctx.resource.LoadMesh("builtin/cube");
  auto sphereMesh = ctx.resource.LoadMesh("builtin/sphere");
  auto planeMesh = ctx.resource.LoadMesh("builtin/plane");
  LOG_INFO("TitleScene", "Resources loaded (Basic shader, meshes)");

  // --- 1. 床 (鏡面ブラック) ---
  m_floorEntity = CreateEntity(ctx.world);
  auto &floorTr = ctx.world.Add<components::Transform>(m_floorEntity);
  floorTr.position = {0.0f, 0.0f, 0.0f};
  floorTr.scale = {200.0f, 1.0f, 200.0f};
  auto &floorMr = ctx.world.Add<components::MeshRenderer>(m_floorEntity);
  floorMr.mesh = planeMesh;
  floorMr.shader = basicShader;
  // 漆黒だがわずかに青みがある高級石材風。半透明にして下の反射を見せる
  floorMr.color = {0.05f, 0.05f, 0.08f, 0.85f};
  floorMr.isVisible = true;
  LOG_INFO("TitleScene", "Floor created");

  // --- 2. 浮遊ボール (神器) ---
  m_ballEntity = CreateEntity(ctx.world);
  auto &ballTr = ctx.world.Add<components::Transform>(m_ballEntity);
  ballTr.position = {0.0f, 1.8f, 0.0f}; // 空中に浮いている
  ballTr.scale = {0.12f, 0.12f, 0.12f}; // 少し大きめ
  auto &ballMr = ctx.world.Add<components::MeshRenderer>(m_ballEntity);
  ballMr.mesh = ballMesh;
  ballMr.shader = basicShader;
  ballMr.color = {2.0f, 2.0f, 2.0f, 1.0f}; // 強烈な輝き
  ballMr.isVisible = true;
  ballMr.normalMapSRV =
      ctx.resource.LoadTextureSRV("Assets/models/golfball_n.png");
  ballMr.hasNormalMap = static_cast<bool>(ballMr.normalMapSRV);
  LOG_INFO("TitleScene", "Floating ball created (normal map set={})",
           ballMr.hasNormalMap);

  LOG_INFO("TitleScene", "Creating ball reflection...");
  // ボールの反射 (Y反転)
  {
    auto e = CreateEntity(ctx.world);
    auto &t = ctx.world.Add<components::Transform>(e);
    t.position = {0.0f, -1.8f, 0.0f};
    t.scale = {0.12f, -0.12f, 0.12f}; // Y反転スケール
    auto &mr = ctx.world.Add<components::MeshRenderer>(e);
    mr.mesh = ballMesh;
    mr.shader = basicShader;
    mr.color = {0.5f, 0.5f, 0.5f, 1.0f}; // 少し暗く
    mr.isVisible = true;
    // 反射はノーマルマップを使わない（原因切り分け用）
    mr.normalMapSRV.Reset();
    mr.hasNormalMap = false;
    m_reflections.push_back(e);
  }
  LOG_INFO("TitleScene", "Ball reflection created");

  LOG_INFO("TitleScene", "Creating clubs...");
  // --- 3. 衛星クラブ (守護者) ---
  for (int i = 0; i < 3; ++i) {
    auto e = CreateEntity(ctx.world);
    auto &t = ctx.world.Add<components::Transform>(e);
    // 位置はUpdateで動かす
    float angle = (float)i * (DirectX::XM_2PI / 3.0f);
    float r = 1.0f;
    t.position = {std::cos(angle) * r, 1.8f, std::sin(angle) * r};
    t.scale = {0.8f, 0.8f, 0.8f};

    auto &mr = ctx.world.Add<components::MeshRenderer>(e);
    mr.mesh = clubMesh;
    mr.shader = basicShader;
    mr.color = {1.0f, 0.8f, 0.2f, 1.0f}; // ゴールドクラブ
    mr.isVisible = true;
    m_clubs.push_back(e);

    // 反射
    auto re = CreateEntity(ctx.world);
    auto &rt = ctx.world.Add<components::Transform>(re);
    rt.position = t.position;
    rt.position.y *= -1.0f;
    rt.scale = {0.8f, -0.8f, 0.8f};

    auto &rmr = ctx.world.Add<components::MeshRenderer>(re);
    rmr.mesh = clubMesh;
    rmr.shader = basicShader;
    rmr.color = {0.4f, 0.3f, 0.1f, 1.0f}; // 暗いゴールド
    rmr.isVisible = true;
    m_reflections.push_back(re);
  }
  LOG_INFO("TitleScene", "Clubs and reflections created");

  // --- 4. 背景リング (神秘的構造体) ---
  int ringCount = 24;
  for (int i = 0; i < ringCount; ++i) {
    auto e = CreateEntity(ctx.world);
    auto &t = ctx.world.Add<components::Transform>(e);
    float angle = (float)i * (DirectX::XM_2PI / ringCount);
    float r = 12.0f;
    t.position = {std::cos(angle) * r, 6.0f + std::sin(angle * 2.0f) * 2.0f,
                  std::sin(angle) * r};
    t.scale = {0.5f, 4.0f, 0.5f}; // 縦長の柱
    // 中心を向く回転
    XMVECTOR q = XMQuaternionRotationRollPitchYaw(0.0f, -angle, 0.0f);
    XMStoreFloat4(&t.rotation, q);

    auto &mr = ctx.world.Add<components::MeshRenderer>(e);
    mr.mesh = cubeMesh;
    mr.shader = basicShader;
    // 周期的に色を変える
    float hue = (float)i / ringCount;
    if (i % 2 == 0)
      mr.color = {0.1f, 0.1f, 0.3f, 1.0f}; // 濃紺
    else
      mr.color = {0.3f, 0.3f, 0.3f, 1.0f}; // ダークシルバー
    mr.isVisible = true;
    m_ringObjects.push_back(e);
  }
  LOG_INFO("TitleScene", "Rings created ({})", ringCount);

  // --- 4.5. Wikipedia地球儀 (シンボル) ---
  m_globeEntity = CreateEntity(ctx.world);
  auto &globeTr = ctx.world.Add<components::Transform>(m_globeEntity);
  globeTr.position = {0.0f, 2.0f, 0.0f}; // クラブの上、高い位置に浮遊
  globeTr.scale = {2.0f, 2.0f, 2.0f};

  auto &globeMr = ctx.world.Add<components::MeshRenderer>(m_globeEntity);
  globeMr.mesh = ctx.resource.LoadMesh(
      "Assets/models/Wikipedia_puzzle_globe_3D_render.stl");
  globeMr.shader = basicShader;
  globeMr.color = {0.9f, 0.9f, 0.95f, 1.0f}; // 白っぽいシルバー
  globeMr.isVisible = true;

  LOG_INFO("TitleScene", "Wikipedia globe loaded");

  // --- 5. パーティクル (虹色の輝き - 超派手) ---
  std::mt19937 rng(9999);
  std::uniform_real_distribution<float> distPos(-20.0f, 20.0f);   // より広範囲
  std::uniform_real_distribution<float> distHeight(-3.0f, 15.0f); // より高く
  std::uniform_real_distribution<float> distPhase(0.0f, 6.28f);
  std::uniform_real_distribution<float> distSpeed(0.3f, 2.0f); // より速く

  for (int i = 0; i < 400; ++i) { // 大幅増量！
    auto e = CreateEntity(ctx.world);
    auto &t = ctx.world.Add<components::Transform>(e);
    t.position = {distPos(rng), distHeight(rng), distPos(rng)};
    float s = 0.03f + (i % 8) * 0.03f; // より多様なサイズ
    t.scale = {s, s, s};

    auto &mr = ctx.world.Add<components::MeshRenderer>(e);
    mr.mesh = sphereMesh;
    mr.shader = basicShader;

    // 虹色グラデーション（HSV→RGB変換）
    float hue = (float)(i % 60) * 6.0f; // 0-360度
    float h = hue / 60.0f;
    int hi = (int)h % 6;
    float f = h - (int)h;
    float r, g, b;
    switch (hi) {
    case 0:
      r = 1.0f;
      g = f;
      b = 0.0f;
      break;
    case 1:
      r = 1.0f - f;
      g = 1.0f;
      b = 0.0f;
      break;
    case 2:
      r = 0.0f;
      g = 1.0f;
      b = f;
      break;
    case 3:
      r = 0.0f;
      g = 1.0f - f;
      b = 1.0f;
      break;
    case 4:
      r = f;
      g = 0.0f;
      b = 1.0f;
      break;
    default:
      r = 1.0f;
      g = 0.0f;
      b = 1.0f - f;
      break;
    }

    // 発光感（1.0超えで明るく）
    float brightness = 1.2f + (float)(i % 5) * 0.2f;
    mr.color = {r * brightness, g * brightness, b * brightness, 0.7f};
    mr.isVisible = true;

    Particle p;
    p.entity = e;
    p.basePos = t.position;
    p.phase = distPhase(rng);
    p.speed = distSpeed(rng);
    m_particles.push_back(p);
  }
  LOG_INFO("TitleScene", "Particles created (400 ultra-fancy)");

  // --- 6. カメラ ---
  m_cameraEntity = CreateEntity(ctx.world);
  auto &camTr = ctx.world.Add<components::Transform>(m_cameraEntity);
  // 初期位置（後で上書き）
  camTr.position = {0.0f, 3.0f, -10.0f};

  auto &cam = ctx.world.Add<components::Camera>(m_cameraEntity);
  cam.fov = XMConvertToRadians(50.0f);
  cam.nearZ = 0.01f;
  cam.farZ = 500.0f;
  cam.isMainCamera = true;
  LOG_INFO("TitleScene", "Camera created");

  // --- 7. UI ---
  // ラグジュアリースタイル（TextStyle.hに定義済み）
  auto titleStyle = graphics::TextStyle::LuxuryTitle();
  auto subStyle = graphics::TextStyle::ModernBlack();
  subStyle.fontSize = 26.0f;
  subStyle.color = {0.8f, 0.8f, 0.9f, 1.0f};
  subStyle.align = graphics::TextAlign::Center;
  subStyle.hasShadow = true;

  auto btnStyle = graphics::TextStyle::LuxuryButton();

  auto spawnUI = [&](const std::wstring &text, float yOffset,
                     const graphics::TextStyle &style,
                     bool interactive = false) {
    auto e = CreateEntity(ctx.world);
    auto &t = ctx.world.Add<components::UIText>(e);
    t.text = text;
    t.x = 0.0f;
    t.y = yOffset;
    t.style = style;
    t.visible = true;
    t.width = 1280.0f; // 画面幅全体を使って中央揃え
    t.layer = 10;      // テキストは手前

    UIElement elem;
    elem.entity = e;
    elem.baseX = 0.0f;
    elem.baseY = yOffset;
    elem.currentScale = 1.0f;
    elem.targetScale = 1.0f;
    elem.text = text;
    elem.baseColor = style.color;
    elem.isHovered = false;
    elem.isClicked = false;

    m_uiElements.push_back(elem);
  };

  spawnUI(L"WIKI GOLF", 160.0f, titleStyle);
  spawnUI(L"- The Encyclopedia Course -", 270.0f, subStyle);
  spawnUI(L"START GAME", 550.0f, btnStyle,
          true); // 少し下に配置して空間を見せる

  LOG_INFO("TitleScene", "OnEnter (Luxury) complete");
}

void TitleScene::OnUpdate(core::GameContext &ctx) {
  m_time += ctx.dt;

  // --- 1. カメラワーク (ドラマチックな旋回) ---
  float orbitSpeed = 0.15f;
  float angle = m_time * orbitSpeed;
  float radius = 5.5f + std::sin(m_time * 0.12f) * 1.5f;

  float camX = std::sin(angle) * radius;
  float camZ = std::cos(angle) * -radius;
  float camY = 2.5f + std::sin(m_time * 0.2f) * 0.5f;

  if (ctx.world.IsAlive(m_cameraEntity)) {
    auto *tr = ctx.world.Get<components::Transform>(m_cameraEntity);
    if (tr) {
      tr->position = {camX, camY, camZ};

      // LookAt: 浮遊ボール(Y=1.8)を見る
      XMVECTOR eye = XMLoadFloat3(&tr->position);
      XMVECTOR focus = XMVectorSet(0.0f, 1.8f, 0.0f, 0.0f);
      XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

      XMMATRIX view = XMMatrixLookAtLH(eye, focus, up);
      XMMATRIX invView = XMMatrixInverse(nullptr, view);
      XMVECTOR q = XMQuaternionRotationMatrix(invView);
      XMStoreFloat4(&tr->rotation, q);
    }
  }

  // --- 2. 衛星クラブの公転 ---
  for (size_t i = 0; i < m_clubs.size(); ++i) {
    if (!ctx.world.IsAlive(m_clubs[i]))
      continue;
    auto *t = ctx.world.Get<components::Transform>(m_clubs[i]);

    float baseAngle = (float)i * (DirectX::XM_2PI / 3.0f);
    float rotSpeed = 0.8f;
    float currentAngle = baseAngle + m_time * rotSpeed;

    float r = 1.6f + std::sin(m_time * 2.0f + (float)i) *
                         0.1f; // 呼吸するように距離変化

    t->position.x = std::cos(currentAngle) * r;
    t->position.z = std::sin(currentAngle) * r;
    t->position.y = 1.8f + std::cos(m_time * 1.5f + (float)i) * 0.2f;

    // ボールの方を向く（または接線方向）
    // ここでは綺麗に見える固定回転 + 少し揺らぎ
    XMVECTOR q = XMQuaternionRotationRollPitchYaw(
        XMConvertToRadians(15.0f),
        -currentAngle + XMConvertToRadians(90.0f), // 接線方向
        XMConvertToRadians(45.0f + std::sin(m_time) * 10.0f));
    XMStoreFloat4(&t->rotation, q);

    // 反射の同期
    if (i < m_reflections.size() *
                2) { // reflectionsにはボールの反射も入ってるので注意
      // ボールの反射は m_reflections[0]
      // クラブの反射は m_reflections[1 + i]
      size_t refIdx = 1 + i;
      if (refIdx < m_reflections.size() &&
          ctx.world.IsAlive(m_reflections[refIdx])) {
        auto *rt = ctx.world.Get<components::Transform>(m_reflections[refIdx]);
        rt->position = t->position;
        rt->position.y *= -1.0f;

        // 回転も反転（ピッチとロールを反転させる簡易近似）
        XMVECTOR rq = XMQuaternionRotationRollPitchYaw(
            -XMConvertToRadians(15.0f),
            -currentAngle + XMConvertToRadians(90.0f),
            -XMConvertToRadians(45.0f + std::sin(m_time) * 10.0f));
        XMStoreFloat4(&rt->rotation, rq);
      }
    }
  }

  // ボールの反射同期 (浮遊アニメがあれば)
  if (ctx.world.IsAlive(m_ballEntity) && !m_reflections.empty() &&
      ctx.world.IsAlive(m_reflections[0])) {
    auto *bt = ctx.world.Get<components::Transform>(m_ballEntity);
    auto *rt = ctx.world.Get<components::Transform>(m_reflections[0]);

    // ボール自体も少し上下動
    bt->position.y = 1.8f + std::sin(m_time * 0.8f) * 0.1f;

    // ボールは回転させる
    XMVECTOR q =
        XMQuaternionRotationRollPitchYaw(m_time * 0.5f, m_time * 0.3f, 0.0f);
    XMStoreFloat4(&bt->rotation, q);

    rt->position = bt->position;
    rt->position.y *= -1.0f;
    // 反射の回転は逆
    XMVECTOR rq =
        XMQuaternionRotationRollPitchYaw(-m_time * 0.5f, m_time * 0.3f, 0.0f);
    XMStoreFloat4(&rt->rotation, rq);
  }

  // --- 2.5. Wikipedia地球儀回転 ---
  if (ctx.world.IsAlive(m_globeEntity)) {
    auto *gt = ctx.world.Get<components::Transform>(m_globeEntity);
    if (gt) {
      // ゆっくり上下に浮遊 + Y軸回転
      gt->position.y = 4.0f + std::sin(m_time * 0.5f) * 0.3f;

      // 地球儀らしく傾けてY軸回転
      XMVECTOR q = XMQuaternionRotationRollPitchYaw(
          XMConvertToRadians(23.5f), // 地軸傾斜
          m_time * 0.3f,             // Y軸回転
          0.0f);
      XMStoreFloat4(&gt->rotation, q);
    }
  }

  // --- 3. 背景リング回転 ---
  for (size_t i = 0; i < m_ringObjects.size(); ++i) {
    if (!ctx.world.IsAlive(m_ringObjects[i]))
      continue;
    auto *t = ctx.world.Get<components::Transform>(m_ringObjects[i]);

    float baseAngle =
        (float)i * (DirectX::XM_2PI / (float)m_ringObjects.size());
    float ringRot = m_time * 0.05f; // ゆっくり全体回転
    float angle = baseAngle + ringRot;

    float r = 12.0f;
    t->position.x = std::cos(angle) * r;
    t->position.z = std::sin(angle) * r;

    // 個別に回転
    XMVECTOR q =
        XMQuaternionRotationRollPitchYaw(m_time * 0.2f, -angle, m_time * 0.1f);
    XMStoreFloat4(&t->rotation, q);
  }

  // --- 4. パーティクル (スパイラル上昇 - 超派手) ---
  for (auto &p : m_particles) {
    if (ctx.world.IsAlive(p.entity)) {
      auto *t = ctx.world.Get<components::Transform>(p.entity);
      auto *mr = ctx.world.Get<components::MeshRenderer>(p.entity);
      if (t) {
        // スパイラル上昇（より大きな動き）
        float sway = std::sin(m_time * p.speed * 1.5f + p.phase) * 1.5f;
        float rise =
            std::fmod(m_time * 1.2f + p.phase, 18.0f) - 3.0f; // より高く、速く

        // ボール（中心）に引き寄せられる効果
        float distFromCenter =
            std::sqrt(p.basePos.x * p.basePos.x + p.basePos.z * p.basePos.z);
        float attraction = std::max(0.0f, 1.0f - distFromCenter / 20.0f) * 0.3f;
        float attractionX = -p.basePos.x * attraction * std::sin(m_time * 0.5f);
        float attractionZ = -p.basePos.z * attraction * std::cos(m_time * 0.5f);

        t->position.x = p.basePos.x + sway * 0.5f + attractionX;
        t->position.z = p.basePos.z + std::cos(m_time * p.speed * 1.5f) * 0.5f +
                        attractionZ;
        t->position.y = rise;

        // ダイナミックな明滅（より激しく）
        float blink = 0.3f + 0.7f * std::sin(m_time * 5.0f + p.phase);
        float baseSize = 0.04f + std::sin(p.phase) * 0.02f;
        t->scale = {baseSize * blink, baseSize * blink, baseSize * blink};

        // 色の動的変化（時間で虹色シフト）
        if (mr) {
          float hue = std::fmod(p.phase * 60.0f + m_time * 30.0f, 360.0f);
          float h = hue / 60.0f;
          int hi = (int)h % 6;
          float f = h - (int)h;
          float r, g, b;
          switch (hi) {
          case 0:
            r = 1.0f;
            g = f;
            b = 0.0f;
            break;
          case 1:
            r = 1.0f - f;
            g = 1.0f;
            b = 0.0f;
            break;
          case 2:
            r = 0.0f;
            g = 1.0f;
            b = f;
            break;
          case 3:
            r = 0.0f;
            g = 1.0f - f;
            b = 1.0f;
            break;
          case 4:
            r = f;
            g = 0.0f;
            b = 1.0f;
            break;
          default:
            r = 1.0f;
            g = 0.0f;
            b = 1.0f - f;
            break;
          }
          float brightness = 1.5f + blink * 0.5f; // 明るさ連動
          mr->color = {r * brightness, g * brightness, b * brightness,
                       0.6f + blink * 0.3f};
        }
      }
    }
  }

  // --- 5. UI ---
  auto mousePos = ctx.input.GetMousePosition();
  bool startClicked = false;

  for (auto &elem : m_uiElements) {
    auto *t = ctx.world.Get<components::UIText>(elem.entity);
    if (!t)
      continue;

    if (elem.text == L"WIKI GOLF") {
      // 鼓動
      float pulse = 1.0f + std::sin(m_time * 1.2f) * 0.02f;
      t->style.fontSize = 110.0f * pulse;

      float shine = std::sin(m_time * 2.0f); // -1 to 1
      // ゴールドの輝き移動
      t->style.color = {1.0f, 0.9f + shine * 0.05f, 0.5f + shine * 0.2f, 1.0f};
    }

    if (elem.text == L"START GAME") {
      float w = 400.0f;
      float h = 70.0f;
      float x = (1280.0f - w) / 2.0f;
      float y = elem.baseY;

      bool hover = (mousePos.x >= x && mousePos.x <= x + w && mousePos.y >= y &&
                    mousePos.y <= y + h);

      if (hover && !elem.isHovered) {
        if (ctx.audio)
          ctx.audio->PlaySE(ctx, "se_shot_soft.mp3", 0.4f);
        elem.targetScale = 1.2f;
        elem.isHovered = true;
      } else if (!hover && elem.isHovered) {
        elem.targetScale = 1.0f;
        elem.isHovered = false;
      }

      if (hover && ctx.input.GetMouseButtonDown(0)) {
        startClicked = true;
        elem.targetScale = 0.9f;
      }

      elem.currentScale +=
          (elem.targetScale - elem.currentScale) * (15.0f * ctx.dt);
      t->style.fontSize = 42.0f * elem.currentScale;

      if (elem.isHovered) {
        t->style.color = {0.6f, 0.9f, 1.0f, 1.0f}; // 青白く輝く
        t->style.outlineColor = {0.0f, 0.6f, 1.0f, 1.0f};
        t->style.outlineWidth = 3.0f;
        // 影も光る
        t->style.shadowColor = {0.0f, 0.5f, 1.0f, 0.5f};
      } else {
        t->style.color = elem.baseColor;
        t->style.outlineColor = {0.0f, 0.2f, 0.3f, 0.8f};
        t->style.outlineWidth = 1.0f;
        t->style.shadowColor = {0.0f, 0.0f, 0.0f, 0.5f};
      }
    }
  }

  if (ctx.input.GetKeyDown(VK_SPACE) || ctx.input.GetKeyDown(VK_RETURN)) {
    startClicked = true;
  }

  if (startClicked) {
    if (ctx.audio) {
      ctx.audio->PlaySE(ctx, "se_shot_hard.mp3"); // 強力な決定音
    }
    if (ctx.sceneManager) {
      auto loadingScene = std::make_unique<LoadingScene>(
          []() { return std::make_unique<WikiGolfScene>(); });
      ctx.sceneManager->ChangeScene(std::move(loadingScene));
    }
  }

  if (ctx.input.GetKeyDown(VK_ESCAPE)) {
    ctx.shouldClose = true;
  }
}

void TitleScene::OnExit(core::GameContext &ctx) {
  LOG_INFO("TitleScene", "OnExit");
  // 全エンティティ破壊
  DestroyAllEntities(ctx);

  m_uiElements.clear();
  m_particles.clear();
  m_clubs.clear();
  m_ringObjects.clear();
  m_reflections.clear();
}

} // namespace game::scenes
