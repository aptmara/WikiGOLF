/**
 * @file WikiGolfHUD.cpp
 * @brief 通常時画面の7ブロックHUDを管理するコントローラー
 *
 * 入力: GolfGameState, ShotState, クラブ情報, 風情報, カメラ情報
 * 変更:
 *   - 通常時はクラブ選択リスト/ショットボタン/操作ヘルプを常時表示
 *   - ショット時はゲージUIをオーバーレイ表示し、クラブリストを薄くする
 *   - 風カードは上中央に移動 (kWindCardCenterX 中心)
 * 出力: UIText/UIImage/UIBarGauge エンティティの表示・テキスト更新
 *
 * 呼び出し元: WikiGolfScene::OnUpdate -> m_hud->Update(...)
 */
#include "WikiGolfHUD.h"
#include "../../ecs/World.h"
#include "../components/UIText.h"
#include "../components/UIImage.h"
#include "../components/WikiComponents.h"
#include "../../core/StringUtils.h"
#include "../utils/UIConstants.h"
#include <algorithm>
#include <cmath>
#include <format>

namespace game {
namespace controllers {

// =====================================================
// Initialize
// =====================================================

void WikiGolfHUD::Initialize(core::GameContext& ctx) {
    InitializeCourseInfoPanel(ctx);
    InitializeWindCard(ctx);
    InitializeClubSelectList(ctx);  // 初期は空、Update時に動的生成
    InitializeShotButton(ctx);
    InitializeControlHint(ctx);
    InitializeShotGaugePanel(ctx);
    InitializeJudgeText(ctx);
    InitializeMinimapUI(ctx);
}

// -------------------------------------------------------
// 左上: 現在地/目的地パネル
// -------------------------------------------------------
void WikiGolfHUD::InitializeCourseInfoPanel(core::GameContext& ctx) {
    // === 背景パネル ===
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.x = game::ui::kBrowserHudX - 10.0f;
        t.y = game::ui::kBrowserHudY - 10.0f;
        t.width  = 380.0f;
        t.height = 110.0f;
        t.style.bgColor = {0.05f, 0.1f, 0.15f, 0.8f};
        t.style.borderColor = game::ui::kColorAccent; // 青い枠線
        t.style.borderWidth = 1.0f;
        t.style.cornerRadius = 8.0f;
        t.visible = true;
        t.layer   = game::ui::kLayerBrowser - 1;
        m_ui.browserBgEntity = e;
    }

    // [WEB] バッジ
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text = L"WEB";
        t.x = game::ui::kBrowserHudX;
        t.y = game::ui::kBrowserHudY + 2.0f;
        t.width  = game::ui::kBrowserIconWidth;
        t.height = 30.0f;
        t.style  = graphics::TextStyle::Guide();
        t.style.fontSize   = game::ui::kBrowserFontSize;
        t.style.color      = game::ui::kColorAccent;
        t.style.align      = graphics::TextAlign::Center;
        t.style.bgColor    = game::ui::kColorBgDark;
        t.style.cornerRadius = 10.0f;
        t.style.borderWidth  = 1.0f;
        t.style.borderColor  = game::ui::kColorBorder;
        t.visible = true;
        t.layer   = game::ui::kLayerBrowser;
        m_ui.browserTabIconEntity = e;
        m_ui.headerEntity = e; // 互換
    }

    // 現在地名
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text  = L"Loading...";
        t.x     = game::ui::kBrowserHudX + game::ui::kBrowserIconWidth + 4.0f;
        t.y     = game::ui::kBrowserHudY + 2.0f;
        t.width = game::ui::kBrowserUrlWidth;
        t.height = 30.0f;
        t.style  = graphics::TextStyle::BrowserURL();
        t.style.fontSize = game::ui::kBrowserFontSize;
        t.style.bgColor  = {0.0f, 0.0f, 0.0f, 0.0f};
        t.style.borderWidth = 0.0f;
        t.visible = true;
        t.layer   = game::ui::kLayerBrowser + 1;
        m_ui.browserCurrentPageEntity = e;
    }

    // 目的地名 (-> xxx)
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text  = L"-> Target page...";
        t.x     = game::ui::kBrowserHudX + game::ui::kBrowserIconWidth + 4.0f;
        t.y     = game::ui::kBrowserHudY + game::ui::kBrowserLineSpacing + 6.0f;
        t.width = 600.0f;
        t.height = 28.0f;
        t.style  = graphics::TextStyle::GoalHighlight();
        t.style.fontSize = game::ui::kBrowserGoalFontSize;
        t.visible = true;
        t.layer   = game::ui::kLayerBrowser + 1;
        m_ui.browserTargetEntity = e;
    }

    // Shots: N / Par N
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text  = L"Shots: 0 / Par ?";
        t.x     = game::ui::kBrowserHudX + game::ui::kBrowserIconWidth + 4.0f;
        t.y     = game::ui::kBrowserHudY + game::ui::kBrowserLineSpacing * 2.0f + 6.0f;
        t.width = 600.0f;
        t.height = 24.0f;
        t.style  = graphics::TextStyle::BrowserSub();
        t.style.fontSize = game::ui::kBrowserSubFontSize;
        t.visible = true;
        t.layer   = game::ui::kLayerBrowser + 1;
        m_ui.browserShotInfoEntity = e;
        m_ui.shotCountEntity = e;
    }

    // 経路履歴
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text  = L"";
        t.x     = game::ui::kBrowserHudX + game::ui::kBrowserIconWidth + 4.0f;
        t.y     = game::ui::kBrowserHudY + game::ui::kBrowserLineSpacing * 3.0f + 2.0f;
        t.width = 700.0f;
        t.height = 22.0f;
        t.style  = graphics::TextStyle::BrowserSub();
        t.style.fontSize = game::ui::kBrowserSubFontSize;
        t.style.color    = game::ui::kColorTextSub;
        t.visible = true;
        t.layer   = game::ui::kLayerBrowser + 1;
        m_ui.browserHistoryEntity = e;
        m_ui.pathEntity = e;
    }

    // infoEntity (互換用・非表示)
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.visible = false;
        m_ui.infoEntity = e;
    }

    // CLUB ヘッダー (クラブリストの少し上)
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text = L"CLUB";
        t.x = game::ui::kClubPanelX;
        t.y = game::ui::kClubPanelY - 30.0f;
        t.width = 100.0f;
        t.height = 20.0f;
        t.style = graphics::TextStyle::BrowserSub();
        t.style.fontSize = 18.0f;
        t.style.color = game::ui::kColorWhite;
        t.visible = true;
        t.layer = game::ui::kLayerClubSelect;
        m_ui.clubHeaderEntity = e;
    }
}

// -------------------------------------------------------
// 上中央: 風情報カード
// -------------------------------------------------------
void WikiGolfHUD::InitializeWindCard(core::GameContext& ctx) {
    // 風カードは上中央に配置 (幅110, 中央揃え)
    constexpr float kWCardW = 110.0f;
    constexpr float kWCardX = 640.0f - kWCardW * 0.5f;
    constexpr float kWCardY = game::ui::kWindCardY;

    // "WIND" ラベル
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text  = L"WIND";
        t.x     = kWCardX;
        t.y     = kWCardY + 4.0f;
        t.width = kWCardW;
        t.height = 20.0f;
        t.style  = graphics::TextStyle::CardLabel();
        t.style.fontSize = game::ui::kWindLabelFontSize;
        t.style.align    = graphics::TextAlign::Center;
        t.style.bgColor  = game::ui::kColorBgDark;
        t.style.cornerRadius = 8.0f;
        t.style.borderWidth  = 1.0f;
        t.style.borderColor  = game::ui::kColorBorder;
        t.visible = true;
        t.layer   = game::ui::kLayerWind;
        m_ui.windCardLabelEntity = e;
    }

    // 風速値 (数字部分)
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text  = L"--";
        t.x     = kWCardX;
        t.y     = kWCardY + 26.0f;
        t.width = kWCardW;
        t.height = 38.0f;
        t.style  = graphics::TextStyle::CardValue();
        t.style.fontSize = game::ui::kWindValueFontSize;
        t.style.align    = graphics::TextAlign::Center;
        t.visible = true;
        t.layer   = game::ui::kLayerWind + 1;
        m_ui.windCardValueEntity = e;
    }

    // 風向き矢印 + m/s (1行)
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text  = L"↑ m/s";
        t.x     = kWCardX;
        t.y     = kWCardY + 64.0f;
        t.width = kWCardW;
        t.height = 28.0f;
        t.style  = graphics::TextStyle::CardValue();
        t.style.color    = game::ui::kColorAccent;
        t.style.fontSize = 16.0f;
        t.style.align    = graphics::TextAlign::Center;
        t.visible = true;
        t.layer   = game::ui::kLayerWind + 1;
        m_ui.windCardUnitEntity = e;
    }

    // 互換用エンティティ (非表示)
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.visible = false;
        m_ui.windEntity = e;
    }
    {
        auto e = ctx.world.CreateEntity();
        auto& img = ctx.world.Add<game::components::UIImage>(e);
        img = game::components::UIImage::Create("", 0.0f, 0.0f);
        img.visible = false;
        m_ui.windArrowEntity = e;
    }
}

// -------------------------------------------------------
// ミニマップ装飾 (背景、N矢印、スケール)
// -------------------------------------------------------
void WikiGolfHUD::InitializeMinimapUI(core::GameContext& ctx) {
    m_ui.minimapDecorationEntities.clear();
    const float mx = game::ui::kMinimapX;
    const float my = game::ui::kMinimapY;
    const float mw = game::ui::kMinimapWidth;
    const float mh = game::ui::kMinimapHeight;
    const int layer = game::ui::kLayerMinimap + 1; // ミニマップ自体の上

    // 背景枠
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.x = mx - 5.0f;
        t.y = my - 5.0f;
        t.width = mw + 40.0f; // 右側に目盛りの余白
        t.height = mh + 40.0f; // 下部に凡例の余白
        t.style.bgColor = {0.05f, 0.1f, 0.15f, 0.7f};
        t.style.borderColor = game::ui::kColorBorder;
        t.style.borderWidth = 1.0f;
        t.style.cornerRadius = 8.0f;
        t.visible = true;
        t.layer = game::ui::kLayerMinimap - 1; // ミニマップの背後
        m_ui.minimapDecorationEntities.push_back(e);
    }

    // N 矢印
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text = L"N\n\u25B2"; // N と上向き矢印
        t.x = mx + mw + 5.0f;
        t.y = my;
        t.width = 30.0f;
        t.height = 30.0f;
        t.style = graphics::TextStyle::Guide();
        t.style.fontSize = 12.0f;
        t.style.color = game::ui::kColorWhite;
        t.style.align = graphics::TextAlign::Center;
        t.visible = true;
        t.layer = layer;
        m_ui.minimapDecorationEntities.push_back(e);
    }

    // 目盛り線とテキスト
    auto createScale = [&](float oy, const std::wstring& txt) {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text = L"| " + txt;
        t.x = mx + mw + 5.0f;
        t.y = my + oy;
        t.width = 35.0f;
        t.height = 15.0f;
        t.style = graphics::TextStyle::Guide();
        t.style.fontSize = 10.0f;
        t.style.color = {0.8f, 0.8f, 0.8f, 1.0f};
        t.style.align = graphics::TextAlign::Left;
        t.visible = true;
        t.layer = layer;
        m_ui.minimapDecorationEntities.push_back(e);
    };
    createScale(30.0f, L"150m");
    createScale(mh / 2.0f, L" 50m");
    createScale(mh - 15.0f, L"  0m");

    // 凡例 (現在地)
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text = L"\u25CF 現在地"; // 黒丸を青く着色する代わり
        t.x = mx;
        t.y = my + mh + 5.0f;
        t.width = 70.0f;
        t.height = 20.0f;
        t.style = graphics::TextStyle::Guide();
        t.style.fontSize = 12.0f;
        t.style.color = {0.18f, 0.85f, 1.0f, 1.0f}; // 蛍光シアンに変更
        t.style.align = graphics::TextAlign::Left;
        t.visible = true;
        t.layer = layer;
        m_ui.minimapDecorationEntities.push_back(e);
    }

    // 凡例 (ピン位置)
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text = L"\u26F3 ピン位置"; // 旗
        t.x = mx + 80.0f;
        t.y = my + mh + 5.0f;
        t.width = 70.0f;
        t.height = 20.0f;
        t.style = graphics::TextStyle::Guide();
        t.style.fontSize = 12.0f;
        t.style.color = {1.0f, 0.2f, 0.2f, 1.0f}; // 鮮烈なレッドに変更
        t.style.align = graphics::TextAlign::Left;
        t.visible = true;
        t.layer = layer;
        m_ui.minimapDecorationEntities.push_back(e);
    }
}

// -------------------------------------------------------
// 左側: クラブ選択リスト (動的構築, Initialize 時は空)
// -------------------------------------------------------
void WikiGolfHUD::InitializeClubSelectList(core::GameContext& ctx) {
    // Update 時にクラブ数が決定したら動的に生成する
    // Initialize 時点では空にしておく
    m_ui.clubBgEntities.clear();
    m_ui.clubNameEntities.clear();
    m_ui.clubArrowEntities.clear();
    m_builtClubCount = 0;
}

// -------------------------------------------------------
// 右下: ショットボタン
// -------------------------------------------------------
void WikiGolfHUD::InitializeShotButton(core::GameContext& ctx) {
    const float bx = game::ui::kShotBtnX - 60.0f; // wider to fit text
    const float by = game::ui::kShotBtnY + 40.0f; // move down a bit
    const float bw = game::ui::kShotBtnW + 100.0f;
    const float bh = 100.0f; // shorter height for keycap

    // 背景 (キーキャップ風)
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text  = L"";
        t.x     = bx;
        t.y     = by;
        t.width = bw;
        t.height = bh;
        t.style.useGradient  = true;
        t.style.bgColor      = {0.15f, 0.35f, 0.8f, 1.0f}; // Top: lighter blue
        t.style.bgGradientEnd = {0.05f, 0.15f, 0.4f, 1.0f}; // Bottom: dark blue
        t.style.cornerRadius = 16.0f; // 角丸
        t.style.borderWidth  = 4.0f;
        t.style.borderColor  = {0.4f, 0.8f, 1.0f, 0.9f}; // Glow-like bright cyan border
        t.style.color        = {0.0f, 0.0f, 0.0f, 0.0f}; // 文字なし
        t.style.hasShadow    = true; // 影効果を有効化します
        t.style.shadowColor  = {0.0f, 0.5f, 1.0f, 0.6f};
        t.style.shadowOffsetX = 0.0f;
        t.style.shadowOffsetY = 8.0f;
        t.visible = true;
        t.layer   = game::ui::kLayerShotButton;
        m_ui.shotButtonBgEntity = e;
    }

    // テキスト "SHOT" とサブテキスト
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text  = L"SHOT\n(Space or Click)";
        t.x     = bx;
        t.y     = by + bh * 0.15f;
        t.width = bw;
        t.height = bh;
        t.style  = graphics::TextStyle::Guide();
        t.style.fontSize  = 26.0f;
        t.style.align     = graphics::TextAlign::Center;
        t.style.color     = {1.0f, 1.0f, 1.0f, 1.0f};
        t.style.hasShadow = true;
        t.style.shadowColor = {0.0f, 0.0f, 0.0f, 0.8f};
        t.style.shadowOffsetX = 2.0f;
        t.style.shadowOffsetY = 2.0f;
        t.style.bgColor   = {0.0f, 0.0f, 0.0f, 0.0f};
        t.style.borderWidth = 0.0f;
        t.visible = true;
        t.layer   = game::ui::kLayerShotButton + 1;
        m_ui.shotButtonTextEntity = e;
    }
}

// -------------------------------------------------------
// 左下: 操作ヘルプ
// -------------------------------------------------------
void WikiGolfHUD::InitializeControlHint(core::GameContext& ctx) {
    auto e = ctx.world.CreateEntity();
    auto& t = ctx.world.Add<game::components::UIText>(e);
    t.text  = L"[Q/E] クラブ切替\n[マウス] カメラ\n[M] マップ\n[Space/Click] ショット";
    t.x     = game::ui::kControlHintX;
    t.y     = game::ui::kControlHintY - 60.0f; // adjust Y to fit 4 lines
    t.width = game::ui::kControlHintW;
    t.height = 100.0f; // taller
    t.style  = graphics::TextStyle::BrowserSub();
    t.style.fontSize = game::ui::kControlHintFont;
    t.style.color    = {0.9f, 0.95f, 1.0f, 1.0f}; // Brighter text for keycap
    
    // ヘルプ表示用の背景を設定します
    t.style.useGradient = true;
    t.style.bgColor     = {0.2f, 0.25f, 0.3f, 0.9f}; // 上部カラー
    t.style.bgGradientEnd = {0.1f, 0.15f, 0.2f, 0.9f}; // 下部カラー
    t.style.borderWidth = 2.0f;
    t.style.borderColor = {0.4f, 0.5f, 0.6f, 0.8f}; // 境界カラー
    t.style.cornerRadius = 8.0f;
    t.style.hasShadow   = true;
    t.style.shadowColor = {0.0f, 0.0f, 0.0f, 0.8f};
    t.style.shadowOffsetX = 0.0f;
    t.style.shadowOffsetY = 4.0f; // 影の縦オフセット

    t.visible = true;
    t.layer   = game::ui::kLayerControlHint;
    m_ui.controlHintEntity = e;
}

// -------------------------------------------------------
// ショット時ゲージパネル (初期は非表示)
// -------------------------------------------------------
void WikiGolfHUD::InitializeShotGaugePanel(core::GameContext& ctx) {
    // ショット中の情報をまとめる背景カード
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text = L"";
        t.x = game::ui::kShotPanelBgX;
        t.y = game::ui::kShotPanelBgY;
        t.width = game::ui::kShotPanelBgWidth;
        t.height = game::ui::kShotPanelBgHeight;
        t.style.bgColor = {0.025f, 0.040f, 0.070f, 0.88f};
        t.style.borderColor = {0.200f, 0.650f, 1.000f, 0.72f};
        t.style.borderWidth = 1.5f;
        t.style.cornerRadius = 10.0f;
        t.style.useGradient = true;
        t.style.bgGradientEnd = {0.050f, 0.085f, 0.130f, 0.90f};
        t.visible = false;
        t.layer = game::ui::kLayerShotPanel - 2;
        m_ui.shotPanelBgEntity = e;
    }

    // 入力ステップ
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text = L"1/2";
        t.x = game::ui::kShotPanelX;
        t.y = game::ui::kShotPanelY - 22.0f;
        t.width = game::ui::kShotStepWidth;
        t.height = 24.0f;
        t.style = graphics::TextStyle::ShotPanelLabel();
        t.style.align = graphics::TextAlign::Center;
        t.style.fontSize = game::ui::kShotLabelFontSize;
        t.style.color = game::ui::kColorBgDark;
        t.style.bgColor = game::ui::kColorWarning;
        t.style.borderColor = {1.0f, 0.86f, 0.24f, 0.85f};
        t.style.borderWidth = 1.0f;
        t.style.cornerRadius = 8.0f;
        t.visible = false;
        t.layer = game::ui::kLayerShotPanel + 1;
        m_ui.shotPanelStepEntity = e;
    }

    // フェーズ見出し
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text = L"飛距離を決める";
        t.x = game::ui::kShotPanelX + game::ui::kShotStepWidth + 12.0f;
        t.y = game::ui::kShotPanelY - 25.0f;
        t.width = 300.0f;
        t.height = 30.0f;
        t.style = graphics::TextStyle::ClubName();
        t.style.align = graphics::TextAlign::Left;
        t.style.fontSize = game::ui::kShotTitleFontSize;
        t.visible = false;
        t.layer = game::ui::kLayerShotPanel + 1;
        m_ui.shotPanelTitleEntity = e;
    }

    // 操作ヒント
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text = L"左クリックで決定 / 右クリックで中止";
        t.x = game::ui::kShotPanelX + game::ui::kShotPanelWidth - 300.0f;
        t.y = game::ui::kShotPanelY - 19.0f;
        t.width = 300.0f;
        t.height = 22.0f;
        t.style = graphics::TextStyle::ShotPanelLabel();
        t.style.align = graphics::TextAlign::Right;
        t.style.fontSize = game::ui::kShotHintFontSize;
        t.style.color = game::ui::kColorTextSub;
        t.visible = false;
        t.layer = game::ui::kLayerShotPanel + 1;
        m_ui.shotPanelHintEntity = e;
    }

    // POWER ラベル
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text  = L"強さ";
        t.x     = game::ui::kShotPanelX;
        t.y     = game::ui::kShotPanelY + 38.0f;
        t.width = 120.0f;
        t.height = 22.0f;
        t.style  = graphics::TextStyle::ShotPanelLabel();
        t.style.fontSize     = game::ui::kShotLabelFontSize;
        t.style.bgColor      = {0.0f, 0.0f, 0.0f, 0.0f};
        t.style.borderWidth  = 0.0f;
        t.visible = false; // ショット時のみ
        t.layer   = game::ui::kLayerShotPanel;
        m_ui.shotPanelPowerLabelEntity = e;
    }

    // POWER 値
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text  = L"0%";
        t.x     = game::ui::kShotPanelX + game::ui::kShotPanelWidth - 180.0f;
        t.y     = game::ui::kShotPanelY + 38.0f;
        t.width = 180.0f;
        t.height = 22.0f;
        t.style  = graphics::TextStyle::ShotPanelValue();
        t.style.fontSize = game::ui::kShotValueFontSize;
        t.visible = false;
        t.layer   = game::ui::kLayerShotPanel + 1;
        m_ui.shotPanelPowerValueEntity = e;
    }

    // ACCURACY ラベル
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text  = L"インパクト";
        t.x     = game::ui::kShotPanelX;
        t.y     = game::ui::kShotPanelY + 64.0f;
        t.width = 120.0f;
        t.height = 22.0f;
        t.style  = graphics::TextStyle::ShotPanelLabel();
        t.style.fontSize = game::ui::kShotLabelFontSize;
        t.visible = false;
        t.layer   = game::ui::kLayerShotPanel;
        m_ui.shotPanelAccuracyLabelEntity = e;
    }

    // ACCURACY 値
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text  = L"待機";
        t.x     = game::ui::kShotPanelX + game::ui::kShotPanelWidth - 220.0f;
        t.y     = game::ui::kShotPanelY + 64.0f;
        t.width = 220.0f;
        t.height = 22.0f;
        t.style  = graphics::TextStyle::ShotPanelValue();
        t.style.fontSize = game::ui::kShotValueFontSize;
        t.visible = false;
        t.layer   = game::ui::kLayerShotPanel + 1;
        m_ui.shotPanelAccuracyValueEntity = e;
    }

    // CLUB 名 (ショットパネル内)
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text  = L"ドライバー";
        t.x     = game::ui::kShotPanelX;
        t.y     = game::ui::kShotPanelY + 88.0f;
        t.width = game::ui::kShotPanelWidth;
        t.height = 18.0f;
        t.style  = graphics::TextStyle::ClubName();
        t.style.align    = graphics::TextAlign::Left;
        t.style.fontSize = game::ui::kBrowserSubFontSize;
        t.style.color    = game::ui::kColorTextSub;
        t.visible = false;
        t.layer   = game::ui::kLayerShotPanel;
        m_ui.shotPanelClubLabelEntity = e;
    }

    // パワーゲージバー (UIBarGauge)
    {
        auto e = ctx.world.CreateEntity();
        auto& gauge = ctx.world.Add<game::components::UIBarGauge>(e);
        gauge.value      = 0.0f;
        gauge.maxValue   = 1.0f;
        gauge.color      = game::ui::kColorWarning;
        gauge.bgColor    = game::ui::kColorBgDark;
        gauge.borderColor= game::ui::kColorBorder;
        gauge.borderWidth= game::ui::kGaugeBorderWidth;
        gauge.x          = game::ui::kShotPanelX;
        gauge.y          = game::ui::kShotPanelY + 12.0f;
        gauge.width      = game::ui::kShotPanelWidth;
        gauge.height     = game::ui::kGaugeHeight;
        gauge.isVisible  = false;
        gauge.mode             = game::components::UIBarGaugeMode::Power;
        gauge.showMarker       = true;
        gauge.markerValue      = 0.0f;
        gauge.markerColor      = game::ui::kColorWhite;
        gauge.showConfirmedMarker = false;
        gauge.showImpactZones  = false;
        gauge.impactCenter     = 0.5f;
        gauge.impactWidthGreat = game::ui::kImpactWidthGreat;
        gauge.impactWidthNice  = game::ui::kImpactWidthNice;
        m_ui.gaugeBarEntity = e;
    }
}

// -------------------------------------------------------
// 判定テキスト
// -------------------------------------------------------
void WikiGolfHUD::InitializeJudgeText(core::GameContext& ctx) {
    auto e = ctx.world.CreateEntity();
    auto& t = ctx.world.Add<game::components::UIText>(e);
    t.text  = L"";
    t.x     = game::ui::kJudgeTextX;
    t.y     = game::ui::kJudgeTextY;
    t.width = 200.0f;
    t.height = 80.0f;
    t.style  = graphics::TextStyle::Guide();
    t.style.fontSize = game::ui::kJudgeFontSize;
    t.style.align    = graphics::TextAlign::Center;
    t.visible = true;
    t.layer   = game::ui::kLayerJudge;
    m_ui.judgeEntity = e;
}

// =====================================================
// Update
// =====================================================

void WikiGolfHUD::Update(core::GameContext& ctx, float dt,
                         const game::components::GolfGameState& state,
                         game::components::ShotState::Phase shotPhase,
                         float currentImpact,
                         float currentPower, float confirmedPower,
                         float windSpeed, const DirectX::XMFLOAT2& windDir, float cameraYaw,
                         const std::vector<ClubUIData>& clubs,
                         int currentClubIndex,
                         float distanceToTarget, float heightDiff)
{
    UpdateCourseInfoPanel(ctx, state);
    UpdateWindUI(ctx, windSpeed, windDir, cameraYaw);
    UpdateClubSelectList(ctx, clubs, currentClubIndex);

    ClubUIData currentClubData;
    if (currentClubIndex >= 0 && currentClubIndex < clubs.size()) {
        currentClubData = clubs[currentClubIndex];
    }
    UpdateBottomInfoPanels(ctx, distanceToTarget, heightDiff, state.currentMaterial, currentClubData);

    // TODO: GuideUI (操作ヘルプ) は後で装飾するが一旦残す
    UpdateGuideUI(ctx, state);
    UpdateShotPhasePanel(ctx, shotPhase, currentPower, confirmedPower,
                         currentImpact, currentClubData);
}

void WikiGolfHUD::UpdateShotPhasePanel(
    core::GameContext& ctx,
    game::components::ShotState::Phase shotPhase,
    float currentPower,
    float confirmedPower,
    float currentImpact,
    const ClubUIData& currentClub)
{
    const bool powerPhase =
        shotPhase == game::components::ShotState::Phase::PowerCharging;
    const bool impactPhase =
        shotPhase == game::components::ShotState::Phase::ImpactTiming;
    const bool activeShotPhase = powerPhase || impactPhase;

    auto setText = [&](ecs::Entity e, const std::wstring& text) {
        if (e != UINT32_MAX && ctx.world.Has<game::components::UIText>(e)) {
            ctx.world.Get<game::components::UIText>(e)->text = text;
        }
    };
    auto setColor = [&](ecs::Entity e, const DirectX::XMFLOAT4& color) {
        if (e != UINT32_MAX && ctx.world.Has<game::components::UIText>(e)) {
            ctx.world.Get<game::components::UIText>(e)->style.color = color;
        }
    };

    const float shownPower = confirmedPower > 0.0f ? confirmedPower : currentPower;
    const int powerPercent =
        std::clamp(static_cast<int>(std::round(shownPower * 100.0f)), 0, 100);

    if (powerPhase) {
        setText(m_ui.shotPanelStepEntity, L"1/2");
        setText(m_ui.shotPanelTitleEntity, L"飛距離を決める");
        setText(m_ui.shotPanelHintEntity, L"左クリックで強さを決定 / 右クリックで中止");
        setText(m_ui.shotPanelPowerLabelEntity, L"強さ");
        setText(m_ui.shotPanelPowerValueEntity,
                std::to_wstring(powerPercent) + L"%");
        setText(m_ui.shotPanelAccuracyLabelEntity, L"次の入力");
        setText(m_ui.shotPanelAccuracyValueEntity, L"インパクト");
        setColor(m_ui.shotPanelPowerValueEntity, game::ui::kColorWarning);
        setColor(m_ui.shotPanelAccuracyValueEntity, game::ui::kColorTextSub);
    } else if (impactPhase) {
        const float diff = currentImpact - 0.5f;
        const float absDiff = std::abs(diff);
        std::wstring impactText = L"芯";
        DirectX::XMFLOAT4 impactColor = game::ui::kColorSpecial;
        if (absDiff >= game::ui::kThresholdSpecial) {
            const int missPercent =
                std::clamp(static_cast<int>(std::round(absDiff * 200.0f)), 0, 100);
            impactText = (diff < 0.0f ? L"左 " : L"右 ") +
                         std::to_wstring(missPercent) + L"%";
            impactColor = absDiff < game::ui::kThresholdGreat
                              ? game::ui::kColorSuccess
                              : game::ui::kColorWarning;
        }

        setText(m_ui.shotPanelStepEntity, L"2/2");
        setText(m_ui.shotPanelTitleEntity, L"インパクトを合わせる");
        setText(m_ui.shotPanelHintEntity, L"中央の芯でクリック / 右クリックで中止");
        setText(m_ui.shotPanelPowerLabelEntity, L"強さ");
        setText(m_ui.shotPanelPowerValueEntity,
                std::to_wstring(powerPercent) + L"% 確定");
        setText(m_ui.shotPanelAccuracyLabelEntity, L"インパクト");
        setText(m_ui.shotPanelAccuracyValueEntity, impactText);
        setColor(m_ui.shotPanelPowerValueEntity, game::ui::kColorTextSub);
        setColor(m_ui.shotPanelAccuracyValueEntity, impactColor);
    } else {
        setText(m_ui.shotPanelStepEntity, L"");
        setText(m_ui.shotPanelTitleEntity, L"ショット準備");
        setText(m_ui.shotPanelHintEntity, L"");
        setText(m_ui.shotPanelPowerLabelEntity, L"強さ");
        setText(m_ui.shotPanelPowerValueEntity, L"0%");
        setText(m_ui.shotPanelAccuracyLabelEntity, L"インパクト");
        setText(m_ui.shotPanelAccuracyValueEntity, L"待機");
        setColor(m_ui.shotPanelPowerValueEntity, game::ui::kColorWhite);
        setColor(m_ui.shotPanelAccuracyValueEntity, game::ui::kColorTextSub);
    }

    std::wstring clubText = core::ToWString(currentClub.name);
    if (activeShotPhase && currentClub.maxPower > 0.0f) {
        const int output =
            std::clamp(static_cast<int>(std::round(currentClub.maxPower * shownPower)),
                       0, 999);
        clubText += L" / 出力 " + std::to_wstring(output);
    }
    setText(m_ui.shotPanelClubLabelEntity, clubText);

    if (m_ui.gaugeBarEntity != UINT32_MAX &&
        ctx.world.Has<game::components::UIBarGauge>(m_ui.gaugeBarEntity))
    {
        auto* gauge = ctx.world.Get<game::components::UIBarGauge>(m_ui.gaugeBarEntity);
        gauge->mode = impactPhase ? game::components::UIBarGaugeMode::Impact
                                  : game::components::UIBarGaugeMode::Power;
        gauge->showImpactZones = impactPhase;
        gauge->showConfirmedMarker = false;
        gauge->confirmedValue = confirmedPower;
        gauge->confirmedMarkerColor = game::ui::kColorWarning;
        gauge->color = powerPercent >= 75 ? game::ui::kColorWarning
                     : powerPercent >= 40 ? game::ui::kColorSuccess
                                          : game::ui::kColorAccent;
        gauge->markerColor = impactPhase ? game::ui::kColorWhite
                                         : game::ui::kColorWarning;
        gauge->borderColor = impactPhase ? game::ui::kColorSuccess
                                         : game::ui::kColorBorder;
    }
}

// -------------------------------------------------------
// 左上パネル更新
// -------------------------------------------------------
void WikiGolfHUD::UpdateCourseInfoPanel(core::GameContext& ctx,
                                         const game::components::GolfGameState& state)
{
    if (m_ui.browserCurrentPageEntity != UINT32_MAX &&
        ctx.world.Has<game::components::UIText>(m_ui.browserCurrentPageEntity))
    {
        auto* t = ctx.world.Get<game::components::UIText>(m_ui.browserCurrentPageEntity);
        t->text = core::ToWString(state.currentPage);
    }

    if (m_ui.browserTargetEntity != UINT32_MAX &&
        ctx.world.Has<game::components::UIText>(m_ui.browserTargetEntity))
    {
        auto* t = ctx.world.Get<game::components::UIText>(m_ui.browserTargetEntity);
        t->text = L"-> " + core::ToWString(state.targetPage);
    }

    if (m_ui.browserShotInfoEntity != UINT32_MAX &&
        ctx.world.Has<game::components::UIText>(m_ui.browserShotInfoEntity))
    {
        auto* t = ctx.world.Get<game::components::UIText>(m_ui.browserShotInfoEntity);
        t->text = L"Shots: " + std::to_wstring(state.shotCount) +
                  L" / Par " + std::to_wstring(state.par);
    }
}

// -------------------------------------------------------
// 風カード更新
// -------------------------------------------------------
void WikiGolfHUD::UpdateWindUI(core::GameContext& ctx,
                                float windSpeed,
                                const DirectX::XMFLOAT2& windDir,
                                float cameraYaw)
{
    // 風速
    if (m_ui.windCardValueEntity != UINT32_MAX &&
        ctx.world.Has<game::components::UIText>(m_ui.windCardValueEntity))
    {
        auto* t = ctx.world.Get<game::components::UIText>(m_ui.windCardValueEntity);
        wchar_t buf[32];
        swprintf(buf, 32, L"%.1f", windSpeed);
        t->text = buf;

        // 強風時は色を変える: 5 m/s 以上で黄色、10 m/s 以上で赤寄り
        if (windSpeed >= 10.0f) {
            t->style.color = game::ui::kColorError;
        } else if (windSpeed >= 5.0f) {
            t->style.color = game::ui::kColorWarning;
        } else {
            t->style.color = game::ui::kColorWhite;
        }
    }

    // 風向き矢印 (カメラ相対)
    if (m_ui.windCardUnitEntity != UINT32_MAX &&
        ctx.world.Has<game::components::UIText>(m_ui.windCardUnitEntity))
    {
        auto* t = ctx.world.Get<game::components::UIText>(m_ui.windCardUnitEntity);

        DirectX::XMVECTOR wdir = DirectX::XMVectorSet(windDir.x, 0, windDir.y, 0);
        DirectX::XMVECTOR camFwd = DirectX::XMVectorSet(sinf(cameraYaw), 0, cosf(cameraYaw), 0);

        DirectX::XMFLOAT3 cross;
        DirectX::XMStoreFloat3(&cross, DirectX::XMVector3Cross(camFwd, wdir));
        float dot   = DirectX::XMVectorGetX(DirectX::XMVector3Dot(camFwd, wdir));
        float angle = atan2f(cross.y, dot);

        std::wstring arrow = L"\u2191"; // ↑
        if      (angle > DirectX::XM_PI/8   && angle <=  3*DirectX::XM_PI/8) arrow = L"\u2197";
        else if (angle > 3*DirectX::XM_PI/8 && angle <=  5*DirectX::XM_PI/8) arrow = L"\u2192";
        else if (angle > 5*DirectX::XM_PI/8 && angle <=  7*DirectX::XM_PI/8) arrow = L"\u2198";
        else if (angle > 7*DirectX::XM_PI/8 || angle <= -7*DirectX::XM_PI/8) arrow = L"\u2193";
        else if (angle < -5*DirectX::XM_PI/8)                                 arrow = L"\u2199";
        else if (angle < -3*DirectX::XM_PI/8)                                 arrow = L"\u2190";
        else if (angle < -DirectX::XM_PI/8)                                   arrow = L"\u2196";

        t->text = arrow + L" m/s";
    }
}

// -------------------------------------------------------
// クラブ選択リスト更新 (動的生成込み)
// 入力: clubs (全クラブデータリスト), currentClubIndex
// -------------------------------------------------------
void WikiGolfHUD::UpdateClubSelectList(core::GameContext& ctx,
                                        const std::vector<ClubUIData>& clubs,
                                        int currentClubIndex)
{
    int n = static_cast<int>(clubs.size());
    if (n == 0) return;

    // クラブ数が変わったら再構築
    if (m_builtClubCount != n) {
        // 既存エンティティを破棄
        for (auto e : m_ui.clubBgEntities)   { if (ctx.world.IsAlive(e)) ctx.world.DestroyEntity(e); }
        for (auto e : m_ui.clubIconEntities) { if (ctx.world.IsAlive(e)) ctx.world.DestroyEntity(e); }
        for (auto e : m_ui.clubNameEntities) { if (ctx.world.IsAlive(e)) ctx.world.DestroyEntity(e); }
        for (auto e : m_ui.clubSubNameEntities){ if (ctx.world.IsAlive(e)) ctx.world.DestroyEntity(e); }
        for (auto e : m_ui.clubArrowEntities){ if (ctx.world.IsAlive(e)) ctx.world.DestroyEntity(e); }
        m_ui.clubBgEntities.clear();
        m_ui.clubIconEntities.clear();
        m_ui.clubNameEntities.clear();
        m_ui.clubSubNameEntities.clear();
        m_ui.clubArrowEntities.clear();

        const float x = game::ui::kClubPanelX;
        float y = game::ui::kClubPanelY;
        const float w = game::ui::kClubItemW;
        const float h = game::ui::kClubItemH;
        const float sp = game::ui::kClubItemSpacing;

        for (int i = 0; i < n; ++i) {
            // 背景行
            {
                auto e = ctx.world.CreateEntity();
                auto& t = ctx.world.Add<game::components::UIText>(e);
                t.text  = L"";
                t.x     = x;
                t.y     = y;
                t.width = w;
                t.height = h;
                t.style.cornerRadius = 6.0f;
                t.visible = true;
                t.layer   = game::ui::kLayerClubSelect;
                m_ui.clubBgEntities.push_back(e);
            }

            // アイコン
            {
                auto e = ctx.world.CreateEntity();
                auto& img = ctx.world.Add<game::components::UIImage>(e);
                img = game::components::UIImage::Create(clubs[i].iconTexture, x + 8.0f, y + 2.0f);
                img.width = h - 4.0f;
                img.height = h - 4.0f;
                img.visible = true;
                img.layer = game::ui::kLayerClubSelect + 1;
                m_ui.clubIconEntities.push_back(e);
            }

            // クラブ名 (日本語)
            {
                auto e = ctx.world.CreateEntity();
                auto& t = ctx.world.Add<game::components::UIText>(e);
                t.text  = core::ToWString(clubs[i].name);
                t.x     = x + h + 12.0f;
                t.y     = y + 4.0f;
                t.width = w - h - 30.0f;
                t.height = 18.0f;
                t.style  = graphics::TextStyle::BrowserSub();
                t.style.fontSize = game::ui::kClubNameFontSize;
                t.style.align    = graphics::TextAlign::Left;
                t.visible = true;
                t.layer   = game::ui::kLayerClubSelect + 1;
                m_ui.clubNameEntities.push_back(e);
            }

            // クラブ種別テキストの生成
            {
                auto e = ctx.world.CreateEntity();
                auto& t = ctx.world.Add<game::components::UIText>(e);
                t.text  = core::ToWString(clubs[i].shortName + " " + clubs[i].categoryEN);
                t.x     = x + h + 12.0f;
                t.y     = y + 22.0f;
                t.width = w - h - 30.0f;
                t.height = 16.0f;
                t.style  = graphics::TextStyle::BrowserSub();
                t.style.fontSize = 12.0f;
                t.style.color    = {0.7f, 0.7f, 0.75f, 1.0f};
                t.style.align    = graphics::TextAlign::Left;
                t.visible = true;
                t.layer   = game::ui::kLayerClubSelect + 1;
                m_ui.clubSubNameEntities.push_back(e);
            }

            // 右端の選択マークの生成
            {
                auto e = ctx.world.CreateEntity();
                auto& t = ctx.world.Add<game::components::UIText>(e);
                t.text  = L"\u25b6"; // ▶
                t.x     = x + w - 24.0f;
                t.y     = y + h * 0.25f;
                t.width = 20.0f;
                t.height = 20.0f;
                t.style  = graphics::TextStyle::Guide();
                t.style.fontSize = game::ui::kClubNameFontSize;
                t.style.color    = game::ui::kColorAccent;
                t.style.align    = graphics::TextAlign::Center;
                t.style.bgColor  = {0.0f, 0.0f, 0.0f, 0.0f};
                t.visible = false;
                t.layer   = game::ui::kLayerClubSelect + 2;
                m_ui.clubArrowEntities.push_back(e);
            }

            y += h + sp;
        }
        m_builtClubCount = n;
    }

    // 毎フレームの選択状態更新
    for (int i = 0; i < n; ++i) {
        bool isActive = (i == currentClubIndex);
        
        // 背景の見た目
        if (i < (int)m_ui.clubBgEntities.size()) {
            if (auto* bg = ctx.world.Get<game::components::UIText>(m_ui.clubBgEntities[i])) {
                if (isActive) {
                    bg->style.bgColor     = {0.0f, 0.4f, 0.8f, 0.8f}; // 青系ハイライト
                    bg->style.borderColor = game::ui::kColorAccent;
                    bg->style.borderWidth = 1.5f;
                } else {
                    bg->style.bgColor     = {0.05f, 0.1f, 0.15f, 0.7f}; // 暗い半透明
                    bg->style.borderColor = {0,0,0,0};
                    bg->style.borderWidth = 0.0f;
                }
            }
        }
        
        // アイコンのアルファ
        if (i < (int)m_ui.clubIconEntities.size()) {
            if (auto* icon = ctx.world.Get<game::components::UIImage>(m_ui.clubIconEntities[i])) {
                icon->alpha = isActive ? 1.0f : 0.4f;
            }
        }

        // テキストのアルファ
        if (i < (int)m_ui.clubNameEntities.size()) {
            if (auto* name = ctx.world.Get<game::components::UIText>(m_ui.clubNameEntities[i])) {
                name->style.color.w = isActive ? 1.0f : 0.4f;
            }
        }
        if (i < (int)m_ui.clubSubNameEntities.size()) {
            if (auto* sub = ctx.world.Get<game::components::UIText>(m_ui.clubSubNameEntities[i])) {
                sub->style.color.w = isActive ? 0.9f : 0.4f;
                if (isActive) {
                    sub->style.color.x = 0.8f;
                    sub->style.color.y = 0.9f;
                    sub->style.color.z = 1.0f;
                } else {
                    sub->style.color.x = 0.7f;
                    sub->style.color.y = 0.7f;
                    sub->style.color.z = 0.75f;
                }
            }
        }

        // 矢印表示切替
        if (i < (int)m_ui.clubArrowEntities.size()) {
            if (auto* arrow = ctx.world.Get<game::components::UIText>(m_ui.clubArrowEntities[i])) {
                arrow->visible = isActive;
            }
        }
    }
}

// -------------------------------------------------------
// 下部情報パネル (距離, クラブ, ライ)
// -------------------------------------------------------
void WikiGolfHUD::UpdateBottomInfoPanels(core::GameContext& ctx, float distanceToTarget, float heightDiff, 
                                         game::components::TerrainMaterial lie, const ClubUIData& currentClub)
{
    const float py = 600.0f;
    const float pw = 180.0f;
    const float ph = 80.0f;
    const float space = 20.0f;
    const float px1 = 640.0f - (pw * 1.5f + space); // 340
    const float px2 = px1 + pw + space;             // 540
    const float px3 = px2 + pw + space;             // 740
    const int layer = game::ui::kLayerShotPanel;

    // Helper lambda for creating text
    auto createText = [&](ecs::Entity& ent, float x, float y, float w, float h, float fontSize, const DirectX::XMFLOAT4& color, graphics::TextAlign align) {
        ent = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(ent);
        t.x = x; t.y = y; t.width = w; t.height = h;
        t.style = graphics::TextStyle::BrowserSub();
        t.style.fontSize = fontSize;
        t.style.color = color;
        t.style.align = align;
        t.layer = layer + 1;
        t.visible = true;
    };
    auto createBg = [&](ecs::Entity& ent, float x) {
        ent = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(ent);
        t.x = x; t.y = py; t.width = pw; t.height = ph;
        t.style.bgColor = {0.05f, 0.1f, 0.15f, 0.7f};
        t.style.borderColor = game::ui::kColorBorder;
        t.style.borderWidth = 1.0f;
        t.style.cornerRadius = 8.0f;
        t.layer = layer;
        t.visible = true;
    };

    // 距離パネルの生成
    if (m_ui.distPanelBgEntity == UINT32_MAX) {
        createBg(m_ui.distPanelBgEntity, px1);
        createText(m_ui.distLabelEntity, px1 + 10, py + 10, 80, 20, 14.0f, {0.8f, 0.8f, 0.8f, 1.0f}, graphics::TextAlign::Left);
        if (auto* t = ctx.world.Get<game::components::UIText>(m_ui.distLabelEntity)) t->text = L"\u26F3 残り距離"; // 旗アイコン + 文字
        createText(m_ui.distValueEntity, px1 + 10, py + 30, pw - 20, 30, 24.0f, {1.0f, 0.9f, 0.2f, 1.0f}, graphics::TextAlign::Center);
        createText(m_ui.heightValueEntity, px1 + 10, py + 60, pw - 20, 16, 12.0f, {0.5f, 0.8f, 1.0f, 1.0f}, graphics::TextAlign::Center);
    }

    // 選択中クラブパネルの生成
    if (m_ui.clubInfoPanelBgEntity == UINT32_MAX) {
        createBg(m_ui.clubInfoPanelBgEntity, px2);
        createText(m_ui.clubInfoLabelEntity, px2 + 10, py + 10, pw - 20, 20, 12.0f, {0.8f, 0.8f, 0.8f, 1.0f}, graphics::TextAlign::Center);
        if (auto* t = ctx.world.Get<game::components::UIText>(m_ui.clubInfoLabelEntity)) t->text = L"選択中クラブ";
        createText(m_ui.clubInfoNameEntity, px2 + 60, py + 30, pw - 60, 24, 20.0f, game::ui::kColorWhite, graphics::TextAlign::Center);
        createText(m_ui.clubInfoShortNameEntity, px2 + 60, py + 55, pw - 60, 16, 14.0f, {0.7f, 0.7f, 0.75f, 1.0f}, graphics::TextAlign::Center);
        
        m_ui.clubInfoIconEntity = ctx.world.CreateEntity();
        auto& img = ctx.world.Add<game::components::UIImage>(m_ui.clubInfoIconEntity);
        img = game::components::UIImage::Create("Assets/textures/Club_01_1W_Driver.png", px2 + 10, py + 30);
        img.width = 40.0f; img.height = 40.0f;
        img.layer = layer + 1; img.visible = true;
    }

    // ライパネルの生成
    if (m_ui.liePanelBgEntity == UINT32_MAX) {
        createBg(m_ui.liePanelBgEntity, px3);
        createText(m_ui.lieLabelEntity, px3 + 10, py + 10, pw - 20, 20, 12.0f, {0.8f, 0.8f, 0.8f, 1.0f}, graphics::TextAlign::Left);
        if (auto* t = ctx.world.Get<game::components::UIText>(m_ui.lieLabelEntity)) t->text = L"ライ";
        createText(m_ui.lieValueEntity, px3 + 10, py + 30, pw - 20, 24, 20.0f, {0.3f, 0.9f, 0.3f, 1.0f}, graphics::TextAlign::Center);
        createText(m_ui.lieCondValueEntity, px3 + 10, py + 55, pw - 20, 16, 12.0f, {0.8f, 0.8f, 0.8f, 1.0f}, graphics::TextAlign::Center);
    }

    // === 更新処理 ===
    wchar_t buf[64];
    
    // 距離
    if (auto* t = ctx.world.Get<game::components::UIText>(m_ui.distValueEntity)) {
        swprintf(buf, 64, L"%d m", (int)distanceToTarget);
        t->text = buf;
    }
    if (auto* t = ctx.world.Get<game::components::UIText>(m_ui.heightValueEntity)) {
        const wchar_t* arrow = (heightDiff < 0) ? L"\u2B07" : L"\u2B06"; // ⬇ or ⬆
        swprintf(buf, 64, L"高低差 %.1f m %ls", heightDiff, arrow);
        t->text = buf;
    }

    // クラブ
    if (auto* t = ctx.world.Get<game::components::UIText>(m_ui.clubInfoNameEntity)) t->text = core::ToWString(currentClub.name);
    if (auto* t = ctx.world.Get<game::components::UIText>(m_ui.clubInfoShortNameEntity)) t->text = core::ToWString(currentClub.shortName + " " + currentClub.categoryEN);
    if (auto* img = ctx.world.Get<game::components::UIImage>(m_ui.clubInfoIconEntity)) {
        if (img->texturePath != currentClub.iconTexture) {
            img->texturePath = currentClub.iconTexture;
            img->textureSRV = nullptr; // force reload
        }
    }

    // ライ
    std::wstring lieStr = L"フェアウェイ";
    std::wstring condStr = L"コンディション: 良好";
    DirectX::XMFLOAT4 lieColor = {0.3f, 0.9f, 0.3f, 1.0f};
    
    switch (lie) {
        case game::components::TerrainMaterial::Rough: 
            lieStr = L"ラフ"; condStr = L"コンディション: やや重い"; lieColor = {0.5f, 0.7f, 0.2f, 1.0f}; break;
        case game::components::TerrainMaterial::Bunker: 
            lieStr = L"バンカー"; condStr = L"コンディション: 重い"; lieColor = {0.8f, 0.7f, 0.4f, 1.0f}; break;
        case game::components::TerrainMaterial::Green: 
            lieStr = L"グリーン"; condStr = L"コンディション: 良好"; lieColor = {0.2f, 0.8f, 0.4f, 1.0f}; break;
        case game::components::TerrainMaterial::Ice: 
            lieStr = L"アイス"; condStr = L"コンディション: 滑る"; lieColor = {0.6f, 0.9f, 1.0f, 1.0f}; break;
        case game::components::TerrainMaterial::Water: 
            lieStr = L"ウォーター"; condStr = L"コンディション: OB"; lieColor = {0.2f, 0.4f, 0.9f, 1.0f}; break;
        case game::components::TerrainMaterial::Lava: 
            lieStr = L"溶岩"; condStr = L"コンディション: OB"; lieColor = {0.9f, 0.2f, 0.1f, 1.0f}; break;
        case game::components::TerrainMaterial::Stone:
            lieStr = L"ストーン"; condStr = L"コンディション: 跳ねやすい"; lieColor = {0.6f, 0.6f, 0.65f, 1.0f}; break;
        default: break;
    }
    
    if (auto* t = ctx.world.Get<game::components::UIText>(m_ui.lieValueEntity)) {
        t->text = lieStr;
        t->style.color = lieColor;
    }
    if (auto* t = ctx.world.Get<game::components::UIText>(m_ui.lieCondValueEntity)) {
        t->text = condStr;
    }
}

// (互換用 UpdateGuideUI)
void WikiGolfHUD::UpdateGuideUI(core::GameContext& ctx,
                                 const game::components::GolfGameState& state) {
    // 現在は UpdateCourseInfoPanel が担当
}

// =====================================================
// ゲージ系 API
// =====================================================

void WikiGolfHUD::UpdatePowerGauge(core::GameContext& ctx,
                                    float fillValue, float markerValue,
                                    float minPower, float maxPower)
{
    if (m_ui.gaugeBarEntity != UINT32_MAX &&
        ctx.world.Has<game::components::UIBarGauge>(m_ui.gaugeBarEntity))
    {
        auto* gauge = ctx.world.Get<game::components::UIBarGauge>(m_ui.gaugeBarEntity);
        float normalizedFill   = 0.0f;
        float normalizedMarker = 0.0f;
        if (maxPower > minPower && maxPower > 0.0f) {
            normalizedFill   = (fillValue   - minPower) / (maxPower - minPower);
            normalizedMarker = (markerValue - minPower) / (maxPower - minPower);
        }
        gauge->value = std::clamp(normalizedFill, 0.0f, 1.0f);
        gauge->markerValue =
            gauge->mode == game::components::UIBarGaugeMode::Power
                ? gauge->value
                : std::clamp(normalizedMarker, 0.0f, 1.0f);
        gauge->showMarker = true;
        gauge->isVisible = true;
    }
}

void WikiGolfHUD::ResetShotUI(core::GameContext& ctx) {
    UpdateJudge(ctx, L"", {1.0f, 1.0f, 1.0f, 1.0f});

    if (m_ui.shotPanelAccuracyValueEntity != UINT32_MAX &&
        ctx.world.Has<game::components::UIText>(m_ui.shotPanelAccuracyValueEntity))
    {
        auto* t = ctx.world.Get<game::components::UIText>(m_ui.shotPanelAccuracyValueEntity);
        t->text = L"---";
        t->style.color = game::ui::kColorWhite;
    }
    if (m_ui.gaugeBarEntity != UINT32_MAX &&
        ctx.world.Has<game::components::UIBarGauge>(m_ui.gaugeBarEntity))
    {
        auto* gauge = ctx.world.Get<game::components::UIBarGauge>(m_ui.gaugeBarEntity);
        gauge->value           = 0.0f;
        gauge->markerValue     = 0.0f;
        gauge->isVisible       = false;
        gauge->mode            = game::components::UIBarGaugeMode::Power;
        gauge->showImpactZones = false;
        gauge->showConfirmedMarker = false;
    }

    SetShotPhaseUIVisible(ctx, false);
}

void WikiGolfHUD::SetGaugeVisible(core::GameContext& ctx, bool visible) {
    if (m_ui.gaugeBarEntity != UINT32_MAX &&
        ctx.world.Has<game::components::UIBarGauge>(m_ui.gaugeBarEntity))
    {
        ctx.world.Get<game::components::UIBarGauge>(m_ui.gaugeBarEntity)->isVisible = visible;
    }
}

void WikiGolfHUD::SetImpactZonesVisible(core::GameContext& ctx, bool visible) {
    if (m_ui.gaugeBarEntity != UINT32_MAX &&
        ctx.world.Has<game::components::UIBarGauge>(m_ui.gaugeBarEntity))
    {
        ctx.world.Get<game::components::UIBarGauge>(m_ui.gaugeBarEntity)->showImpactZones = visible;
    }
}

void WikiGolfHUD::UpdateJudge(core::GameContext& ctx,
                               const std::wstring& text,
                               const DirectX::XMFLOAT4& color)
{
    if (m_ui.judgeEntity != UINT32_MAX &&
        ctx.world.Has<game::components::UIText>(m_ui.judgeEntity))
    {
        auto* t = ctx.world.Get<game::components::UIText>(m_ui.judgeEntity);
        t->text = text;
        t->style.color = color;
    }
    if (m_ui.shotPanelAccuracyValueEntity != UINT32_MAX &&
        ctx.world.Has<game::components::UIText>(m_ui.shotPanelAccuracyValueEntity))
    {
        auto* t = ctx.world.Get<game::components::UIText>(m_ui.shotPanelAccuracyValueEntity);
        t->text = text;
        t->style.color = color;
    }
}

// -------------------------------------------------------
// 通常時 <-> ショット時 UI 切り替え
// 入力: shotPhase = true なら ゲージ表示、クラブリスト薄く
// 変更: ショットボタン/操作ヘルプの visible を制御
// 出力: 各エンティティの visible 変更
// -------------------------------------------------------
void WikiGolfHUD::SetShotPhaseUIVisible(core::GameContext& ctx, bool shotPhase) {
    // ショットフェーズではショットボタンと操作ヘルプを非表示
    auto setVis = [&](ecs::Entity e, bool vis) {
        if (e != UINT32_MAX && ctx.world.IsAlive(e)) {
            if (auto* t = ctx.world.Get<game::components::UIText>(e)) t->visible = vis;
        }
    };
    auto setAlpha = [&](ecs::Entity e, float a) {
        if (e != UINT32_MAX && ctx.world.IsAlive(e)) {
            if (auto* t = ctx.world.Get<game::components::UIText>(e)) t->style.color.w = a;
        }
    };

    setVis(m_ui.shotButtonBgEntity,   !shotPhase);
    setVis(m_ui.shotButtonTextEntity, !shotPhase);
    setVis(m_ui.controlHintEntity,    !shotPhase);

    const bool bottomInfoVisible = !shotPhase;
    setVis(m_ui.distPanelBgEntity, bottomInfoVisible);
    setVis(m_ui.distLabelEntity, bottomInfoVisible);
    setVis(m_ui.distValueEntity, bottomInfoVisible);
    setVis(m_ui.heightLabelEntity, bottomInfoVisible);
    setVis(m_ui.heightValueEntity, bottomInfoVisible);
    setVis(m_ui.clubInfoPanelBgEntity, bottomInfoVisible);
    setVis(m_ui.clubInfoLabelEntity, bottomInfoVisible);
    setVis(m_ui.clubInfoNameEntity, bottomInfoVisible);
    setVis(m_ui.clubInfoShortNameEntity, bottomInfoVisible);
    setVis(m_ui.liePanelBgEntity, bottomInfoVisible);
    setVis(m_ui.lieLabelEntity, bottomInfoVisible);
    setVis(m_ui.lieValueEntity, bottomInfoVisible);
    setVis(m_ui.lieCondLabelEntity, bottomInfoVisible);
    setVis(m_ui.lieCondValueEntity, bottomInfoVisible);
    if (m_ui.clubInfoIconEntity != UINT32_MAX && ctx.world.IsAlive(m_ui.clubInfoIconEntity)) {
        if (auto* img = ctx.world.Get<game::components::UIImage>(m_ui.clubInfoIconEntity)) {
            img->visible = bottomInfoVisible;
        }
    }

    // クラブリスト: ショット時は選択中だけ残して視界を整理
    float clubAlpha = shotPhase ? 0.35f : 1.0f;
    for (size_t i = 0; i < m_ui.clubBgEntities.size(); ++i) {
        const bool selected = i < m_ui.clubArrowEntities.size() &&
                              ctx.world.Get<game::components::UIText>(m_ui.clubArrowEntities[i]) &&
                              ctx.world.Get<game::components::UIText>(m_ui.clubArrowEntities[i])->visible;
        const bool rowVisible = !shotPhase || selected;

        if (i < m_ui.clubBgEntities.size()) setVis(m_ui.clubBgEntities[i], rowVisible);
        if (i < m_ui.clubNameEntities.size()) {
            setVis(m_ui.clubNameEntities[i], rowVisible);
            setAlpha(m_ui.clubNameEntities[i], clubAlpha);
        }
        if (i < m_ui.clubSubNameEntities.size()) {
            setVis(m_ui.clubSubNameEntities[i], rowVisible);
            setAlpha(m_ui.clubSubNameEntities[i], clubAlpha * 0.9f);
        }
        if (i < m_ui.clubArrowEntities.size()) setVis(m_ui.clubArrowEntities[i], rowVisible && selected);
        if (i < m_ui.clubIconEntities.size() && m_ui.clubIconEntities[i] != UINT32_MAX &&
            ctx.world.IsAlive(m_ui.clubIconEntities[i])) {
            if (auto* img = ctx.world.Get<game::components::UIImage>(m_ui.clubIconEntities[i])) {
                img->visible = rowVisible;
                img->alpha = shotPhase ? 0.35f : (img->alpha > 0.5f ? 1.0f : 0.4f);
            }
        }

        if (i < m_ui.clubBgEntities.size() && m_ui.clubBgEntities[i] != UINT32_MAX &&
            ctx.world.IsAlive(m_ui.clubBgEntities[i])) {
            if (auto* t = ctx.world.Get<game::components::UIText>(m_ui.clubBgEntities[i])) {
                t->style.bgColor.w = shotPhase ? 0.35f : (
                    t->style.borderColor.x > 0.15f ? 0.8f : 0.7f);
            }
        }
    }

    // ゲージパネル: ショット時のみ表示
    setVis(m_ui.shotPanelBgEntity,          shotPhase);
    setVis(m_ui.shotPanelStepEntity,        shotPhase);
    setVis(m_ui.shotPanelTitleEntity,       shotPhase);
    setVis(m_ui.shotPanelHintEntity,        shotPhase);
    setVis(m_ui.shotPanelPowerLabelEntity,    shotPhase);
    setVis(m_ui.shotPanelPowerValueEntity,    shotPhase);
    setVis(m_ui.shotPanelAccuracyLabelEntity, shotPhase);
    setVis(m_ui.shotPanelAccuracyValueEntity, shotPhase);
    setVis(m_ui.shotPanelClubLabelEntity,     shotPhase);
    SetGaugeVisible(ctx, shotPhase);
} // SetShotPhaseUIVisible

// -----------------------------------------------------------------
// HUD全体の表示/非表示切り替え（ロード中は非表示）
// 入力: visible=false → 全UIエンティティを非表示
// 変更: UIText の visible フラグを一括更新
// 出力: なし（副作用: ECS UIText コンポーネントの visible 変更）
// -----------------------------------------------------------------
void WikiGolfHUD::SetVisible(core::GameContext& ctx, bool visible) {
    // UIText 系エンティティの可視フラグを変更するヘルパー
    auto setVis = [&](ecs::Entity e, bool v) {
        if (e == UINT32_MAX) return;
        if (auto* t = ctx.world.Get<components::UIText>(e)) t->visible = v;
    };
    auto setVisImg = [&](ecs::Entity e, bool v) {
        if (e == UINT32_MAX) return;
        if (auto* img = ctx.world.Get<components::UIImage>(e)) img->visible = v;
    };
    auto setGaugeVis = [&](ecs::Entity e, bool v) {
        if (e == UINT32_MAX) return;
        if (auto* gauge = ctx.world.Get<components::UIBarGauge>(e)) gauge->isVisible = v;
    };

    // ブラウザ風情報パネル
    setVis(m_ui.browserBgEntity,          visible);
    setVis(m_ui.browserTabIconEntity,     visible);
    setVis(m_ui.browserCurrentPageEntity, visible);
    setVis(m_ui.browserTargetEntity,      visible);
    setVis(m_ui.browserShotInfoEntity,    visible);
    setVis(m_ui.browserHistoryEntity,     visible);
    setVis(m_ui.clubHeaderEntity,         visible);
    setVis(m_ui.headerEntity,             visible);
    setVis(m_ui.shotCountEntity,          visible);
    setVis(m_ui.infoEntity,              visible);
    setVis(m_ui.pathEntity,              visible);

    // 風カード
    setVis(m_ui.windEntity,           visible);
    setVisImg(m_ui.windArrowEntity,   visible);
    setVis(m_ui.windCardLabelEntity,  visible);
    setVis(m_ui.windCardValueEntity,  visible);
    setVis(m_ui.windCardUnitEntity,   visible);

    // クラブ選択リスト
    for (auto e : m_ui.clubBgEntities)      setVis(e, visible);
    for (auto e : m_ui.clubIconEntities)    setVisImg(e, visible);
    for (auto e : m_ui.clubNameEntities)    setVis(e, visible);
    for (auto e : m_ui.clubSubNameEntities) setVis(e, visible);
    for (auto e : m_ui.clubArrowEntities)   setVis(e, visible);

    // ショットボタン
    setVis(m_ui.shotButtonBgEntity,   visible);
    setVis(m_ui.shotButtonTextEntity, visible);

    // 操作ヘルプ
    setVis(m_ui.controlHintEntity, visible);

    // 距離・クラブ・ライパネル
    setVis(m_ui.distPanelBgEntity,     visible);
    setVis(m_ui.distLabelEntity,       visible);
    setVis(m_ui.distValueEntity,       visible);
    setVis(m_ui.heightLabelEntity,     visible);
    setVis(m_ui.heightValueEntity,     visible);
    setVis(m_ui.clubInfoPanelBgEntity, visible);
    setVis(m_ui.clubInfoLabelEntity,   visible);
    setVis(m_ui.clubInfoNameEntity,    visible);
    setVisImg(m_ui.clubInfoIconEntity, visible);
    setVis(m_ui.clubInfoShortNameEntity, visible);
    setVis(m_ui.liePanelBgEntity,     visible);
    setVis(m_ui.lieLabelEntity,        visible);
    setVis(m_ui.lieValueEntity,        visible);
    setVis(m_ui.lieCondLabelEntity,    visible);
    setVis(m_ui.lieCondValueEntity,    visible);

    for (auto e : m_ui.minimapDecorationEntities) setVis(e, visible);

    if (!visible) {
        setGaugeVis(m_ui.gaugeBarEntity, false);
        setVis(m_ui.gaugeFillEntity,   false);
        setVis(m_ui.gaugeMarkerEntity, false);
        setVis(m_ui.judgeEntity,       false);
        setVis(m_ui.shotPanelBgEntity,          false);
        setVis(m_ui.shotPanelStepEntity,        false);
        setVis(m_ui.shotPanelTitleEntity,       false);
        setVis(m_ui.shotPanelHintEntity,        false);
        setVis(m_ui.shotPanelPowerLabelEntity,    false);
        setVis(m_ui.shotPanelPowerValueEntity,    false);
        setVis(m_ui.shotPanelAccuracyLabelEntity, false);
        setVis(m_ui.shotPanelAccuracyValueEntity, false);
        setVis(m_ui.shotPanelClubLabelEntity,     false);
    }
}

} // namespace controllers
} // namespace game
