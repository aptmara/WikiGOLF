#include "TitleScene.h"
#include "../../audio/AudioSystem.h"
#include "../../core/GameContext.h"
#include "../../core/Input.h"
#include "../../core/SceneManager.h"
#include "../../core/StringUtils.h"
#include "../../graphics/GraphicsDevice.h"
#include "../../graphics/VideoPlayer.h"
#include "../../graphics/TextRenderer.h"
#include "../../graphics/SkyboxTextureGenerator.h"
#include "../components/Camera.h"
#include "../components/MeshRenderer.h"
#include "../components/PhysicsComponents.h"
#include "../components/Skybox.h"
#include "../components/Transform.h"
#include "../components/UIButton.h"
#include "../components/UIImage.h"
#include "../components/UIText.h"
#include "../components/UIText.h"
#include "../components/WikiComponents.h"
#include "../systems/SkyboxRenderSystem.h"
#include "../systems/TerrainGenerator.h"
#include "../systems/WikiClient.h"
#include "../../core/StringUtils.h"
#include "LoadingScene.h"
#include "WikiGolfScene.h"
#include <filesystem>
#include <fstream>
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <windows.h>
#include <shellapi.h> // ShellExecuteA用

namespace game::scenes {

using namespace DirectX;

namespace {
constexpr float kIntroAudioVolume = 0.8f;
}

namespace {
std::string UrlDecode(const std::string& src) {
  std::string ret;
  for (size_t i = 0; i < src.length(); i++) {
    if (src[i] == '%' && i + 2 < src.length()) {
      int ii;
      if (sscanf_s(src.substr(i + 1, 2).c_str(), "%x", &ii) == 1) {
        ret += static_cast<char>(ii);
        i += 2;
      } else {
        ret += src[i];
      }
    } else if (src[i] == '+') {
      ret += ' ';
    } else {
      ret += src[i];
    }
  }
  return ret;
}

std::string ExtractWikiTitle(const std::string& input) {
  std::string prefix = "wikipedia.org/wiki/";
  size_t pos = input.find(prefix);
  std::string title = input;
  if (pos != std::string::npos) {
    title = input.substr(pos + prefix.length());
    size_t hashPos = title.find('#');
    if (hashPos != std::string::npos) title = title.substr(0, hashPos);
    size_t qPos = title.find('?');
    if (qPos != std::string::npos) title = title.substr(0, qPos);
    title = UrlDecode(title);
  }
  return title;
}
} // namespace

// ===========================================================================
// OnEnter
// ===========================================================================
/**
 * @brief シーンに侵入した際の初期化処理を行います。
 */
void TitleScene::OnEnter(core::GameContext &ctx) {
  LOG_INFO("TitleScene", "OnEnter (WIKI GOLF High-End UI Style)");

  m_time = 0.0f;
  m_menuEntries.clear();

  // マウスカーソル表示
  ctx.input.SetMouseCursorVisible(true);
  ctx.input.SetMouseCursorLocked(false);


  m_videoPlayer = std::make_unique<graphics::VideoPlayer>();
  if (!m_videoPlayer->Initialize(ctx.graphics.GetDevice(), "Assets/videos/aptma_intro.mp4")) {
      LOG_ERROR("TitleScene", "Failed to load intro video");
      m_videoPlayer.reset();
      m_startupLoadTask = std::async(std::launch::async, [](){});
  } else if (ctx.audio) {
      ctx.audio->PlayOneShotFile(ctx, kIntroAudioLabel, "Assets/videos/aptma_intro.mp4", kIntroAudioVolume);
  }

  m_startupLoadTask = std::async(std::launch::async, [&ctx]() {
      LOG_INFO("TitleScene", "Async load task started on thread!");
      try {
        HRESULT hrCom = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        LOG_INFO("TitleScene", "CoInitializeEx returned: {:08X}", static_cast<uint32_t>(hrCom));

        LOG_INFO("TitleScene", "Step 1: LoadAudio");
        if (ctx.audio) {
            LOG_INFO("TitleScene", "Calling ctx.resource.LoadAudio bgm_title.mp3...");
            ctx.resource.LoadAudio("Assets/sounds/bgm_title.mp3");
            LOG_INFO("TitleScene", "LoadAudio bgm_title.mp3 finished successfully!");
        }

        LOG_INFO("TitleScene", "Step 2: LoadShader Basic");
        ctx.resource.LoadShader("Basic", L"Assets/shaders/BasicVS.hlsl", L"Assets/shaders/BasicPS.hlsl");
        LOG_INFO("TitleScene", "Step 3: LoadShader Skybox");
        ctx.resource.LoadShader("Skybox", L"Assets/shaders/SkyboxVS.hlsl", L"Assets/shaders/SkyboxPS.hlsl");

        LOG_INFO("TitleScene", "Step 4: LoadMesh sphere");
        ctx.resource.LoadMesh("builtin/sphere");

        LOG_INFO("TitleScene", "Step 5: LoadMesh globe");
        ctx.resource.LoadMesh("Assets/models/Wikipedia_puzzle_globe_3D_render.stl");

        LOG_INFO("TitleScene", "Step 6: LoadTextureSRV");
        ctx.resource.LoadTextureSRV("Assets/textures/GRASS_BASE.png");

        LOG_INFO("TitleScene", "Step 7: GenerateTerrain");
        game::systems::TerrainConfig tconf;
        tconf.worldWidth = 150.0f; tconf.worldDepth = 150.0f;
        tconf.resolutionX = 64; tconf.resolutionZ = 64;
        tconf.baseHeight = 0.0f; tconf.heightScale = 2.5f; tconf.biome = 0;
        auto tdata = game::systems::TerrainGenerator::GenerateTerrain("TitleSeed", {}, tconf);

        LOG_INFO("TitleScene", "Step 8: CreateDynamicMesh");
        ctx.resource.CreateDynamicMesh("TitleTerrain", tdata.vertices, tdata.indices);

        LOG_INFO("TitleScene", "Step 9: LoadCubemapFromSingleFile");
        graphics::SkyboxTextureGenerator gen;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> cubemapSRV;
        gen.LoadCubemapFromSingleFile(ctx.graphics.GetDevice(), L"Assets/textures/skybox_default_px_1767953230432.png", cubemapSRV);

        LOG_INFO("TitleScene", "Async load finished");
        CoUninitialize();
      } catch (const std::exception& e) {
        LOG_ERROR("TitleScene", "Exception caught in async load task: {}", e.what());
      } catch (...) {
        LOG_ERROR("TitleScene", "Unknown exception caught in async load task!");
      }
  });
}

/**
 * @brief スタートアップロード完了後の初期化処理を行います。
 */
void TitleScene::FinalizeStartupLoad(core::GameContext &ctx) {
  StopIntroAudio(ctx);

// BGM 再生
  if (ctx.audio) {
    ctx.audio->PlayBGM(ctx, "bgm_title.mp3", 0.5f);
  }

  // リソースのロード
  auto basicShader = ctx.resource.LoadShader(
      "Basic", L"Assets/shaders/BasicVS.hlsl", L"Assets/shaders/BasicPS.hlsl");
  auto sphereMesh = ctx.resource.LoadMesh("builtin/sphere");
  auto globeMesh  = ctx.resource.LoadMesh("Assets/models/Wikipedia_puzzle_globe_3D_render.stl");

  // スカイボックスの生成
  m_skyboxEntity = CreateEntity(ctx.world);
  auto &skybox = ctx.world.Add<components::Skybox>(m_skyboxEntity);
  {
    graphics::SkyboxTextureGenerator gen;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> cubemapSRV;
    bool ok = gen.LoadCubemapFromSingleFile(
        ctx.graphics.GetDevice(),
        L"Assets/textures/skybox_default_px_1767953230432.png",
        cubemapSRV);
    if (ok) {
      skybox.cubemapSRV  = cubemapSRV;
      skybox.isVisible   = true;
      skybox.brightness  = 1.25f;
      skybox.saturation  = 1.18f;
    }
  }

  // 地面の生成
  m_floorEntity = CreateEntity(ctx.world);
  auto &floorTr = ctx.world.Add<components::Transform>(m_floorEntity);
  floorTr.position = {0.0f, -0.5f, 0.0f};
  
  auto &floorMr  = ctx.world.Add<components::MeshRenderer>(m_floorEntity);
  game::systems::TerrainConfig tconf;
  tconf.worldWidth = 150.0f;
  tconf.worldDepth = 150.0f;
  tconf.resolutionX = 64;
  tconf.resolutionZ = 64;
  tconf.baseHeight = 0.0f;
  tconf.heightScale = 2.5f;
  tconf.biome = 0; // 草原
  
  auto tdata = game::systems::TerrainGenerator::GenerateTerrain("TitleSeed", {}, tconf);
  floorMr.mesh = ctx.resource.CreateDynamicMesh("TitleTerrain", tdata.vertices, tdata.indices);
  floorMr.shader = basicShader;
  floorMr.isVisible = true;
  floorMr.textureSRV = ctx.resource.LoadTextureSRV("Assets/textures/GRASS_BASE.png");
  floorMr.hasTexture = true;
  floorMr.customFlags.x = 30.0f; // UV Scale

  // 地球儀とティー台の生成
  m_globeEntity = CreateEntity(ctx.world);
  auto &globeTr = ctx.world.Add<components::Transform>(m_globeEntity);
  globeTr.position = {0.0f, 2.7f, 0.0f};
  globeTr.scale    = {3.0f, 3.0f, 3.0f}; // 大きく
  XMVECTOR gq = XMQuaternionRotationRollPitchYaw(XMConvertToRadians(15.0f), 0.0f, 0.0f);
  XMStoreFloat4(&globeTr.rotation, gq);

  auto &globeMr = ctx.world.Add<components::MeshRenderer>(m_globeEntity);
  globeMr.mesh = globeMesh;
  globeMr.shader = basicShader;
  globeMr.color = {0.95f, 0.95f, 0.98f, 1.0f}; // 少し白く
  globeMr.isVisible = true;

  // 白いティー（球体を縦に伸ばしてティーに見立てる）
  m_teeLoEntity = CreateEntity(ctx.world);
  auto &teeLoTr = ctx.world.Add<components::Transform>(m_teeLoEntity);
  teeLoTr.position = {0.0f, 0.5f, 0.0f};
  teeLoTr.scale    = {0.1f, 1.5f, 0.1f};
  auto &teeLoMr = ctx.world.Add<components::MeshRenderer>(m_teeLoEntity);
  teeLoMr.mesh = sphereMesh;
  teeLoMr.shader = basicShader;
  teeLoMr.color = {0.9f, 0.9f, 0.9f, 1.0f};
  teeLoMr.isVisible = true;

  // カメラの生成
  m_cameraEntity = CreateEntity(ctx.world);
  auto &camTr = ctx.world.Add<components::Transform>(m_cameraEntity);
  camTr.position = {0.0f, 4.2f, -12.0f}; // やや引き
  XMMATRIX view = XMMatrixLookAtLH(XMLoadFloat3(&camTr.position), XMVectorSet(0, 2.5f, 0, 0), XMVectorSet(0, 1, 0, 0));
  XMStoreFloat4(&camTr.rotation, XMQuaternionRotationMatrix(XMMatrixInverse(nullptr, view)));

  auto &cam = ctx.world.Add<components::Camera>(m_cameraEntity);
  cam.fov = XMConvertToRadians(45.0f);
  cam.isMainCamera = true;

  // UIレイヤーの生成

  // 背景の透かし文字の生成
  {
    auto e = CreateEntity(ctx.world);
    auto &t = ctx.world.Add<components::UIText>(e);
    t.text = L"W";
    t.x = -150.0f; t.y = -200.0f;
    t.style.fontFamily = "Times New Roman";
    t.style.fontSize = 700.0f;
    t.style.color = {1.0f, 1.0f, 1.0f, 0.05f}; // 非常に薄く
    t.layer = 1;
  }

  // メインタイトルロゴの生成
  {
    // 上部装飾線とボールアイコン
    auto eL = CreateEntity(ctx.world);
    auto &tL = ctx.world.Add<components::UIText>(eL);
    tL.text = L"────────────  ⚽  ────────────"; // 単色絵文字として描画される
    tL.x = 0.0f; tL.y = 40.0f;
    tL.style.fontSize = 18.0f;
    tL.style.color = {0.9f, 0.8f, 0.3f, 1.0f};
    tL.style.align = graphics::TextAlign::Center;
    tL.style.hasShadow = true;
    tL.style.shadowColor = {0, 0, 0, 0.6f};
    tL.width = 1280.0f;
    tL.layer = 50;

    auto e = CreateEntity(ctx.world);
    auto &t = ctx.world.Add<components::UIText>(e);
    t.text = L"WIKI GOLF";
    t.x = 0.0f; t.y = 60.0f;
    t.style.fontFamily = "Times New Roman";
    t.style.fontSize = 120.0f;
    t.style.color = {1.0f, 0.95f, 0.7f, 1.0f}; // ゴールド
    t.style.useGradient = true;
    t.style.bgGradientEnd = {0.8f, 0.7f, 0.2f, 1.0f}; // テキストカラーには効かないが予備
    t.style.align = graphics::TextAlign::Center;
    t.style.hasShadow = true;
    t.style.shadowColor = {0, 0, 0, 0.8f};
    t.style.shadowOffsetX = 4.0f;
    t.style.shadowOffsetY = 4.0f;
    t.style.hasOutline = true;
    t.style.outlineColor = {0.3f, 0.2f, 0.05f, 0.9f}; // 金枠エッジ風
    t.style.outlineWidth = 2.0f;
    t.width = 1280.0f;
    t.layer = 50;

    auto eS = CreateEntity(ctx.world);
    auto &tS = ctx.world.Add<components::UIText>(eS);
    tS.text = L"─ Wikipediaの記事リンクを辿って、目標ページへ到達せよ ─";
    tS.x = 0.0f; tS.y = 195.0f;
    tS.style.fontSize = 20.0f;
    tS.style.color = {1.0f, 1.0f, 1.0f, 1.0f};
    tS.style.align = graphics::TextAlign::Center;
    tS.style.hasShadow = true;
    tS.style.shadowColor = {0, 0, 0, 0.8f};
    tS.width = 1280.0f;
    tS.layer = 50;
  }

  // 左パネルの生成
  {
    // 背景カード (白・角丸)
    auto ep = CreateEntity(ctx.world);
    auto &tp = ctx.world.Add<components::UIText>(ep);
    tp.text = L""; 
    tp.x = 40.0f; tp.y = 260.0f;
    tp.width = 340.0f; tp.height = 410.0f; // 下パネル(y=680)と被らないように430->410へ短縮
    tp.style.bgColor = {0.98f, 0.98f, 0.98f, 0.95f};
    tp.style.cornerRadius = 16.0f;
    tp.style.hasShadow = true;
    tp.style.shadowColor = {0.0f, 0.0f, 0.0f, 0.4f};
    tp.style.shadowOffsetX = 4.0f;
    tp.style.shadowOffsetY = 4.0f;
    tp.layer = 10;

    // 「W」背景透かし (カード内)
    auto ew = CreateEntity(ctx.world);
    auto &tw = ctx.world.Add<components::UIText>(ew);
    tw.text = L"W";
    tw.x = 240.0f; tw.y = 260.0f;
    tw.style.fontFamily = "Times New Roman";
    tw.style.fontSize = 180.0f;
    tw.style.color = {0.9f, 0.9f, 0.9f, 0.8f};
    tw.layer = 11;

    // 見出し「ゴルフ」
    auto et = CreateEntity(ctx.world);
    auto &tt = ctx.world.Add<components::UIText>(et);
    tt.text = L"ゴルフ";
    tt.x = 65.0f; tt.y = 280.0f;
    tt.style.fontSize = 32.0f;
    tt.style.color = {0.1f, 0.1f, 0.1f, 1.0f};
    tt.style.fontFamily = "Times New Roman";
    tt.layer = 12;

    // 出典
    auto ec = CreateEntity(ctx.world);
    auto &tc = ctx.world.Add<components::UIText>(ec);
    tc.text = L"出典: フリー百科事典『ウィキペディア（Wikipedia）』";
    tc.x = 65.0f; tc.y = 325.0f;
    tc.style.fontSize = 11.0f;
    tc.style.color = {0.4f, 0.4f, 0.4f, 1.0f};
    tc.layer = 12;

    // サムネイル画像
    auto ei = CreateEntity(ctx.world);
    auto &ui = ctx.world.Add<components::UIImage>(ei);
    ui.texturePath = "Golfer_swing.jpg"; 
    ui.x = 65.0f; ui.y = 350.0f;
    ui.width = 120.0f; ui.height = 80.0f; // 左半分に配置
    ui.layer = 12;

    // 記事説明文
    auto ed = CreateEntity(ctx.world);
    auto &td = ctx.world.Add<components::UIText>(ed);
    td.text = L"ゴルフ（英: golf）とは、クラブを\n用いてボールを打ち、ホールに\n入れるまでの打数を競う球技\nである。";
    td.x = 195.0f; td.y = 350.0f;
    td.style.fontSize = 12.0f;
    td.style.color = {0.2f, 0.2f, 0.2f, 1.0f};
    td.layer = 12;

    // ミッション情報 (Start / Goal / Par)
    const wchar_t* missionLabels[] = { L"⚑   Start Page:", L"🎯   Goal Page:", L"🔗   Par:" };
    const wchar_t* missionValues[] = { L"ゴルフ", L"Wikipedia", L"5 Links" };
    for(int i=0; i<3; ++i) {
        auto el = CreateEntity(ctx.world);
        auto &tl = ctx.world.Add<components::UIText>(el);
        tl.text = missionLabels[i];
        tl.x = 65.0f; tl.y = 450.0f + i * 35.0f;
        tl.style.fontSize = 16.0f;
        tl.style.color = {0.3f, 0.3f, 0.3f, 1.0f};
        tl.layer = 12;

        auto ev = CreateEntity(ctx.world);
        auto &tv = ctx.world.Add<components::UIText>(ev);
        tv.text = missionValues[i];
        tv.x = 220.0f; tv.y = 450.0f + i * 35.0f;
        tv.style.fontSize = 18.0f;
        tv.style.color = {0.06f, 0.24f, 0.58f, 1.0f}; // リンク青
        tv.style.fontFamily = "Times New Roman";
        tv.layer = 12;
    }

    // 関連項目見出し
    auto rl = CreateEntity(ctx.world);
    auto &trl = ctx.world.Add<components::UIText>(rl);
    trl.text = L"関連項目";
    trl.x = 65.0f; trl.y = 575.0f;
    trl.style.fontSize = 14.0f;
    trl.style.color = {0.2f, 0.2f, 0.2f, 1.0f};
    trl.layer = 12;

    // 関連項目タグ
    const wchar_t* tags[] = { L"スポーツ", L"球技", L"レジャー", L"オリンピック" };
    float tagX = 65.0f;
    for (int i = 0; i < 4; ++i) {
        auto etag = CreateEntity(ctx.world);
        auto &ttag = ctx.world.Add<components::UIText>(etag);
        ttag.text = tags[i];
        ttag.x = tagX; ttag.y = 605.0f;
        ttag.style.fontSize = 12.0f;
        ttag.style.color = {0.06f, 0.24f, 0.58f, 1.0f};
        ttag.style.bgColor = {0.95f, 0.95f, 0.98f, 1.0f};
        ttag.style.borderColor = {0.7f, 0.8f, 0.9f, 1.0f};
        ttag.style.borderWidth = 1.0f;
        ttag.style.cornerRadius = 4.0f;
        ttag.layer = 12;
        // タグの幅を概算してXをずらす
        tagX += wcslen(tags[i]) * 13.0f + 30.0f;
    }
  }

  // 右パネルの生成
  {
    // メニュー背景 (濃紺・半透明・金枠)
    auto ep = CreateEntity(ctx.world);
    auto &tp = ctx.world.Add<components::UIText>(ep);
    tp.text = L"";
    tp.x = 880.0f; tp.y = 260.0f;
    tp.width = 360.0f; tp.height = 410.0f;
    tp.style.bgColor = {0.04f, 0.10f, 0.18f, 0.85f};
    tp.style.cornerRadius = 16.0f;
    tp.style.borderWidth = 2.0f;
    tp.style.borderColor = {0.8f, 0.7f, 0.3f, 1.0f};
    tp.style.hasShadow = true;
    tp.layer = 10;

    // 各メニュー項目を UIButton コンポーネントで定義
    bool tutorialDone = std::filesystem::exists("save_tutorial_done.flag");
    const struct { const wchar_t* label; const char* action; } menuItems[] = {
        {L"▶  はじめから",   "new_game"},
        {tutorialDone ? L"🎓  チュートリアル" : L"🎓  チュートリアル (NEW!)", "tutorial"},
        {L"↺  デイリーチャレンジ", "daily"},
        {L"⚑  コース選択",   "course"},
        {L"⚙  オプション",     "option"},
        {L"🚪  終了",         "exit"},
    };

    for (int i = 0; i < 6; ++i) {
      auto eb = CreateEntity(ctx.world);
      auto &btn = ctx.world.Add<components::UIButton>(eb);
      btn.label = menuItems[i].label;
      btn.action = menuItems[i].action;
      btn.x = 890.0f;
      btn.y = 272.0f + i * 60.0f;
      btn.width = 340.0f;
      btn.height = 48.0f;
      btn.visible = true;
      // 通常時: 透明
      btn.normalColor  = {0.0f,  0.0f,  0.0f,  0.0f};
      // ホバー時: ゴールド
      btn.hoverColor   = {1.0f,  0.85f, 0.3f,  0.9f};
      // プレス時: 濃い金色
      btn.pressedColor = {0.8f,  0.6f,  0.1f,  1.0f};
      btn.textStyle.fontSize = 26.0f;
      btn.textStyle.color = {0.95f, 0.95f, 0.95f, 1.0f};
      btn.textStyle.align = graphics::TextAlign::Left;

      if (i == 1 && !tutorialDone) {
          btn.normalColor  = {0.8f,  0.6f,  0.1f,  0.2f};
          btn.textStyle.color = {1.0f, 0.9f, 0.3f, 1.0f};
      }

      // セパレータ（最後以外）
      if (i < 5) {
        auto es = CreateEntity(ctx.world);
        auto &ts = ctx.world.Add<components::UIText>(es);
        ts.text = L"────────────────────────";
        ts.x = 905.0f; ts.y = 272.0f + i * 60.0f + 44.0f;
        ts.style.fontSize = 12.0f;
        ts.style.color = {0.3f, 0.4f, 0.5f, 0.5f};
        ts.layer = 11;
      }

      m_menuEntries.push_back({eb, 0, menuItems[i].label, btn.y, false});
    }

    // Go to Wikipedia リンク
    auto eLink = CreateEntity(ctx.world);
    auto &btnLink = ctx.world.Add<components::UIButton>(eLink);
    btnLink.label  = L"Go to Wikipedia  🔗";
    btnLink.action = "wikipedia";
    btnLink.x = 890.0f;
    btnLink.y = 615.0f;
    btnLink.width  = 340.0f;
    btnLink.height = 36.0f;
    btnLink.visible = true;
    btnLink.normalColor  = {0.0f, 0.0f, 0.0f, 0.0f};
    btnLink.hoverColor   = {0.0f, 0.0f, 0.0f, 0.0f};
    btnLink.pressedColor = {0.0f, 0.0f, 0.0f, 0.0f};
    btnLink.textStyle.fontSize = 20.0f;
    btnLink.textStyle.color = {0.4f, 0.6f, 0.9f, 1.0f};
    btnLink.textStyle.align = graphics::TextAlign::Center;
    m_menuEntries.push_back({eLink, 0, L"Go to Wikipedia", btnLink.y, false});
  }

  // 下部ナビゲーションの生成
  {
    // 半透明帯
    auto eb = CreateEntity(ctx.world);
    auto &tb = ctx.world.Add<components::UIText>(eb);
    tb.text = L"";
    tb.x = 0.0f; tb.y = 680.0f;
    tb.width = 1280.0f; tb.height = 40.0f;
    tb.style.bgColor = {0.0f, 0.0f, 0.0f, 0.5f};
    tb.layer = 50;

    // オンラインランキング
    auto e1 = CreateEntity(ctx.world);
    auto &t1 = ctx.world.Add<components::UIButton>(e1);
    t1.label = L"🌐 オンラインランキング";
    t1.action = "ranking";
    t1.x = 450.0f; t1.y = 685.0f;
    t1.width = 180.0f; t1.height = 30.0f;
    t1.textStyle.fontSize = 16.0f;
    t1.textStyle.color = {0.9f, 0.9f, 0.9f, 1.0f};
    t1.textStyle.align = graphics::TextAlign::Center;
    t1.normalColor = {1.0f, 1.0f, 1.0f, 0.1f};
    t1.hoverColor = {1.0f, 1.0f, 1.0f, 0.3f};
    t1.pressedColor = {0.8f, 0.8f, 0.8f, 0.4f};

    // 実績
    auto e2 = CreateEntity(ctx.world);
    auto &t2 = ctx.world.Add<components::UIButton>(e2);
    t2.label = L"🏆 実績";
    t2.action = "achievement";
    t2.x = 650.0f; t2.y = 685.0f;
    t2.width = 120.0f; t2.height = 30.0f;
    t2.textStyle.fontSize = 16.0f;
    t2.textStyle.color = {0.9f, 0.9f, 0.9f, 1.0f};
    t2.textStyle.align = graphics::TextAlign::Center;
    t2.normalColor = {1.0f, 1.0f, 1.0f, 0.1f};
    t2.hoverColor = {1.0f, 1.0f, 1.0f, 0.3f};
    t2.pressedColor = {0.8f, 0.8f, 0.8f, 0.4f};

    // コピーライト
    auto ec = CreateEntity(ctx.world);
    auto &tc = ctx.world.Add<components::UIText>(ec);
    tc.text = L"©WikiGolf v1.0.0  |  CC BY-SA 4.0";
    tc.x = 860.0f; tc.y = 690.0f;
    tc.style.fontSize = 14.0f;
    tc.style.color = {0.6f, 0.6f, 0.6f, 1.0f};
    tc.style.align = graphics::TextAlign::Right;
    tc.width = 400.0f;
    tc.layer = 51;
  }

  // ポップアップUIの生成
  m_popupTimer = 0.0f;
  
  m_popupBgEntity = CreateEntity(ctx.world);
  auto &pbg = ctx.world.Add<components::UIText>(m_popupBgEntity);
  pbg.text = L"";
  pbg.x = 440.0f; pbg.y = 300.0f;
  pbg.width = 400.0f; pbg.height = 120.0f;
  pbg.style.bgColor = {0.15f, 0.2f, 0.3f, 0.0f}; // アルファ0
  pbg.style.cornerRadius = 16.0f;
  pbg.style.borderWidth = 2.0f;
  pbg.style.borderColor = {0.9f, 0.85f, 0.3f, 0.0f};
  pbg.style.hasShadow = true;
  pbg.layer = 100;
  pbg.visible = false;

  m_popupTextEntity = CreateEntity(ctx.world);
  auto &ptxt = ctx.world.Add<components::UIText>(m_popupTextEntity);
  ptxt.text = L"Coming Soon...\n\n現在開発中です";
  ptxt.x = 440.0f; ptxt.y = 330.0f;
  ptxt.width = 400.0f;
  ptxt.style.fontSize = 22.0f;
  ptxt.style.color = {1.0f, 1.0f, 1.0f, 0.0f}; // アルファ0
  ptxt.style.align = graphics::TextAlign::Center;
  ptxt.layer = 101;
  ptxt.visible = false;

  CreateCourseSelectUI(ctx);

}


/**
 * @brief シーンの毎フレーム更新処理を行います。
 */
void TitleScene::OnUpdate(core::GameContext &ctx) {
  if (m_state == TitleState::IntroVideo) {
    if (m_videoPlayer) {
      m_videoPlayer->Update(ctx.graphics.GetContext(), ctx.dt);

      // 動画再生とロードが完了したか確認
      bool loadReady = m_startupLoadTask.valid() ? (m_startupLoadTask.wait_for(std::chrono::seconds(0)) == std::future_status::ready) : true;
      if (m_videoPlayer->IsFinished() && loadReady) {
        m_state = TitleState::MainMenu;
        StopIntroAudio(ctx);
        m_videoPlayer->Stop();
        m_videoPlayer.reset();
        FinalizeStartupLoad(ctx);
      }
    } else {
      // 動画再生に失敗した場合のフォールバック
      bool loadReady = m_startupLoadTask.valid() ? (m_startupLoadTask.wait_for(std::chrono::seconds(0)) == std::future_status::ready) : true;
      if (loadReady) {
        m_state = TitleState::MainMenu;
        StopIntroAudio(ctx);
        FinalizeStartupLoad(ctx);
      }
    }
    return;
  }

  m_time += ctx.dt;

  auto *skybox = ctx.world.Get<components::Skybox>(m_skyboxEntity);
  if (skybox) {
    skybox->time = m_time;
  }

  auto *globeTr = ctx.world.Get<components::Transform>(m_globeEntity);
  if (globeTr) {
    XMVECTOR gq = XMQuaternionRotationRollPitchYaw(XMConvertToRadians(15.0f), m_time * 0.25f, 0.0f);
    XMStoreFloat4(&globeTr->rotation, gq);
  }

  // UIButton の状態をポーリングしてアクションを処理
  bool newGame = false;
  bool startTutorial = false;
  bool exitGame = false;
  bool prevHoveredAny = false;

  if (m_state == TitleState::CourseSelect) {
    UpdateCourseSelect(ctx);
  } else {
    // メインメニュー状態の更新
    ctx.world.Query<components::UIButton>().Each([&](ecs::Entity, components::UIButton &btn) {
    if (!btn.visible) return;

    bool isHovered = (btn.state == components::ButtonState::Hovered ||
                     btn.state == components::ButtonState::Pressed);

    // ホバー内入時にSE
    if (isHovered && !prevHoveredAny) {
      prevHoveredAny = true;
    }

    // ホバー状態に応じてリンクのテキスト色を変更
    if (btn.action == "wikipedia") {
      btn.textStyle.color = isHovered
          ? DirectX::XMFLOAT4{0.2f, 0.5f, 1.0f, 1.0f}   // ホバー時: 明るい青
          : DirectX::XMFLOAT4{0.4f, 0.6f, 0.9f, 1.0f};  // 通常時: 青
    } else {
      // ホバー時は黒文字、通常時は白文字
      btn.textStyle.color = isHovered
          ? DirectX::XMFLOAT4{0.05f, 0.05f, 0.05f, 1.0f}  // 黒
          : DirectX::XMFLOAT4{0.95f, 0.95f, 0.95f, 1.0f}; // 白
    }

    // クリック判定 (Pressed 状態の瞬間をトリガーとする)
    if (btn.state == components::ButtonState::Pressed && ctx.input.GetMouseButtonDown(0)) {
      if (btn.action == "new_game") {
        if (ctx.audio) ctx.audio->PlaySE(ctx, "se_shot_hard.mp3", 0.5f);
        newGame = true;
      } else if (btn.action == "tutorial") {
        if (ctx.audio) ctx.audio->PlaySE(ctx, "se_shot_soft.mp3", 0.5f);
        startTutorial = true;
      } else if (btn.action == "course") {
        if (ctx.audio) ctx.audio->PlaySE(ctx, "se_shot_soft.mp3", 0.5f);
        m_state = TitleState::CourseSelect;
        SetMainMenuVisible(ctx, false);
        SetCourseSelectVisible(ctx, true);
        m_focusIndex = 0;
      } else if (btn.action == "daily" || btn.action == "option" || btn.action == "ranking" || btn.action == "achievement") {
        if (ctx.audio) ctx.audio->PlaySE(ctx, "se_cancel.mp3", 0.5f);
        m_popupTimer = 2.0f; // 2秒間表示
      } else if (btn.action == "exit") {
        exitGame = true;
      } else if (btn.action == "wikipedia") {
        if (ctx.audio) ctx.audio->PlaySE(ctx, "se_shot_soft.mp3", 0.3f);
        ShellExecuteA(nullptr, "open", "https://ja.wikipedia.org/wiki/%E3%82%B4%E3%83%AB%E3%83%95",
                      nullptr, nullptr, SW_SHOW);
      }
    }
  });
  } // else (MainMenu)

  if (newGame) {
    StopIntroAudio(ctx);
    auto loadingScene = std::make_unique<LoadingScene>([]() { return std::make_unique<WikiGolfScene>(false); });
    ctx.sceneManager->ChangeScene(std::move(loadingScene));
  }
  if (startTutorial) {
    StopIntroAudio(ctx);
    ctx.sceneManager->ChangeScene(std::make_unique<WikiGolfScene>(true));
  }
  if (exitGame) {
    StopIntroAudio(ctx);
    ctx.shouldClose = true;
  }

  // ポップアップの更新
  if (m_popupTimer > 0.0f) {
    m_popupTimer -= ctx.dt;
    float alpha = std::min(m_popupTimer * 2.0f, 1.0f); // 残り0.5秒でフェードアウト
    if (m_popupTimer > 1.5f) {
      alpha = (2.0f - m_popupTimer) * 2.0f; // 最初の0.5秒でフェードイン
    }
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    
    auto *pbg = ctx.world.Get<components::UIText>(m_popupBgEntity);
    auto *ptxt = ctx.world.Get<components::UIText>(m_popupTextEntity);
    if (pbg && ptxt) {
      pbg->visible = true;
      ptxt->visible = true;
      pbg->style.bgColor.w = alpha * 0.95f;
      pbg->style.borderColor.w = alpha;
      ptxt->style.color.w = alpha;
    }
  } else {
    auto *pbg = ctx.world.Get<components::UIText>(m_popupBgEntity);
    auto *ptxt = ctx.world.Get<components::UIText>(m_popupTextEntity);
    if (pbg && ptxt) {
      pbg->visible = false;
      ptxt->visible = false;
    }
  }
}

/**
 * @brief シーンの描画処理を行います。
 */
void TitleScene::Render(core::GameContext &ctx) {
  if (m_state != TitleState::IntroVideo || !m_videoPlayer || !ctx.textRenderer) {
    return;
  }

  auto *srv = m_videoPlayer->GetSRV();
  if (!srv) {
    return;
  }

  ctx.textRenderer->BeginDraw();
  D2D1_RECT_F rect = {0, 0, ctx.textRenderer->GetWidth(), ctx.textRenderer->GetHeight()};
  ctx.textRenderer->RenderImage(srv, rect);
  ctx.textRenderer->EndDraw();
}

/**
 * @brief シーンを抜ける際の後処理を行います。
 */
void TitleScene::OnExit(core::GameContext &ctx) {
  LOG_INFO("TitleScene", "OnExit");
  if (ctx.audio) {
    ctx.audio->StopBGM();
  }
  StopIntroAudio(ctx);
  if (m_videoPlayer) {
    m_videoPlayer->Stop();
    m_videoPlayer.reset();
  }
  DestroyAllEntities(ctx);
  m_menuEntries.clear();
}

void TitleScene::StopIntroAudio(core::GameContext &ctx) {
  if (ctx.audio) {
    ctx.audio->StopOneShot(kIntroAudioLabel);
  }
}

/**
 * @brief コース選択用UIを生成します。
 */
void TitleScene::CreateCourseSelectUI(core::GameContext& ctx) {
  // 背景半透明パネル
  m_csBgEntity = CreateEntity(ctx.world);
  auto& bg = ctx.world.Add<components::UIText>(m_csBgEntity);
  bg.text = L""; bg.x = 240.0f; bg.y = 100.0f; bg.width = 800.0f; bg.height = 520.0f;
  bg.style.bgColor = {0.05f, 0.1f, 0.15f, 0.95f}; bg.style.cornerRadius = 16.0f;
  bg.style.borderWidth = 2.0f; bg.style.borderColor = {0.8f, 0.7f, 0.3f, 1.0f};
  bg.layer = 200; bg.visible = false;

  // タイトル
  m_csTitleEntity = CreateEntity(ctx.world);
  auto& title = ctx.world.Add<components::UIText>(m_csTitleEntity);
  title.text = L"コース選択"; title.x = 240.0f; title.y = 120.0f; title.width = 800.0f;
  title.style.fontSize = 32.0f; title.style.color = {1.0f, 0.95f, 0.7f, 1.0f};
  title.style.align = graphics::TextAlign::Center;
  title.layer = 201; title.visible = false;

  // スタート入力枠
  m_startInputBg = CreateEntity(ctx.world);
  auto& stBg = ctx.world.Add<components::UIText>(m_startInputBg);
  stBg.text = L"Start Page:"; stBg.x = 280.0f; stBg.y = 180.0f; stBg.width = 650.0f; stBg.height = 40.0f;
  stBg.style.fontSize = 20.0f; stBg.style.color = {0.7f, 0.7f, 0.7f, 1.0f};
  stBg.style.bgColor = {0.0f, 0.0f, 0.0f, 0.8f}; stBg.style.cornerRadius = 8.0f;
  stBg.style.borderWidth = 1.0f; stBg.style.borderColor = {0.5f, 0.5f, 0.5f, 1.0f};
  stBg.layer = 201; stBg.visible = false;

  m_startInputText = CreateEntity(ctx.world);
  auto& stTxt = ctx.world.Add<components::UIText>(m_startInputText);
  stTxt.text = L""; stTxt.x = 420.0f; stTxt.y = 188.0f; stTxt.width = 500.0f;
  stTxt.style.fontSize = 20.0f; stTxt.style.color = {1.0f, 1.0f, 1.0f, 1.0f};
  stTxt.layer = 202; stTxt.visible = false;

  // スタート貼り付けボタン
  m_startPasteBtn = CreateEntity(ctx.world);
  auto& stPaste = ctx.world.Add<components::UIButton>(m_startPasteBtn);
  stPaste.label = L"📋 貼付"; stPaste.action = "cs_paste_start";
  stPaste.x = 940.0f; stPaste.y = 180.0f; stPaste.width = 80.0f; stPaste.height = 40.0f;
  stPaste.textStyle.fontSize = 18.0f; stPaste.textStyle.align = graphics::TextAlign::Center;
  stPaste.normalColor = {0.2f, 0.2f, 0.3f, 1.0f}; stPaste.hoverColor = {0.3f, 0.3f, 0.5f, 1.0f};
  stPaste.pressedColor = {0.1f, 0.1f, 0.2f, 1.0f};
  stPaste.visible = false;

  // ゴール入力枠
  m_goalInputBg = CreateEntity(ctx.world);
  auto& glBg = ctx.world.Add<components::UIText>(m_goalInputBg);
  glBg.text = L"Goal Page:"; glBg.x = 280.0f; glBg.y = 240.0f; glBg.width = 650.0f; glBg.height = 40.0f;
  glBg.style.fontSize = 20.0f; glBg.style.color = {0.7f, 0.7f, 0.7f, 1.0f};
  glBg.style.bgColor = {0.0f, 0.0f, 0.0f, 0.8f}; glBg.style.cornerRadius = 8.0f;
  glBg.style.borderWidth = 1.0f; glBg.style.borderColor = {0.5f, 0.5f, 0.5f, 1.0f};
  glBg.layer = 201; glBg.visible = false;

  m_goalInputText = CreateEntity(ctx.world);
  auto& glTxt = ctx.world.Add<components::UIText>(m_goalInputText);
  glTxt.text = L""; glTxt.x = 420.0f; glTxt.y = 248.0f; glTxt.width = 500.0f;
  glTxt.style.fontSize = 20.0f; glTxt.style.color = {1.0f, 1.0f, 1.0f, 1.0f};
  glTxt.layer = 202; glTxt.visible = false;

  // ゴール貼り付けボタン
  m_goalPasteBtn = CreateEntity(ctx.world);
  auto& glPaste = ctx.world.Add<components::UIButton>(m_goalPasteBtn);
  glPaste.label = L"📋 貼付"; glPaste.action = "cs_paste_goal";
  glPaste.x = 940.0f; glPaste.y = 240.0f; glPaste.width = 80.0f; glPaste.height = 40.0f;
  glPaste.textStyle.fontSize = 18.0f; glPaste.textStyle.align = graphics::TextAlign::Center;
  glPaste.normalColor = {0.2f, 0.2f, 0.3f, 1.0f}; glPaste.hoverColor = {0.3f, 0.3f, 0.5f, 1.0f};
  glPaste.pressedColor = {0.1f, 0.1f, 0.2f, 1.0f};
  glPaste.visible = false;

  // プレビュー領域
  m_previewBg = CreateEntity(ctx.world);
  auto& prBg = ctx.world.Add<components::UIText>(m_previewBg);
  prBg.text = L""; prBg.x = 280.0f; prBg.y = 300.0f; prBg.width = 740.0f; prBg.height = 220.0f;
  prBg.style.bgColor = {0.0f, 0.0f, 0.0f, 0.6f}; prBg.style.cornerRadius = 8.0f;
  prBg.layer = 201; prBg.visible = false;

  m_previewText = CreateEntity(ctx.world);
  auto& prTxt = ctx.world.Add<components::UIText>(m_previewText);
  prTxt.text = L"ここにプレビューが表示されます\n（未確認）"; prTxt.x = 300.0f; prTxt.y = 320.0f; prTxt.width = 700.0f;
  prTxt.style.fontSize = 16.0f; prTxt.style.color = {0.8f, 0.8f, 0.8f, 1.0f};
  prTxt.layer = 202; prTxt.visible = false;

  // ボタン類
  m_checkBtn = CreateEntity(ctx.world);
  auto& chk = ctx.world.Add<components::UIButton>(m_checkBtn);
  chk.label = L"疎通確認"; chk.action = "cs_check";
  chk.x = 280.0f; chk.y = 510.0f; chk.width = 200.0f; chk.height = 50.0f;
  chk.textStyle.fontSize = 24.0f; chk.textStyle.align = graphics::TextAlign::Center;
  chk.normalColor = {0.1f, 0.3f, 0.6f, 1.0f}; chk.hoverColor = {0.2f, 0.5f, 0.9f, 1.0f}; chk.pressedColor = {0.1f, 0.2f, 0.5f, 1.0f};
  chk.visible = false;

  m_startBtn = CreateEntity(ctx.world);
  auto& st = ctx.world.Add<components::UIButton>(m_startBtn);
  st.label = L"スタート (確認未)"; st.action = "cs_start";
  st.x = 540.0f; st.y = 510.0f; st.width = 200.0f; st.height = 50.0f;
  st.textStyle.fontSize = 20.0f; st.textStyle.align = graphics::TextAlign::Center;
  st.normalColor = {0.3f, 0.3f, 0.3f, 1.0f}; st.hoverColor = {0.3f, 0.3f, 0.3f, 1.0f}; st.pressedColor = {0.3f, 0.3f, 0.3f, 1.0f};
  st.state = components::ButtonState::Disabled;
  st.visible = false;

  m_closeBtn = CreateEntity(ctx.world);
  auto& cls = ctx.world.Add<components::UIButton>(m_closeBtn);
  cls.label = L"閉じる"; cls.action = "cs_close";
  cls.x = 800.0f; cls.y = 510.0f; cls.width = 200.0f; cls.height = 50.0f;
  cls.textStyle.fontSize = 24.0f; cls.textStyle.align = graphics::TextAlign::Center;
  cls.normalColor = {0.6f, 0.2f, 0.2f, 1.0f}; cls.hoverColor = {0.8f, 0.3f, 0.3f, 1.0f}; cls.pressedColor = {0.5f, 0.1f, 0.1f, 1.0f};
  cls.visible = false;
}

/**
 * @brief メインメニューの表示状態を切り替えます。
 */
void TitleScene::SetMainMenuVisible(core::GameContext& ctx, bool visible) {
  ctx.world.Query<components::UIButton>().Each([&](ecs::Entity, components::UIButton &btn) {
    if (btn.action != "cs_check" && btn.action != "cs_start" && btn.action != "cs_close" && btn.action != "cs_paste_start" && btn.action != "cs_paste_goal") {
      btn.visible = visible;
    }
  });
}

/**
 * @brief コース選択UIの表示状態を切り替えます。
 */
void TitleScene::SetCourseSelectVisible(core::GameContext& ctx, bool visible) {
  if (auto* bg = ctx.world.Get<components::UIText>(m_csBgEntity)) bg->visible = visible;
  if (auto* title = ctx.world.Get<components::UIText>(m_csTitleEntity)) title->visible = visible;
  if (auto* stBg = ctx.world.Get<components::UIText>(m_startInputBg)) stBg->visible = visible;
  if (auto* stTxt = ctx.world.Get<components::UIText>(m_startInputText)) stTxt->visible = visible;
  if (auto* glBg = ctx.world.Get<components::UIText>(m_goalInputBg)) glBg->visible = visible;
  if (auto* glTxt = ctx.world.Get<components::UIText>(m_goalInputText)) glTxt->visible = visible;
  if (auto* prevBg = ctx.world.Get<components::UIText>(m_previewBg)) prevBg->visible = visible;
  if (auto* prevTxt = ctx.world.Get<components::UIText>(m_previewText)) prevTxt->visible = visible;

  ctx.world.Query<components::UIButton>().Each([&](ecs::Entity, components::UIButton &btn) {
    if (btn.action == "cs_check" || btn.action == "cs_start" || btn.action == "cs_close" || btn.action == "cs_paste_start" || btn.action == "cs_paste_goal") {
      btn.visible = visible;
    }
  });
}

/**
 * @brief コース選択UIの毎フレーム更新処理を行います。
 */
void TitleScene::UpdateCourseSelect(core::GameContext& ctx) {
  // フォーカス切り替え（マウスクリック）
  if (ctx.input.GetMouseButtonDown(0)) {
    auto mousePos = ctx.input.GetMousePosition();
    float mx = (float)mousePos.x;
    float my = (float)mousePos.y;

    if (mx >= 280 && mx <= 930 && my >= 180 && my <= 220) m_focusIndex = 1;
    else if (mx >= 280 && mx <= 930 && my >= 240 && my <= 280) m_focusIndex = 2;
    else {
      // UIButton 以外の場所をクリックしたらフォーカス外す処理
      bool onButton = false;
      ctx.world.Query<components::UIButton>().Each([&](ecs::Entity, components::UIButton &btn) {
        if (btn.visible && mx >= btn.x && mx <= btn.x + btn.width && my >= btn.y && my <= btn.y + btn.height) {
          onButton = true;
        }
      });
      if (!onButton) m_focusIndex = 0;
    }
  }

  // 入力反映
  if (m_focusIndex == 1 || m_focusIndex == 2) {
    std::wstring& targetStr = (m_focusIndex == 1) ? m_startString : m_goalString;
    const std::wstring& inChars = ctx.input.GetInputChars();
    if (ctx.input.GetBackspacePressed() && !targetStr.empty()) {
      targetStr.pop_back();
      m_readyToStart = false; // 変更があったら再確認
    }
    if (!inChars.empty()) {
      targetStr += inChars;
      m_readyToStart = false;
    }

    if (auto* bg1 = ctx.world.Get<components::UIText>(m_startInputBg)) bg1->style.borderColor = (m_focusIndex == 1) ? DirectX::XMFLOAT4{1,1,1,1} : DirectX::XMFLOAT4{0.5f,0.5f,0.5f,1};
    if (auto* bg2 = ctx.world.Get<components::UIText>(m_goalInputBg)) bg2->style.borderColor = (m_focusIndex == 2) ? DirectX::XMFLOAT4{1,1,1,1} : DirectX::XMFLOAT4{0.5f,0.5f,0.5f,1};
  }

  // カーソル点滅表示
  std::wstring cursor = ((int)(ctx.time * 2.0f) % 2 == 0) ? L"_" : L"";
  if (auto* t1 = ctx.world.Get<components::UIText>(m_startInputText)) t1->text = m_startString + ((m_focusIndex == 1) ? cursor : L"");
  if (auto* t2 = ctx.world.Get<components::UIText>(m_goalInputText)) t2->text = m_goalString + ((m_focusIndex == 2) ? cursor : L"");

  // 非同期確認完了のチェック
  if (m_checking && m_checkTask.valid() && m_checkTask.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
    m_checking = false;
    std::string result = m_checkTask.get();
    if (auto* p = ctx.world.Get<components::UIText>(m_previewText)) {
      if (!result.empty() && result != "ERROR" && result != "(Failed to fetch extract)") {
        LOG_INFO("TitleScene", "Course check success.");
        p->text = core::ToWString(result);
        m_readyToStart = true;
      } else {
        LOG_ERROR("TitleScene", "Course check failed.");
        p->text = L"ページが見つからないか、通信エラーが発生しました。";
        m_readyToStart = false;
      }
    }
    if (auto* stBtn = ctx.world.Get<components::UIButton>(m_startBtn)) {
      if (m_readyToStart) {
        stBtn->label = L"スタート！";
        stBtn->state = components::ButtonState::Normal;
        stBtn->normalColor = {0.2f, 0.7f, 0.2f, 1.0f};
        stBtn->hoverColor = {0.3f, 0.9f, 0.3f, 1.0f};
      } else {
        stBtn->label = L"スタート (確認未)";
        stBtn->state = components::ButtonState::Disabled;
        stBtn->normalColor = {0.3f, 0.3f, 0.3f, 1.0f};
      }
    }
  }

  // ボタン処理
  bool doClose = false;
  bool doStart = false;
  ctx.world.Query<components::UIButton>().Each([&](ecs::Entity, components::UIButton &btn) {
    if (!btn.visible || btn.state == components::ButtonState::Disabled) return;
    if (btn.state == components::ButtonState::Pressed && ctx.input.GetMouseButtonDown(0)) {
      if (btn.action == "cs_close") {
        if (ctx.audio) ctx.audio->PlaySE(ctx, "se_cancel.mp3", 0.5f);
        doClose = true;
      } else if (btn.action == "cs_paste_start") {
        std::wstring cb = ctx.input.GetClipboardText();
        cb.erase(std::remove(cb.begin(), cb.end(), L'\r'), cb.end());
        cb.erase(std::remove(cb.begin(), cb.end(), L'\n'), cb.end());
        m_startString = cb;
        m_focusIndex = 1;
        m_readyToStart = false;
      } else if (btn.action == "cs_paste_goal") {
        std::wstring cb = ctx.input.GetClipboardText();
        cb.erase(std::remove(cb.begin(), cb.end(), L'\r'), cb.end());
        cb.erase(std::remove(cb.begin(), cb.end(), L'\n'), cb.end());
        m_goalString = cb;
        m_focusIndex = 2;
        m_readyToStart = false;
      } else if (btn.action == "cs_check") {
        if (ctx.audio) ctx.audio->PlaySE(ctx, "se_shot_soft.mp3", 0.5f);
        if (!m_checking && !m_startString.empty() && !m_goalString.empty()) {
          std::string startUtf8 = ExtractWikiTitle(core::ToString(m_startString));
          std::string goalUtf8 = ExtractWikiTitle(core::ToString(m_goalString));

          if (startUtf8 == goalUtf8) {
            if (auto* p = ctx.world.Get<components::UIText>(m_previewText)) p->text = L"スタートとゴールに同じ記事は指定できません。";
            m_readyToStart = false;
          } else {
            m_checking = true;
            if (auto* p = ctx.world.Get<components::UIText>(m_previewText)) p->text = L"確認中...";
            m_checkTask = std::async(std::launch::async, [startUtf8, goalUtf8]() {
              auto trimW = [](const std::string& str, size_t maxLen) {
                std::wstring w = core::ToWString(str);
                if (w.length() <= maxLen) return core::ToString(w);
                return core::ToString(w.substr(0, maxLen) + L"...");
              };
              game::systems::WikiClient wiki;
              std::string extStart = wiki.FetchPageExtract(startUtf8, 100);
              if (extStart.empty() || extStart == "ERROR" || extStart == "(Failed to fetch extract)") return std::string("ERROR");
              std::string extGoal = wiki.FetchPageExtract(goalUtf8, 100);
              if (extGoal.empty() || extGoal == "ERROR" || extGoal == "(Failed to fetch extract)") return std::string("ERROR");
              return "【" + startUtf8 + "】\n" + trimW(extStart, 80) + "\n\n【" + goalUtf8 + "】\n" + trimW(extGoal, 80);
            });
          }
        } else if (m_startString.empty() || m_goalString.empty()) {
            if (auto* p = ctx.world.Get<components::UIText>(m_previewText)) p->text = L"スタートとゴールの両方を入力してください。";
        }
      } else if (btn.action == "cs_start" && m_readyToStart) {
        if (ctx.audio) ctx.audio->PlaySE(ctx, "se_shot_hard.mp3", 0.5f);
        doStart = true;
      }
    }
  });

  if (doClose) {
    SetCourseSelectVisible(ctx, false);
    SetMainMenuVisible(ctx, true);
    m_state = TitleState::MainMenu;
    m_focusIndex = 0;
  }
  if (doStart) {
    StopIntroAudio(ctx);
    game::components::WikiGlobalData data;
    data.startPage = ExtractWikiTitle(core::ToString(m_startString));
    data.targetPage = ExtractWikiTitle(core::ToString(m_goalString));
    data.targetPageId = -1;
    data.isUserOverride = true;
    ctx.world.SetGlobal(std::move(data));

    LOG_INFO("TitleScene", "Starting game with Start: '{}', Goal: '{}'", ExtractWikiTitle(core::ToString(m_startString)), ExtractWikiTitle(core::ToString(m_goalString)));

    auto loadingScene = std::make_unique<LoadingScene>([]() { return std::make_unique<WikiGolfScene>(); });
    ctx.sceneManager->ChangeScene(std::move(loadingScene));
  }
}

} // namespace game::scenes
