/**
 * @file TitleSceneStartup.cpp
 * @brief TitleSceneStartup の実装
*/

#include "ResultScene.h"
#include "TitleScene.h"
#include "TitleSceneSupport.h"
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
#include "SettingsScene.h"
#include "WikiGolfScene.h"
#include <filesystem>
#include <fstream>
#include <thread>
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <windows.h>
#include <shellapi.h> // ShellExecuteA用


namespace game::scenes {

using namespace DirectX;

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
    const wchar_t* missionLabels[] = { L"⚑   Start Page:", L"   Goal Page:", L"   Par:" };
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
    const wchar_t* tutorialLabel = L"  チュートリアル (NEW!)";
    if (tutorialDone) {
      tutorialLabel = L"  チュートリアル";
    }
    const struct { const wchar_t* label; const char* action; } menuItems[] = {
        {L"▶  はじめから",   "new_game"},
        {tutorialLabel, "tutorial"},
        {L"↺  デイリーチャレンジ", "daily"},
        {L"⚑  コース選択",   "course"},
        {L"⚙  オプション",     "option"},
        {L"  終了",         "exit"},
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
    btnLink.label  = L"Go to Wikipedia  ";
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
    t1.label = L" オンラインランキング";
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
    t2.label = L" 実績";
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

} // namespace game::scenes
