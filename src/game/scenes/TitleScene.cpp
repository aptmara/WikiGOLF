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

  LOG_INFO("TitleScene", "Resources loaded (Basic shader, meshes)");

  // --- 1. アビス・フロア (The Abyssal Mirror) ---
  m_floorEntity = CreateEntity(ctx.world);
  auto &floorTr = ctx.world.Add<components::Transform>(m_floorEntity);
  floorTr.position = {0.0f, 0.0f, 0.0f};
  floorTr.scale = {500.0f, 1.0f, 500.0f}; // より広大に
  auto &floorMr = ctx.world.Add<components::MeshRenderer>(m_floorEntity);
  floorMr.mesh = planeMesh;
  floorMr.shader = basicShader;
  // 深淵の黒＋わずかな紫
  floorMr.color = {0.02f, 0.0f, 0.05f, 0.9f};
  floorMr.isVisible = true;

  // 深淵グリッド（フロアの下で明滅する）
  {
    int gridLines = 40;
    float spacing = 20.0f;
    // 実装簡略化のため、いくつか代表的なラインだけ引くか、ここではスキップして他の演出にリソースを割く
    // (今回はオーロラ等に注力)
  }
  LOG_INFO("TitleScene", "Abyssal Floor created");

  // --- 2. 浮遊ボール (神器) -> 削除済み ---

  // ボールの反射 -> 削除済み

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

  // --- 4. インフィニティ・ハイパードライブ・リング (Infinity Hyperdrive) ---
  // リングを三重構造に増強 (3層 x 40個 = 120個)
  int ringLayers = 3;
  int ringsPerLayer = 40;

  for (int layer = 0; layer < ringLayers; ++layer) {
    float baseRadius = 12.0f + layer * 8.0f; // 12, 20, 28
    float heightScale = 4.0f + layer * 2.0f; // 奥ほど巨大

    for (int i = 0; i < ringsPerLayer; ++i) {
      auto e = CreateEntity(ctx.world);
      auto &t = ctx.world.Add<components::Transform>(e);
      float angle = (float)i * (DirectX::XM_2PI / ringsPerLayer);

      t.position = {std::cos(angle) * baseRadius,
                    6.0f + std::sin(angle * 3.0f) * 2.0f,
                    std::sin(angle) * baseRadius};

      // 柱の太さも変える
      t.scale = {0.5f + layer * 0.2f, heightScale, 0.5f + layer * 0.2f};

      // 中心を向く回転
      XMVECTOR q = XMQuaternionRotationRollPitchYaw(0.0f, -angle, 0.0f);
      XMStoreFloat4(&t.rotation, q);

      auto &mr = ctx.world.Add<components::MeshRenderer>(e);
      mr.mesh = cubeMesh;
      mr.shader = basicShader;

      // 層ごとに色味を変える (Cyan -> Blue -> Purple)
      if (layer == 0)
        mr.color = {0.0f, 0.8f, 1.0f, 1.0f}; // Cyan
      else if (layer == 1)
        mr.color = {0.1f, 0.3f, 1.0f, 1.0f}; // Blue
      else
        mr.color = {0.6f, 0.0f, 1.0f, 1.0f}; // Purple

      mr.isVisible = true;
      m_ringObjects.push_back(e);
    }
  }
  LOG_INFO("TitleScene", "Infinity Rings created (Triple Layer)");

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

  // --- 5. パーティクル (控えめに調整) ---
  std::mt19937 rng(9999);
  std::uniform_real_distribution<float> distPos(-20.0f, 20.0f);
  std::uniform_real_distribution<float> distHeight(-3.0f, 15.0f);
  std::uniform_real_distribution<float> distPhase(0.0f, 6.28f);
  std::uniform_real_distribution<float> distSpeed(0.2f, 1.0f); // 速度も控えめに

  // 既存スパイラルパーティクル (300個程度に削減)
  for (int i = 0; i < 300; ++i) {
    auto e = CreateEntity(ctx.world);
    auto &t = ctx.world.Add<components::Transform>(e);
    t.position = {distPos(rng), distHeight(rng), distPos(rng)};
    float s = 0.02f + (i % 5) * 0.02f; // サイズも少し小さく
    t.scale = {s, s, s};

    auto &mr = ctx.world.Add<components::MeshRenderer>(e);
    mr.mesh = sphereMesh;
    mr.shader = basicShader;
    mr.color = {1.0f, 1.0f, 1.0f, 1.0f}; // 後で更新
    mr.isVisible = true;

    Particle p;
    p.entity = e;
    p.basePos = t.position;
    p.phase = distPhase(rng);
    p.speed = distSpeed(rng);
    p.type = 0; // Normal
    m_particles.push_back(p);
  }

  // ギャラクシー渦巻きパーティクル -> 削除（多すぎるため）
  LOG_INFO("TitleScene", "Particles created (300 modest)");

  // --- 6. デジタル・オーロラ (Digital Aurora) ---
  int auroraSegments = 100;
  for (int i = 0; i < auroraSegments; ++i) {
    auto e = CreateEntity(ctx.world);
    auto &t = ctx.world.Add<components::Transform>(e);
    // 上空に配置
    float x = ((float)i - auroraSegments / 2.0f) * 1.5f;
    t.position = {x, 15.0f, 20.0f};
    t.scale = {0.8f, 8.0f, 0.2f}; // 縦長の帯

    auto &mr = ctx.world.Add<components::MeshRenderer>(e);
    mr.mesh = cubeMesh;
    mr.shader = basicShader;
    mr.color = {0.0f, 1.0f, 0.8f, 0.3f}; // 半透明シアン
    mr.isVisible = true;

    AuroraSegment as;
    as.entity = e;
    as.phaseOffset = (float)i * 0.1f;
    as.heightBase = 15.0f;
    m_auroraSegments.push_back(as);
  }

  // --- 7. ディバイン・ライト (神の光) ---
  {
    auto e = CreateEntity(ctx.world);
    auto &t = ctx.world.Add<components::Transform>(e);
    t.position = {0.0f, 10.0f, 0.0f};
    t.scale = {2.0f, 20.0f, 2.0f}; // 細長い光柱

    auto &mr = ctx.world.Add<components::MeshRenderer>(e);
    mr.mesh = ctx.resource.LoadMesh("builtin/cylinder"); // 円柱で代用
    mr.shader = basicShader;
    mr.color = {1.0f, 1.0f, 0.8f, 0.15f}; // 非常に薄い光
    mr.isVisible = true;
    // 回転アニメはUpdateで
    m_reflections.push_back(e); // 簡易的にリストに入れておく（管理用）
  }

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

      // LookAt: 地球儀のあたり(Y=4.0)を見る
      XMVECTOR eye = XMLoadFloat3(&tr->position);
      XMVECTOR focus = XMVectorSet(0.0f, 4.0f, 0.0f, 0.0f);
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
    float rotSpeed = 0.5f; // 少しゆっくりに
    float currentAngle = baseAngle + m_time * rotSpeed;

    float r = 2.5f + std::sin(m_time * 1.5f + (float)i) * 0.2f;

    t->position.x = std::cos(currentAngle) * r;
    t->position.z = std::sin(currentAngle) * r;
    t->position.y = 2.0f + std::cos(m_time * 1.0f + (float)i) * 0.3f;

    // クラブの回転
    XMVECTOR q = XMQuaternionRotationRollPitchYaw(
        XMConvertToRadians(15.0f),
        -currentAngle + XMConvertToRadians(90.0f), // 接線方向
        XMConvertToRadians(45.0f + std::sin(m_time) * 5.0f));
    XMStoreFloat4(&t->rotation, q);

    // 反射の同期
    if (i < m_reflections.size()) {
      // 元々 reflections=[ballReflection, clubRef1, clubRef2, clubRef3,
      // DivineLight] だったが
      // ballReflectionが削除されたので、クラブ反射のインデックスが変わる可能性がある
      // しかし m_reflections への push 順序による。
      // 1. BallRef (削除済み)
      // 2. ClubRefs (ここ)
      // なので、リストを正しく管理しないとズレる。
      // 面倒なので m_reflections
      // を使わず、クラブ生成時に親子関係っぽく管理するのがベストだが、
      // ここでは単純に「リフレクション用エンティティ」を別途メンバ変数で持っていないため、
      // m_reflections の中身を推測する必要がある。
      // 今回の実装では、m_reflections には「Divine Light」も入っている。

      // 実装簡易化:
      // クラブのループ内で対応する反射エンティティを探すのは困難（インデックス依存）。
      // むしろ反射エンティティも m_clubs と並列で管理すべきだった。
      // ここでは「クラブの直後に反射を作った」わけではないので（Create時）、ループで回すのは危険。

      // TitleScene.hを見ると m_reflections は vector<Entity>。
      // OnEnterでの生成順序:
      // 1. (削除) Ball Reflection
      // 2. Club Reflection 1
      // 3. Club Reflection 2
      // 4. Club Reflection 3
      // 5. Divine Light

      // つまり、m_reflections[0] ~ [2]
      // がクラブの反射になるはず（BallRefが消えたので）。

      if (i < m_reflections.size()) {
        // インデックス i がそのまま反射に対応すると仮定
        auto *rt = ctx.world.Get<components::Transform>(m_reflections[i]);
        if (rt) {
          rt->position = t->position;
          rt->position.y *= -1.0f;
          // 回転も反転
          XMVECTOR rq = XMQuaternionRotationRollPitchYaw(
              -XMConvertToRadians(15.0f),
              -currentAngle + XMConvertToRadians(90.0f),
              -XMConvertToRadians(45.0f + std::sin(m_time) * 5.0f));
          XMStoreFloat4(&rt->rotation, rq);
        }
      }
    }
  }

  // ボールの反射同期 -> 削除済み

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

  // --- 3. インフィニティ・ハイパードライブ・リング回転 ---
  int ringCounter = 0;
  for (size_t i = 0; i < m_ringObjects.size(); ++i) {
    if (!ctx.world.IsAlive(m_ringObjects[i]))
      continue;
    auto *t = ctx.world.Get<components::Transform>(m_ringObjects[i]);

    // インデックスに応じた複雑な動き
    float layer = (float)(i / 40); // 0, 1, 2
    float idxInLayer = (float)(i % 40);

    float baseAngle = idxInLayer * (DirectX::XM_2PI / 40.0f);

    // 層ごとに回転速度を変える（逆回転も）
    float rotSpeed =
        0.05f * (1.0f + layer * 2.0f) * ((layer == 1) ? -1.0f : 1.0f);
    float angle = baseAngle + m_time * rotSpeed;

    // 半径の呼吸
    float r = (12.0f + layer * 8.0f) +
              std::sin(m_time * 2.0f + idxInLayer * 0.1f) * (0.5f + layer);

    t->position.x = std::cos(angle) * r;
    t->position.z = std::sin(angle) * r;

    // 音楽波形のような高さ変動
    float wave =
        std::sin(angle * 4.0f + m_time * 3.0f) * std::cos(m_time + angle);
    t->scale.y = (4.0f + layer * 2.0f) * (1.0f + wave * 0.5f);

    // ワープ効果（Z軸方向へのストレッチと移動...は今回は止めて回転強調）

    // 3軸回転
    XMVECTOR q = XMQuaternionRotationRollPitchYaw(
        m_time * (0.2f + layer * 0.1f), -angle, m_time * 0.1f + wave);
    XMStoreFloat4(&t->rotation, q);
  }

  // --- 4. ギャラクシー・ボルテックス & スパイラル ---
  for (auto &p : m_particles) {
    if (ctx.world.IsAlive(p.entity)) {
      auto *t = ctx.world.Get<components::Transform>(p.entity);
      auto *mr = ctx.world.Get<components::MeshRenderer>(p.entity);
      if (!t || !mr)
        continue;

      if (p.type == 0) {
        // Normal: スパイラル上昇
        float sway = std::sin(m_time * p.speed * 1.5f + p.phase) * 1.5f;
        float rise = std::fmod(m_time * 1.2f + p.phase, 18.0f) - 3.0f;

        t->position.x = p.basePos.x + sway * 0.5f; // 引力省略でCPU負荷軽減
        t->position.z = p.basePos.z + std::cos(m_time * p.speed * 1.5f) * 0.5f;
        t->position.y = rise;

        float blink = 0.3f + 0.7f * std::sin(m_time * 5.0f + p.phase);
        mr->color.w = 0.6f + blink * 0.3f; // Alpha明滅
      } else if (p.type == 1) {
        // Galaxy: 渦巻き回転
        // p.basePos を中心からの相対ベクトルとして使用しない、動的に計算
        // p.phase = 初期角度

        // 半径減衰（吸い込み）
        float rInfo = std::sqrt(p.basePos.x * p.basePos.x +
                                p.basePos.z * p.basePos.z);        // 概算半径
        float currentR = rInfo * (1.0f + 0.1f * std::sin(m_time)); // 呼吸

        // 角度進行
        float currentTheta = p.phase + m_time * 0.5f * p.speed;

        t->position.x = currentR * std::cos(currentTheta);
        t->position.z = currentR * std::sin(currentTheta);
        // Y軸のうねり
        t->position.y = p.basePos.y + std::sin(currentTheta * 2.0f) * 0.5f;

        // 色変換 (Gold <-> Magenta)
        float colorMix = std::sin(currentTheta + m_time); // -1 ~ 1
        if (colorMix > 0) {
          mr->color = {1.0f, 0.8f + colorMix * 0.2f, 0.2f, 0.8f};
        } else {
          mr->color = {1.0f, 0.0f, 0.5f - colorMix * 0.5f, 0.8f};
        }
      }
    }
  }

  // --- 6. オーロラアニメーション ---
  for (auto &as : m_auroraSegments) {
    if (!ctx.world.IsAlive(as.entity))
      continue;
    auto *t = ctx.world.Get<components::Transform>(as.entity);

    // パーリンノイズ風の波
    float wave = std::sin(m_time * 0.5f + as.phaseOffset) +
                 0.5f * std::sin(m_time * 1.2f + as.phaseOffset * 3.0f);

    t->position.y = as.heightBase + wave * 2.0f;
    t->position.z = 20.0f + std::cos(m_time * 0.3f + as.phaseOffset) * 5.0f;
    t->scale.y = 8.0f + wave * 3.0f; // 伸縮

    // 色変調
    auto *mr = ctx.world.Get<components::MeshRenderer>(as.entity);
    if (mr) {
      mr->color.x = 0.0f;                                            // R
      mr->color.y = 0.7f + 0.3f * std::sin(m_time + as.phaseOffset); // G
      mr->color.z = 0.8f + 0.2f * std::cos(m_time * 0.8f);           // B
    }
  }

  // --- 7. ストーム発生 (簡易版) ---
  m_stormTimer += ctx.dt;
  if (m_stormTimer > 0.1f) {
    m_stormTimer = 0.0f;
    // 負荷考慮し今回は省略、または既に十分豪華なためスキップ
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
  m_auroraSegments.clear();
  m_stormObjects.clear();
  m_clubs.clear();
  m_ringObjects.clear();
  m_reflections.clear();
}

} // namespace game::scenes