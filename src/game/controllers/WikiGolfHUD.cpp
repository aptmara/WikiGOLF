/**
 * @file WikiGolfHUD.cpp
 * @brief 通常時画面のHUDを管理するコントローラー
 *
 * デザインの方針:
 *   - すべてのパネルは ApplySurfaceStyle() が作る単一の見た目（単色・
 *     同一角丸・同一枠線・同一影）だけを使う。パネルごとに固有の
 *     グラデーションや影を作らない。
 *   - 色は用途で固定する: kColorAccent=操作可能な要素、kColorSpecial=
 *     目的地、kColorSuccess/kColorWarning/kColorError=プレー結果の
 *     フィードバック、kColorTextSub=すべてのラベル。装飾のための
 *     色分けはしない。
 *   - 常時明滅するアイドルアニメーションは持たない。動きはフェーズ
 *     遷移（ショット開始/終了）など、状態が変わった瞬間にのみ使う。
 *
 * 入力: GolfGameState, ShotState, クラブ情報, 風情報, カメラ情報
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
#include "../utils/ShotGaugeRules.h"
#include "../utils/UIConstants.h"
#include <algorithm>
#include <cmath>
#include <format>

namespace game {
namespace controllers {

namespace {
bool SetTextIfChanged(game::components::UIText& text,
                      const std::wstring& value) {
    if (text.text == value) {
        return false;
    }
    text.text = value;
    return true;
}

bool SetColorIfChanged(DirectX::XMFLOAT4& color,
                       const DirectX::XMFLOAT4& value) {
    if (color.x == value.x && color.y == value.y && color.z == value.z &&
        color.w == value.w) {
        return false;
    }
    color = value;
    return true;
}

// 全パネル共通の面スタイル。単色・単一角丸・単一枠線・単一影のみ。
// 個別パネルはここに手を加えず、この関数だけを通して見た目を揃える。
void ApplySurfaceStyle(graphics::TextStyle& style,
                       float radius = game::ui::kRadiusPanel) {
    style.bgColor        = game::ui::kColorBgDark;
    style.useGradient     = false;
    style.bgGradientEnd  = {0.0f, 0.0f, 0.0f, 0.0f};
    style.cornerRadius    = radius;
    style.borderWidth     = game::ui::kBorderWidthThin;
    style.borderColor     = game::ui::kColorBorder;
    style.hasShadow       = true;
    style.shadowColor     = game::ui::kShadowColor;
    style.shadowOffsetX   = 0.0f;
    style.shadowOffsetY   = game::ui::kShadowOffsetY;
}

// 選択中/強調状態の行に使う面スタイル。ApplySurfaceStyle と同じ角丸・
// 枠線幅の"文法"を保ったまま、面色と枠線色だけをアクセントに寄せる。
void ApplyActiveRowStyle(graphics::TextStyle& style) {
    style.bgColor        = game::ui::kColorSurfaceRaised;
    style.useGradient     = false;
    style.bgGradientEnd  = {0.0f, 0.0f, 0.0f, 0.0f};
    style.cornerRadius    = game::ui::kRadiusChip;
    style.borderWidth     = game::ui::kBorderWidthThin;
    style.borderColor     = game::ui::kColorAccent;
    style.borderColor.w   = 0.75f;
    style.hasShadow       = true;
    style.shadowColor     = game::ui::kShadowColor;
    style.shadowOffsetX   = 0.0f;
    style.shadowOffsetY   = game::ui::kShadowOffsetY;
}

void ApplyRowStyle(graphics::TextStyle& style) {
    style.bgColor        = game::ui::kColorBgDark;
    style.useGradient     = false;
    style.bgGradientEnd  = {0.0f, 0.0f, 0.0f, 0.0f};
    style.cornerRadius    = game::ui::kRadiusChip;
    style.borderWidth     = game::ui::kBorderWidthThin;
    style.borderColor     = game::ui::kColorBorder;
    style.hasShadow       = true;
    style.shadowColor     = game::ui::kShadowColor;
    style.shadowOffsetX   = 0.0f;
    style.shadowOffsetY   = game::ui::kShadowOffsetY;
}
} // namespace

// =====================================================
// Initialize
// =====================================================

void WikiGolfHUD::Initialize(core::GameContext& ctx) {
    InitializeCourseInfoPanel(ctx);
    InitializeWindCard(ctx);
    InitializeClubSelectList(ctx);  // 初期は空、Update時に動的生成
    InitializeLandingPreviewButton(ctx);
    InitializeShotButton(ctx);
    InitializeControlHint(ctx);
    InitializeShotGaugePanel(ctx);
    InitializeJudgeText(ctx);
    InitializeMinimapUI(ctx);
}

// -------------------------------------------------------
// 左上: 現在地/目的地パネル
// ラベル(小・淡色)→値(大)の2段構成をCURRENT/TARGETそれぞれに持たせ、
// "見出し"と"データ"の区別を常に一目でわかるようにする。
// -------------------------------------------------------
void WikiGolfHUD::InitializeCourseInfoPanel(core::GameContext& ctx) {
    const float x = game::ui::kBrowserHudX;
    const float y = game::ui::kBrowserHudY;
    const float panelW = 372.0f;
    const float panelH = 150.0f;

    // === 背景パネル ===
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.x = x;
        t.y = y;
        t.width  = panelW;
        t.height = panelH;
        ApplySurfaceStyle(t.style);
        t.visible = true;
        t.layer   = game::ui::kLayerBrowser - 1;
        m_ui.browserBgEntity = e;
    }

    // WIKI バッジ（識別用の小さなチップ。装飾ではなく識別のみに徹する）
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text = L"WIKI";
        t.x = x + 14.0f;
        t.y = y + 12.0f;
        t.width  = 48.0f;
        t.height = 20.0f;
        t.style  = graphics::TextStyle::CardLabel();
        t.style.align      = graphics::TextAlign::Center;
        t.style.bgColor    = game::ui::kColorSurfaceRaised;
        t.style.cornerRadius = game::ui::kRadiusChip * 0.75f;
        t.style.borderWidth  = game::ui::kBorderWidthThin;
        t.style.borderColor  = game::ui::kColorBorder;
        t.visible = true;
        t.layer   = game::ui::kLayerBrowser;
        m_ui.browserTabIconEntity = e;
        m_ui.headerEntity = e; // 互換
    }

    // "CURRENT" ラベル
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text  = L"CURRENT";
        t.x     = x + 14.0f;
        t.y     = y + 44.0f;
        t.width = panelW - 28.0f;
        t.height = 16.0f;
        t.style  = graphics::TextStyle::CardLabel();
        t.visible = true;
        t.layer   = game::ui::kLayerBrowser + 1;
        m_ui.browserCurrentLabelEntity = e;
    }

    // 現在地名（値）
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text  = L"Loading...";
        t.x     = x + 14.0f;
        t.y     = y + 59.0f;
        t.width = panelW - 28.0f;
        t.height = 26.0f;
        t.style  = graphics::TextStyle::BrowserURL();
        t.style.fontSize = game::ui::kBrowserFontSize;
        t.visible = true;
        t.layer   = game::ui::kLayerBrowser + 1;
        m_ui.browserCurrentPageEntity = e;
    }

    // "TARGET" ラベル
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text  = L"TARGET";
        t.x     = x + 14.0f;
        t.y     = y + 90.0f;
        t.width = panelW - 28.0f;
        t.height = 16.0f;
        t.style  = graphics::TextStyle::CardLabel();
        t.visible = true;
        t.layer   = game::ui::kLayerBrowser + 1;
        m_ui.browserTargetLabelEntity = e;
    }

    // 目的地名（値・金色は目的地専用）
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text  = L"Target page...";
        t.x     = x + 14.0f;
        t.y     = y + 105.0f;
        t.width = panelW - 28.0f;
        t.height = 24.0f;
        t.style  = graphics::TextStyle::GoalHighlight();
        t.style.fontSize = game::ui::kBrowserGoalFontSize;
        t.visible = true;
        t.layer   = game::ui::kLayerBrowser + 1;
        m_ui.browserTargetEntity = e;
    }

    // Shots: N / Par N / Route N（下部キャプション行。ここは互換用に
    // browserShotInfoEntity として使い続けるが、パネル外の余白に配置する）
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text  = L"SHOT 0   PAR ?   ROUTE 0";
        t.x     = x + 14.0f;
        t.y     = y + panelH + 8.0f;
        t.width = panelW - 28.0f;
        t.height = 18.0f;
        t.style  = graphics::TextStyle::BrowserSub();
        t.style.fontSize = game::ui::kBrowserSubFontSize;
        t.visible = true;
        t.layer   = game::ui::kLayerBrowser;
        m_ui.browserShotInfoEntity = e;
        m_ui.shotCountEntity = e;
    }

    // 経路履歴（現状は未使用のまま非表示エンティティとして保持）
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text  = L"";
        t.style  = graphics::TextStyle::BrowserSub();
        t.style.color = game::ui::kColorTextSub;
        t.visible = false;
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
        t.text = L"CLUB SELECT   Q / E";
        t.x = game::ui::kClubPanelX;
        t.y = game::ui::kClubPanelY - 26.0f;
        t.width = game::ui::kClubItemW;
        t.height = 18.0f;
        t.style = graphics::TextStyle::CardLabel();
        t.style.color = game::ui::kColorAccent; // 操作可能な領域である目印
        t.visible = true;
        t.layer = game::ui::kLayerClubSelect;
        m_ui.clubHeaderEntity = e;
    }
}

// -------------------------------------------------------
// 上中央: 風情報カード
// -------------------------------------------------------
void WikiGolfHUD::InitializeWindCard(core::GameContext& ctx) {
    constexpr float kWCardW = game::ui::kWindCardWidth;
    constexpr float kWCardX = game::ui::kWindCardX;
    constexpr float kWCardY = game::ui::kWindCardY;

    // 背景（他パネルと同一の面スタイル）
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.x = kWCardX;
        t.y = kWCardY;
        t.width = kWCardW;
        t.height = 92.0f;
        ApplySurfaceStyle(t.style);
        t.visible = true;
        t.layer = game::ui::kLayerWind - 1;
        m_ui.windEntity = e;
    }

    // ラベル
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text  = L"WIND";
        t.x     = kWCardX + 14.0f;
        t.y     = kWCardY + 10.0f;
        t.width = kWCardW - 28.0f;
        t.height = 18.0f;
        t.style  = graphics::TextStyle::CardLabel();
        t.style.fontSize = game::ui::kWindLabelFontSize;
        t.style.align    = graphics::TextAlign::Left;
        t.visible = true;
        t.layer   = game::ui::kLayerWind;
        m_ui.windCardLabelEntity = e;
    }

    // 風速値 (数字部分)
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text  = L"--";
        t.x     = kWCardX + 14.0f;
        t.y     = kWCardY + 30.0f;
        t.width = 70.0f;
        t.height = 38.0f;
        t.style  = graphics::TextStyle::CardValue();
        t.style.fontSize = game::ui::kWindValueFontSize;
        t.style.align    = graphics::TextAlign::Left;
        t.visible = true;
        t.layer   = game::ui::kLayerWind + 1;
        m_ui.windCardValueEntity = e;
    }

    // 風向きと単位（方角は読み取り専用の情報なので中立色で統一する）
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text  = L"↑  m/s";
        t.x     = kWCardX + 76.0f;
        t.y     = kWCardY + 38.0f;
        t.width = 58.0f;
        t.height = 32.0f;
        t.style  = graphics::TextStyle::CardValue();
        t.style.color    = game::ui::kColorTextSub;
        t.style.fontSize = 14.0f;
        t.style.align    = graphics::TextAlign::Right;
        t.visible = true;
        t.layer   = game::ui::kLayerWind + 1;
        m_ui.windCardUnitEntity = e;
    }

    // 互換用画像エンティティ
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

    // 背景枠（他パネルと同一の面スタイル）
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.x = mx - 6.0f;
        t.y = my - 6.0f;
        t.width = mw + 12.0f;
        t.height = mh + 36.0f;
        ApplySurfaceStyle(t.style);
        t.visible = true;
        t.layer = game::ui::kLayerMinimap - 1; // ミニマップの背後
        m_ui.minimapDecorationEntities.push_back(e);
    }

    // マップ本体の縁取り。地形レンダリングが明るい色になりがちで、暗い
    // HUDの中で縁のない白い板のように浮いて見えるため、マップ画像の上
    // (kLayerMinimap+1相当)に他パネルと同じ暗色トーンの枠+薄いスクリムを
    // 重ねて馴染ませる。アクセントカラーは操作要素専用のため額縁には
    // 使わない（使うと安っぽい発光リングに見えてしまう）。
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.x = mx;
        t.y = my;
        t.width = mw;
        t.height = mh;
        t.style.bgColor = {0.0f, 0.0f, 0.0f, 0.22f}; // 明るいマップ全体をわずかに沈める
        t.style.borderWidth = 3.0f;
        t.style.borderColor = game::ui::kColorBgDark;
        t.style.cornerRadius = game::ui::kRadiusPanel;
        t.visible = true;
        t.layer = layer; // マップ画像より前面
        m_ui.minimapDecorationEntities.push_back(e);
    }

    // N 矢印
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text = L"N\n▲";
        t.x = mx + mw - 26.0f;
        t.y = my + 6.0f;
        t.width = 22.0f;
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
        t.text = txt;
        t.x = mx + mw - 42.0f;
        t.y = my + oy;
        t.width = 36.0f;
        t.height = 15.0f;
        t.style = graphics::TextStyle::Guide();
        t.style.fontSize = 10.0f;
        t.style.color = {0.8f, 0.8f, 0.8f, 1.0f};
        t.style.align = graphics::TextAlign::Right;
        t.visible = true;
        t.layer = layer;
        m_ui.minimapDecorationEntities.push_back(e);
    };
    createScale(30.0f, L"150m");
    createScale(mh / 2.0f, L" 50m");
    createScale(mh - 15.0f, L"  0m");

    // 凡例 (現在地)。実際のマーカー色 (MinimapController) と一致させる。
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text = L"● BALL";
        t.x = mx + 8.0f;
        t.y = my + mh + 5.0f;
        t.width = 72.0f;
        t.height = 20.0f;
        t.style = graphics::TextStyle::Guide();
        t.style.fontSize = 12.0f;
        t.style.color = {0.18f, 0.85f, 1.0f, 1.0f};
        t.style.align = graphics::TextAlign::Left;
        t.visible = true;
        t.layer = layer;
        m_ui.minimapDecorationEntities.push_back(e);
    }

    // 凡例 (ピン位置)。実際のフラグ色 (MinimapController) と一致させる。
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text = L"⛳ TARGET";
        t.x = mx + 92.0f;
        t.y = my + mh + 5.0f;
        t.width = 80.0f;
        t.height = 20.0f;
        t.style = graphics::TextStyle::Guide();
        t.style.fontSize = 12.0f;
        t.style.color = {1.0f, 0.2f, 0.2f, 1.0f};
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
// クラブ選択パネル横: 着弾点プレビュー(トップビュー)トグルボタン
// -------------------------------------------------------
void WikiGolfHUD::InitializeLandingPreviewButton(core::GameContext& ctx) {
    const float bx = game::ui::kLandingPreviewBtnX;
    const float by = game::ui::kLandingPreviewBtnY;
    const float bw = game::ui::kLandingPreviewBtnW;
    const float bh = game::ui::kLandingPreviewBtnH;

    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text  = L"";
        t.x     = bx;
        t.y     = by;
        t.width = bw;
        t.height = bh;
        ApplyRowStyle(t.style);
        t.visible = true;
        t.layer   = game::ui::kLayerClubSelect;
        m_ui.landingPreviewBtnBgEntity = e;
    }
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text  = L"⛳ 着弾予測";
        t.x     = bx;
        t.y     = by;
        t.width = bw;
        t.height = bh;
        t.style  = graphics::TextStyle::BrowserSub();
        t.style.fontSize = game::ui::kLandingPreviewBtnFontSize;
        t.style.align    = graphics::TextAlign::Center;
        t.style.color    = game::ui::kColorWhite;
        t.style.bgColor  = {0.0f, 0.0f, 0.0f, 0.0f};
        t.style.borderWidth = 0.0f;
        t.visible = true;
        t.layer   = game::ui::kLayerClubSelect + 1;
        m_ui.landingPreviewBtnTextEntity = e;
    }
}

// -------------------------------------------------------
// 右下: ショットボタン
// -------------------------------------------------------
void WikiGolfHUD::InitializeShotButton(core::GameContext& ctx) {
    const float bx = game::ui::kShotBtnX;
    const float by = game::ui::kShotBtnY;
    const float bw = game::ui::kShotBtnW;
    const float bh = game::ui::kShotBtnH;

    // 背景（唯一の主操作なのでアクセント色の面を使う。常時明滅はさせない）
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text  = L"";
        t.x     = bx;
        t.y     = by;
        t.width = bw;
        t.height = bh;
        t.style.bgColor      = game::ui::kColorShotBtn;
        t.style.useGradient  = false;
        t.style.cornerRadius = game::ui::kRadiusPanel;
        t.style.borderWidth  = 2.0f;
        t.style.borderColor  = game::ui::kColorShotBtnBorder;
        t.style.hasShadow    = true;
        t.style.shadowColor  = game::ui::kShadowColor;
        t.style.shadowOffsetX = 0.0f;
        t.style.shadowOffsetY = game::ui::kShadowOffsetY;
        t.visible = true;
        t.layer   = game::ui::kLayerShotButton;
        m_ui.shotButtonBgEntity = e;
    }

    // テキスト "SHOT" とサブテキスト
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text  = L"SHOT   SPACE / CLICK";
        t.x     = bx;
        t.y     = by + 22.0f;
        t.width = bw;
        t.height = 30.0f;
        t.style  = graphics::TextStyle::Guide();
        t.style.fontSize  = game::ui::kShotBtnFontSize;
        t.style.align     = graphics::TextAlign::Center;
        t.style.color     = {1.0f, 1.0f, 1.0f, 1.0f};
        t.style.hasShadow = true;
        t.style.shadowColor = {0.0f, 0.0f, 0.0f, 0.8f};
        t.style.shadowOffsetX = 1.0f;
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
    t.text  = L"Q / E  CLUB     RMB  CAMERA     M  MAP";
    t.x     = game::ui::kControlHintX;
    t.y     = game::ui::kControlHintY;
    t.width = game::ui::kControlHintW;
    t.height = game::ui::kControlHintH;
    t.style  = graphics::TextStyle::BrowserSub();
    t.style.fontSize = game::ui::kControlHintFont;
    t.style.color    = game::ui::kColorTextSub;
    ApplySurfaceStyle(t.style, game::ui::kRadiusChip);
    t.style.bgColor.w = 0.72f;

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
        ApplySurfaceStyle(t.style);
        t.visible = false;
        t.layer = game::ui::kLayerShotPanel - 2;
        m_ui.shotPanelBgEntity = e;
    }

    // 入力ステップ
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text = L"01 / 02";
        t.x = game::ui::kShotPanelX;
        t.y = game::ui::kShotPanelY - 22.0f;
        t.width = game::ui::kShotStepWidth;
        t.height = 24.0f;
        t.style = graphics::TextStyle::ShotPanelLabel();
        t.style.align = graphics::TextAlign::Center;
        t.style.fontSize = game::ui::kShotLabelFontSize;
        t.style.color = game::ui::kColorWhite;
        t.style.bgColor = game::ui::kColorSurfaceRaised;
        t.style.borderColor = game::ui::kColorAccent;
        t.style.borderColor.w = 0.75f;
        t.style.borderWidth = game::ui::kBorderWidthThin;
        t.style.cornerRadius = game::ui::kRadiusChip;
        t.visible = false;
        t.layer = game::ui::kLayerShotPanel + 1;
        m_ui.shotPanelStepEntity = e;
    }

    // フェーズ見出し
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text = L"パワー調整";
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
        t.text = L"左クリックで確定　　右クリックでキャンセル";
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

    // パワー ラベル
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text  = L"パワー";
        t.x     = game::ui::kShotPanelX;
        t.y     = game::ui::kShotPanelY + 38.0f;
        t.width = 120.0f;
        t.height = 22.0f;
        t.style  = graphics::TextStyle::ShotPanelLabel();
        t.style.fontSize     = game::ui::kShotLabelFontSize;
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

    // 正確性 ラベル
    {
        auto e = ctx.world.CreateEntity();
        auto& t = ctx.world.Add<game::components::UIText>(e);
        t.text  = L"正確性";
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
        gauge.impactWidthSpecial =
            game::utils::GetImpactZoneVisualWidth(game::ui::kThresholdSpecial);
        gauge.impactWidthGreat =
            game::utils::GetImpactZoneVisualWidth(game::ui::kThresholdGreat);
        gauge.impactWidthNice =
            game::utils::GetImpactZoneVisualWidth(game::ui::kThresholdNice);
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
    t.style.fontSize = 34.0f;
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
                         float confirmedImpact,
                         float windSpeed, const DirectX::XMFLOAT2& windDir, float cameraYaw,
                         const std::vector<ClubUIData>& clubs,
                         int currentClubIndex,
                         float distanceToTarget, float heightDiff)
{
    m_elapsedTime += dt;

    // インパクト確定後、保持表示 → フェードアウトの残り時間を減衰させる。
    m_gaugeDismissRemaining = std::max(0.0f, m_gaugeDismissRemaining - dt);

    const auto previousPhase = m_previousShotPhase;
    if (shotPhase != previousPhase) {
        m_previousShotPhase = shotPhase;
        m_phaseTransition = 0.0f;

        // ImpactTiming を抜けた瞬間（=インパクト確定）だけ保持+フェードタイマーを起動する。
        if (previousPhase == game::components::ShotState::Phase::ImpactTiming) {
            m_gaugeDismissRemaining =
                game::ui::kGaugeHoldDuration + game::ui::kGaugeFadeDuration;
        }
    }
    m_phaseTransition = std::min(1.0f, m_phaseTransition + dt * 7.5f);

    UpdateCourseInfoPanel(ctx, state);
    UpdateWindUI(ctx, windSpeed, windDir, cameraYaw);
    UpdateClubSelectList(ctx, clubs, currentClubIndex);

    ClubUIData currentClubData;
    if (currentClubIndex >= 0 && currentClubIndex < clubs.size()) {
        currentClubData = clubs[currentClubIndex];
    }
    UpdateBottomInfoPanels(ctx, state.currentMaterial);

    // TODO: GuideUI (操作ヘルプ) は後で装飾するが一旦残す
    UpdateGuideUI(ctx, state);
    UpdateShotPhasePanel(ctx, shotPhase, currentPower, confirmedPower,
                         currentImpact, confirmedImpact, currentClubData);
}

namespace {
DirectX::XMFLOAT4 JudgementColor(game::components::ShotJudgement judgement) {
    switch (judgement) {
    case game::components::ShotJudgement::Special: return game::ui::kColorSpecial;
    case game::components::ShotJudgement::Great:   return game::ui::kColorSuccess;
    case game::components::ShotJudgement::Nice:    return game::ui::kColorAccent;
    default:                                       return game::ui::kColorError;
    }
}
} // namespace

void WikiGolfHUD::UpdateShotPhasePanel(
    core::GameContext& ctx,
    game::components::ShotState::Phase shotPhase,
    float currentPower,
    float confirmedPower,
    float currentImpact,
    float confirmedImpact,
    const ClubUIData& currentClub)
{
    const bool powerPhase =
        shotPhase == game::components::ShotState::Phase::PowerCharging;
    const bool impactPhase =
        shotPhase == game::components::ShotState::Phase::ImpactTiming;
    const bool activeShotPhase = powerPhase || impactPhase;
    // インパクト確定直後、判定色つきで保持表示 → フェードアウトする状態。
    const bool dismissing = !activeShotPhase && m_gaugeDismissRemaining > 0.0f;
    // 保持表示の間は1.0、その後 kGaugeFadeDuration かけて0まで滑らかに減衰する。
    const float fadeLinear =
        dismissing ? std::clamp(m_gaugeDismissRemaining / game::ui::kGaugeFadeDuration,
                                0.0f, 1.0f)
                   : 1.0f;
    const float fadeAlpha = fadeLinear * fadeLinear * (3.0f - 2.0f * fadeLinear); // smoothstep

    auto setText = [&](ecs::Entity e, const std::wstring& text) {
        if (e != UINT32_MAX && ctx.world.Has<game::components::UIText>(e)) {
            SetTextIfChanged(*ctx.world.Get<game::components::UIText>(e), text);
        }
    };
    auto setColor = [&](ecs::Entity e, const DirectX::XMFLOAT4& color) {
        if (e != UINT32_MAX && ctx.world.Has<game::components::UIText>(e)) {
            SetColorIfChanged(ctx.world.Get<game::components::UIText>(e)->style.color,
                              color);
        }
    };

    const float shownPower = confirmedPower > 0.0f ? confirmedPower : currentPower;
    // 「0-100%のパワー」ではなく、クラブの基本飛距離からの変位(ヤード)として表示する。
    const int distanceYards = std::clamp(
        static_cast<int>(std::round(currentClub.baseCarryDistance * shownPower)),
        0, 9999);
    // ゲージバーの色分け(閾値判定)にはゲージ位置そのもの(0-100%)を使う。
    const int powerPercent =
        std::clamp(static_cast<int>(std::round(shownPower * 100.0f)), 0, 100);

    // 確定直後だけ0→1で立ち上がるパルス（既存のフェーズ切替イージングを流用）。
    const float pulse = std::max(0.0f, 1.0f - m_phaseTransition);

    if (powerPhase) {
        setText(m_ui.shotPanelStepEntity, L"01 / 02");
        setText(m_ui.shotPanelTitleEntity, L"パワー調整");
        setText(m_ui.shotPanelHintEntity, L"左クリックで確定　　右クリックでキャンセル");
        setText(m_ui.shotPanelPowerLabelEntity, L"距離");
        setText(m_ui.shotPanelPowerValueEntity,
                std::to_wstring(distanceYards) + L"y");
        setText(m_ui.shotPanelAccuracyLabelEntity, L"つぎ");
        setText(m_ui.shotPanelAccuracyValueEntity, L"インパクト");
        setColor(m_ui.shotPanelPowerValueEntity, game::ui::kColorWarning);
        setColor(m_ui.shotPanelAccuracyValueEntity, game::ui::kColorTextSub);
    } else if (impactPhase || dismissing) {
        const float impactForDisplay = impactPhase ? currentImpact : confirmedImpact;
        const float diff = impactForDisplay - 0.5f;
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

        setText(m_ui.shotPanelStepEntity, L"02 / 02");
        setText(m_ui.shotPanelTitleEntity, L"インパクトタイミング");
        setText(m_ui.shotPanelHintEntity,
                dismissing ? L"" : L"左クリックでインパクト　　右クリックでキャンセル");
        setText(m_ui.shotPanelPowerLabelEntity, L"距離");
        setText(m_ui.shotPanelPowerValueEntity,
                std::to_wstring(distanceYards) + L"y 確定");
        setText(m_ui.shotPanelAccuracyLabelEntity, L"正確性");
        setText(m_ui.shotPanelAccuracyValueEntity, impactText);
        setColor(m_ui.shotPanelPowerValueEntity, game::ui::kColorTextSub);
        setColor(m_ui.shotPanelAccuracyValueEntity, impactColor);
    } else if (shotPhase == game::components::ShotState::Phase::Idle) {
        // 本当の待機状態（次のショットへ戻った時）だけリセットする。
        // ここを「アクティブでもdismissingでもない全部」に対して実行して
        // いたため、ボール飛行中(Executing)などフェード完了後もまだ
        // ショットが進行中の間にこの分岐へ落ちてきて、パワー/正確性の
        // 文字色を(フェード済みのアルファを無視して)不透明な白へ強制的に
        // 書き戻してしまい、背景パネルだけ消えた状態で文字だけが浮いて
        // 残り続けるように見える不具合になっていた。
        setText(m_ui.shotPanelStepEntity, L"");
        setText(m_ui.shotPanelTitleEntity, L"ショット準備");
        setText(m_ui.shotPanelHintEntity, L"");
        setText(m_ui.shotPanelPowerLabelEntity, L"距離");
        setText(m_ui.shotPanelPowerValueEntity, L"0y");
        setText(m_ui.shotPanelAccuracyLabelEntity, L"正確性");
        setText(m_ui.shotPanelAccuracyValueEntity, L"待機");
        setColor(m_ui.shotPanelPowerValueEntity, game::ui::kColorWhite);
        setColor(m_ui.shotPanelAccuracyValueEntity, game::ui::kColorTextSub);
    }
    // それ以外(Executing/ShowResult/RestoringCamera でフェードが既に完了
    // している場合)は何もしない。SetShotPhaseUIVisible が isShotPhase の
    // 遷移に合わせて最終的に visible=false へ倒すまで、フェード済みの
    // アルファ値をそのまま維持する。

    // 確定直後だけ数値をわずかに拡大→通常サイズへ戻すパンチ演出（グロー等は使わない）。
    auto applyPunch = [&](ecs::Entity e) {
        if (auto* t = ctx.world.Get<game::components::UIText>(e)) {
            t->style.fontSize = game::ui::kShotValueFontSize +
                                game::ui::kShotValuePunchFontDelta * pulse;
        }
    };
    applyPunch(m_ui.shotPanelPowerValueEntity);
    applyPunch(m_ui.shotPanelAccuracyValueEntity);

    std::wstring clubText = core::ToWString(currentClub.name);
    if (currentClub.baseCarryDistance > 0.0f) {
        clubText += L" (基準 " +
                    std::to_wstring(static_cast<int>(
                        std::round(currentClub.baseCarryDistance))) +
                    L"y)";
    }
    setText(m_ui.shotPanelClubLabelEntity, clubText);

    if (m_ui.gaugeBarEntity != UINT32_MAX &&
        ctx.world.Has<game::components::UIBarGauge>(m_ui.gaugeBarEntity))
    {
        auto* gauge = ctx.world.Get<game::components::UIBarGauge>(m_ui.gaugeBarEntity);
        gauge->isVisible = powerPhase || impactPhase || dismissing;
        gauge->mode = (impactPhase || dismissing) ? game::components::UIBarGaugeMode::Impact
                                                   : game::components::UIBarGaugeMode::Power;
        gauge->showImpactZones = impactPhase || dismissing;
        gauge->showMarker = powerPhase || impactPhase;
        gauge->showConfirmedMarker = dismissing;
        gauge->confirmPulse = pulse;
        gauge->opacity = dismissing ? fadeAlpha : 1.0f;

        if (dismissing) {
            const auto judgement = game::utils::EvaluateImpactJudgement(confirmedImpact);
            const auto color = JudgementColor(judgement);
            gauge->confirmedValue = confirmedImpact;
            gauge->confirmedMarkerColor = color;
            gauge->markerColor = color;
            gauge->borderColor = color;
        } else {
            gauge->confirmedValue = confirmedPower;
            gauge->confirmedMarkerColor = game::ui::kColorWarning;
            gauge->markerColor = impactPhase ? game::ui::kColorWhite
                                             : game::ui::kColorWarning;
            gauge->borderColor = impactPhase ? game::ui::kColorSuccess
                                              : game::ui::kColorBorder;
        }

        gauge->color = powerPercent >= 75 ? game::ui::kColorWarning
                     : powerPercent >= 40 ? game::ui::kColorSuccess
                                          : game::ui::kColorAccent;

        // スライドインは充填/インパクト開始時、上抜けドリフトはフェードアウト時のみ。
        const float eased = 1.0f - std::pow(1.0f - m_phaseTransition, 3.0f);
        float gaugeOffsetY = 0.0f;
        if (activeShotPhase) {
            gaugeOffsetY = (1.0f - eased) * 22.0f;
        } else if (dismissing) {
            gaugeOffsetY = -(1.0f - fadeAlpha) * game::ui::kGaugeFadeDriftY;
        }
        gauge->y = game::ui::kShotPanelY + 12.0f + gaugeOffsetY;
    }

    // alpha/offsetY は毎フレーム必ず明示的に決める（条件付きスキップに
    // しない）。以前は activeShotPhase・dismissing のどちらでもない間は
    // このブロックごとスキップしていたため、ボール飛行中(Executing)や
    // 結果表示中(ShowResult)のようにパネルはまだ visible=true のまま
    // フェードだけが自然終了した瞬間の中途半端なアルファ値で凍りつき、
    // SetShotPhaseUIVisible が最終的に隠すまでの数秒間、文字だけが
    // 消えずに浮いて見える不具合になっていた。
    float offsetY;
    float alpha;
    if (activeShotPhase) {
        const float eased = 1.0f - std::pow(1.0f - m_phaseTransition, 3.0f);
        offsetY = (1.0f - eased) * 22.0f;
        alpha = eased;
    } else if (dismissing) {
        // フェードアウト中はわずかに上へ抜けながら消える（発光は使わない）。
        offsetY = -(1.0f - fadeAlpha) * game::ui::kGaugeFadeDriftY;
        alpha = fadeAlpha;
    } else {
        offsetY = -game::ui::kGaugeFadeDriftY;
        alpha = 0.0f;
    }
    if (auto* bg = ctx.world.Get<game::components::UIText>(m_ui.shotPanelBgEntity)) {
        bg->y = game::ui::kShotPanelBgY + offsetY;
        bg->style.bgColor.w = game::ui::kColorBgDark.w * alpha;
        bg->style.borderColor.w = game::ui::kColorBorder.w * alpha;
    }
    // ShotPanelLabel/ShotPanelValue プリセットの影の基準アルファ。
    // 影は固定アルファのままだと本体だけ消えて影の残像が残ってしまうため、
    // この基準値に本体と同じ alpha を掛けて一緒に消えるようにする
    // （毎フレーム shadowColor.w 自体を書き換えるので、掛け算の起点は
    // 変動しないこの定数から取る必要がある）。
    constexpr float kShotPanelShadowBaseAlpha = 0.35f;
    auto moveText = [&](ecs::Entity e, float baseY) {
        if (auto* text = ctx.world.Get<game::components::UIText>(e)) {
            text->y = baseY + offsetY;
            text->style.color.w = alpha;
            if (text->style.hasShadow) {
                text->style.shadowColor.w = kShotPanelShadowBaseAlpha * alpha;
            }
        }
    };
    moveText(m_ui.shotPanelStepEntity, game::ui::kShotPanelY - 22.0f);
    // shotPanelStepEntity(「01/02」等のチップ)だけは自前の不透明な背景
    // (kColorSurfaceRaised)と枠線を持っているため、moveText の本体色
    // フェードだけでは消えず、周りが消えた後もチップの背景枠だけ不透明
    // なまま浮いて残ってしまう。同じ alpha で背景/枠線も一緒に消す。
    if (auto* step = ctx.world.Get<game::components::UIText>(m_ui.shotPanelStepEntity)) {
        step->style.bgColor.w = game::ui::kColorSurfaceRaised.w * alpha;
        step->style.borderColor.w = 0.75f * alpha;
    }
    moveText(m_ui.shotPanelTitleEntity, game::ui::kShotPanelY - 25.0f);
    moveText(m_ui.shotPanelHintEntity, game::ui::kShotPanelY - 19.0f);
    moveText(m_ui.shotPanelPowerLabelEntity, game::ui::kShotPanelY + 48.0f);
    moveText(m_ui.shotPanelPowerValueEntity, game::ui::kShotPanelY + 48.0f);
    moveText(m_ui.shotPanelAccuracyLabelEntity, game::ui::kShotPanelY + 76.0f);
    moveText(m_ui.shotPanelAccuracyValueEntity, game::ui::kShotPanelY + 76.0f);
    moveText(m_ui.shotPanelClubLabelEntity, game::ui::kShotPanelY + 104.0f);
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
        SetTextIfChanged(*t, core::ToWString(state.currentPage));
    }

    if (m_ui.browserTargetEntity != UINT32_MAX &&
        ctx.world.Has<game::components::UIText>(m_ui.browserTargetEntity))
    {
        auto* t = ctx.world.Get<game::components::UIText>(m_ui.browserTargetEntity);
        SetTextIfChanged(*t, core::ToWString(state.targetPage));
    }

    if (m_ui.browserShotInfoEntity != UINT32_MAX &&
        ctx.world.Has<game::components::UIText>(m_ui.browserShotInfoEntity))
    {
        auto* t = ctx.world.Get<game::components::UIText>(m_ui.browserShotInfoEntity);
        SetTextIfChanged(*t, L"SHOT " + std::to_wstring(state.shotCount) +
                                  L"   PAR " + std::to_wstring(state.par) +
                                  L"   ROUTE " + std::to_wstring(state.moveCount));
    }
}

// -------------------------------------------------------
// 数値ウェーブ演出: 1文字ずつ独立したエンティティに割り当て、右端から
// 位相をずらして上下に揺らす。「常に何かが動いている」感を主要な数値
// 表示にだけ持たせるための共通処理。
// -------------------------------------------------------
void WikiGolfHUD::UpdateWaveNumberText(core::GameContext& ctx,
                                        std::vector<ecs::Entity>& chars,
                                        const std::wstring& text,
                                        float baseX, float baseY,
                                        const graphics::TextStyle& baseStyle,
                                        int layer)
{
    constexpr size_t kMaxWaveChars = 12;
    if (chars.empty()) {
        chars.reserve(kMaxWaveChars);
        for (size_t i = 0; i < kMaxWaveChars; ++i) {
            auto e = ctx.world.CreateEntity();
            auto& t = ctx.world.Add<game::components::UIText>(e);
            t.visible = false;
            chars.push_back(e);
        }
    }

    auto glyphWidth = [&](wchar_t c) {
        if (c == L' ') return baseStyle.fontSize * 0.30f;
        if (c == L'.' || c == L',') return baseStyle.fontSize * 0.26f;
        return baseStyle.fontSize * 0.58f;
    };

    // リレー式: 全文字が同時に揺れるのではなく、1文字の上下動が終わって
    // から次の文字が始まる。activeIndexF は「今どの文字の番か」を表す
    // 連続値で、文字 i は activeIndexF が [i, i+1) の間だけ持ち上がって
    // 戻り、それ以外の時間は静止する。
    constexpr float kPerCharDuration = 0.3f; // 1文字が上がって戻るのにかかる秒数

    const size_t n = std::min(text.size(), chars.size());
    const float totalCycle = kPerCharDuration * std::max<float>(1.0f, static_cast<float>(n));
    const float activeIndexF = std::fmod(m_elapsedTime, totalCycle) / kPerCharDuration;

    float cursorX = baseX;
    for (size_t i = 0; i < n; ++i) {
        auto* t = ctx.world.Get<game::components::UIText>(chars[i]);
        if (!t) continue;
        const float w = glyphWidth(text[i]);
        t->text  = std::wstring(1, text[i]);
        t->style = baseStyle;
        t->style.align = graphics::TextAlign::Left;
        t->x = cursorX;
        t->width  = w + 4.0f;
        t->height = baseStyle.fontSize + 8.0f;

        const float local = activeIndexF - static_cast<float>(i); // この文字の番が来たら 0..1
        const float bump = (local >= 0.0f && local < 1.0f) ? std::sin(local * DirectX::XM_PI) : 0.0f;
        t->y = baseY - bump * 5.0f;
        t->visible = true;
        t->layer = layer;
        cursorX += w;
    }
    for (size_t i = n; i < chars.size(); ++i) {
        if (auto* t = ctx.world.Get<game::components::UIText>(chars[i])) {
            t->visible = false;
        }
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
    // 風速。強さに応じた色分けだけが意味のあるシグナルであり、
    // 枠線の明滅など付随演出は持たせない。
    // 数値を1文字ずつウェーブさせていたが、文字ごとの上下位置がずれて
    // 数値そのものが読み取りにくくなる（実機確認で「3」と「.1」の位置が
    // ずれて見える）と分かったため静止表示に戻した。
    if (m_ui.windCardValueEntity != UINT32_MAX &&
        ctx.world.Has<game::components::UIText>(m_ui.windCardValueEntity))
    {
        auto* t = ctx.world.Get<game::components::UIText>(m_ui.windCardValueEntity);
        wchar_t buf[32];
        swprintf(buf, 32, L"%.1f", windSpeed);
        SetTextIfChanged(*t, buf);

        if (windSpeed >= 10.0f) {
            SetColorIfChanged(t->style.color, game::ui::kColorError);
        } else if (windSpeed >= 5.0f) {
            SetColorIfChanged(t->style.color, game::ui::kColorWarning);
        } else {
            SetColorIfChanged(t->style.color, game::ui::kColorWhite);
        }
    }
    for (auto e : m_ui.windValueWaveChars) {
        if (e != UINT32_MAX && ctx.world.IsAlive(e)) {
            if (auto* wt = ctx.world.Get<game::components::UIText>(e)) wt->visible = false;
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

        std::wstring arrow = L"↑"; // ↑
        if      (angle > DirectX::XM_PI/8   && angle <=  3*DirectX::XM_PI/8) arrow = L"↗";
        else if (angle > 3*DirectX::XM_PI/8 && angle <=  5*DirectX::XM_PI/8) arrow = L"→";
        else if (angle > 5*DirectX::XM_PI/8 && angle <=  7*DirectX::XM_PI/8) arrow = L"↘";
        else if (angle > 7*DirectX::XM_PI/8 || angle <= -7*DirectX::XM_PI/8) arrow = L"↓";
        else if (angle < -5*DirectX::XM_PI/8)                                 arrow = L"↙";
        else if (angle < -3*DirectX::XM_PI/8)                                 arrow = L"←";
        else if (angle < -DirectX::XM_PI/8)                                   arrow = L"↖";

        SetTextIfChanged(*t, arrow + L" m/s");

        // 左右のスウェイではなく、通常位置→上→通常位置を繰り返す片方向の
        // バウンス。sin(phase*PI) は 0→1→0 としか動かないため、基準位置より
        // 下には行かず「上がって戻る」動きだけになる。
        constexpr float kCycle = 0.55f; // 秒（速めのテンポ）
        const float phase = std::fmod(m_elapsedTime, kCycle) / kCycle; // 0..1
        const float rise = std::sin(phase * DirectX::XM_PI);           // 0->1->0
        const float amp = 5.0f + std::min(windSpeed, 12.0f) * 1.0f;    // 風速が強いほど大きく跳ねる
        t->x = game::ui::kWindCardX + 76.0f;
        t->y = game::ui::kWindCardY + 38.0f - rise * amp;
    }
}

// -------------------------------------------------------
// クラブ選択パネル横: 着弾点プレビュー(トップビュー)トグルボタンの見た目更新
// -------------------------------------------------------
void WikiGolfHUD::UpdateLandingPreviewButton(core::GameContext& ctx, bool hovered,
                                             bool active, bool enabled) {
    auto* bg  = ctx.world.Get<game::components::UIText>(m_ui.landingPreviewBtnBgEntity);
    auto* txt = ctx.world.Get<game::components::UIText>(m_ui.landingPreviewBtnTextEntity);
    if (!bg || !txt) return;

    if (active) {
        // トップビュー表示中はアクセント色で強調する。
        bg->style.bgColor     = game::ui::kColorAccent;
        bg->style.borderColor = game::ui::kColorAccent;
        txt->style.color      = game::ui::kColorWhite;
    } else if (hovered && enabled) {
        bg->style.bgColor     = game::ui::kColorBgDark;
        bg->style.bgColor.w   = 0.95f;
        bg->style.borderColor = game::ui::kColorBorder;
        txt->style.color      = game::ui::kColorWhite;
    } else {
        bg->style.bgColor     = game::ui::kColorBgDark;
        bg->style.borderColor = game::ui::kColorBorder;
        txt->style.color      = enabled ? game::ui::kColorWhite : game::ui::kColorTextSub;
    }
}

// -------------------------------------------------------
// クラブ選択リスト更新 (動的生成込み)
// 入力: clubs (全クラブデータリスト), currentClubIndex
// 選択状態は「面色」と「枠線色」の2値だけで表現し、勾配・影の増減・
// 明滅・矢印マークといった重複するシグナルは持たせない。
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
        m_clubRowWindowVisible.assign(n, false);

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
                ApplyRowStyle(t.style);
                t.visible = true;
                t.layer   = game::ui::kLayerClubSelect;
                m_ui.clubBgEntities.push_back(e);
            }

            // アイコン
            {
                auto e = ctx.world.CreateEntity();
                auto& img = ctx.world.Add<game::components::UIImage>(e);
                img = game::components::UIImage::Create(clubs[i].iconTexture, x + 8.0f, y + 6.0f);
                img.width = h - 12.0f;
                img.height = h - 12.0f;
                img.visible = true;
                img.layer = game::ui::kLayerClubSelect + 1;
                m_ui.clubIconEntities.push_back(e);
            }

            // クラブ名 (日本語)
            {
                auto e = ctx.world.CreateEntity();
                auto& t = ctx.world.Add<game::components::UIText>(e);
                t.text  = core::ToWString(clubs[i].name);
                t.x     = x + h + 10.0f;
                t.y     = y + 7.0f;
                t.width = w - h - 30.0f;
                t.height = 18.0f;
                t.style  = graphics::TextStyle::BrowserSub();
                t.style.fontFamily = "Kiwi Maru Medium"; // 日本語のクラブ名なので丸ゴシックで表示
                t.style.fontSize = game::ui::kClubNameFontSize;
                t.style.color    = game::ui::kColorWhite;
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
                t.x     = x + h + 10.0f;
                t.y     = y + 29.0f;
                t.width = w - h - 30.0f;
                t.height = 16.0f;
                t.style  = graphics::TextStyle::BrowserSub();
                t.style.fontSize = 11.0f;
                t.style.align    = graphics::TextAlign::Left;
                t.visible = true;
                t.layer   = game::ui::kLayerClubSelect + 1;
                m_ui.clubSubNameEntities.push_back(e);
            }

            // 選択状態の内部フラグ保持用（非表示・空テキストのまま）。
            // 見た目は背景行の面色/枠線色だけで表現するため、この
            // エンティティ自体は何も描画しない。
            {
                auto e = ctx.world.CreateEntity();
                auto& t = ctx.world.Add<game::components::UIText>(e);
                t.text  = L"";
                t.x     = x + w - 24.0f;
                t.y     = y + h * 0.25f;
                t.width = 20.0f;
                t.height = 20.0f;
                t.visible = false;
                t.layer   = game::ui::kLayerClubSelect + 2;
                m_ui.clubArrowEntities.push_back(e);
            }

            y += h + sp;
        }

        // 「切替できるよ」ヒント: 前後スロットの右端に出る三角。どのクラブが
        // 前/次スロットに来ても位置は変わらない(スロット位置は固定)ため、
        // 生成は1回だけでよく、毎フレームはバウンス量だけ更新する。
        {
            auto makeHint = [&](const wchar_t* glyph, float rowY) {
                auto e = ctx.world.CreateEntity();
                auto& t = ctx.world.Add<game::components::UIText>(e);
                t.text  = glyph;
                t.x     = x + w - 24.0f;
                t.y     = rowY + h * 0.25f;
                t.width = 20.0f;
                t.height = 20.0f;
                t.style  = graphics::TextStyle::Guide();
                t.style.fontSize = game::ui::kClubNameFontSize;
                t.style.color    = game::ui::kColorAccent;
                t.style.align    = graphics::TextAlign::Center;
                t.style.bgColor  = {0.0f, 0.0f, 0.0f, 0.0f};
                t.style.borderWidth = 0.0f;
                t.visible = true;
                t.layer   = game::ui::kLayerClubSelect + 2;
                return e;
            };
            const float slot0Y = game::ui::kClubPanelY;
            const float slot2Y = game::ui::kClubPanelY +
                                  2.0f * (game::ui::kClubItemH + game::ui::kClubItemSpacing);
            m_ui.clubScrollUpEntity   = makeHint(L"▲", slot0Y); // ▲ 前へ切替可能
            m_ui.clubScrollDownEntity = makeHint(L"▼", slot2Y); // ▼ 次へ切替可能
        }

        m_builtClubCount = n;
    }

    // 切替ヒントの三角を上下にバウンスさせ、明滅させる。「動かせるよ」を
    // 一目で伝えるための唯一のアイドルアニメーション（他パネルは持たせない）。
    if (n > 1) {
        const float bob   = std::sin(m_elapsedTime * 1.3f) * 3.0f;
        const float pulse = 0.55f + 0.45f * (0.5f + 0.5f * std::sin(m_elapsedTime * 1.3f));
        const float slot0Y = game::ui::kClubPanelY;
        const float slot2Y = game::ui::kClubPanelY +
                              2.0f * (game::ui::kClubItemH + game::ui::kClubItemSpacing);
        const float h = game::ui::kClubItemH;
        if (auto* up = ctx.world.Get<game::components::UIText>(m_ui.clubScrollUpEntity)) {
            up->y = slot0Y + h * 0.25f - bob;
            up->style.color.w = pulse;
        }
        if (auto* down = ctx.world.Get<game::components::UIText>(m_ui.clubScrollDownEntity)) {
            down->y = slot2Y + h * 0.25f + bob;
            down->style.color.w = pulse;
        }
    }

    const int previousIndex = n > 0 ? (currentClubIndex - 1 + n) % n : -1;
    const int nextIndex = n > 0 ? (currentClubIndex + 1) % n : -1;

    // 通常時は前・選択中・次の3本だけを表示する。全クラブの常設メニュー化を避ける。
    for (int i = 0; i < n; ++i) {
        bool isActive = (i == currentClubIndex);
        int slot = -1;
        if (i == previousIndex) slot = 0;
        if (isActive) slot = 1;
        if (i == nextIndex) slot = 2;
        const bool isVisibleRow = slot >= 0;
        if (i < static_cast<int>(m_clubRowWindowVisible.size())) {
            m_clubRowWindowVisible[i] = isVisibleRow;
        }
        // 窓の外の行は位置を動かさない。slot=-1 のとき先頭スロットへ丸めて
        // しまうと、非表示のはずの行が先頭パネルと同じ座標に積み上がり、
        // 何かの拍子に visible が立った瞬間に画像が重なって見える不具合の
        // 原因になるため、表示対象の行だけ座標を更新する。
        const float rowY = game::ui::kClubPanelY +
                           static_cast<float>(slot) *
                               (game::ui::kClubItemH + game::ui::kClubItemSpacing);

        // 選択中の行だけ、ごくゆっくり上下にたゆたわせる。「これが選ばれて
        // いる」を常時アイドルで体感させるための唯一の位置揺れで、他の行
        // には掛けない。横揺れではなく縦方向・低速のゆったりした動き。
        float jitterY = 0.0f;
        if (isActive) {
            jitterY = std::sin(m_elapsedTime * 0.9f) * 2.2f;
        }
        const float baseX = game::ui::kClubPanelX;
        const float iconBaseX = baseX + 8.0f;
        const float nameBaseX = baseX + game::ui::kClubItemH + 10.0f;

        // 背景の見た目: 面色と枠線色の2値だけで選択状態を表す
        if (i < (int)m_ui.clubBgEntities.size()) {
            if (auto* bg = ctx.world.Get<game::components::UIText>(m_ui.clubBgEntities[i])) {
                bg->visible = isVisibleRow;
                if (isVisibleRow) { bg->y = rowY + jitterY; bg->x = baseX; }
                if (isActive) {
                    ApplyActiveRowStyle(bg->style);
                } else {
                    ApplyRowStyle(bg->style);
                    bg->style.bgColor.w = 0.55f;
                }
            }
        }

        // アイコンのアルファ
        if (i < (int)m_ui.clubIconEntities.size()) {
            if (auto* icon = ctx.world.Get<game::components::UIImage>(m_ui.clubIconEntities[i])) {
                icon->visible = isVisibleRow;
                if (isVisibleRow) { icon->y = rowY + 6.0f + jitterY; icon->x = iconBaseX; }
                icon->alpha = isActive ? 1.0f : 0.5f;
            }
        }

        // テキストのアルファ
        if (i < (int)m_ui.clubNameEntities.size()) {
            if (auto* name = ctx.world.Get<game::components::UIText>(m_ui.clubNameEntities[i])) {
                name->visible = isVisibleRow;
                if (isVisibleRow) { name->y = rowY + 7.0f + jitterY; name->x = nameBaseX; }
                name->style.color.w = isActive ? 1.0f : 0.5f;
            }
        }
        if (i < (int)m_ui.clubSubNameEntities.size()) {
            if (auto* sub = ctx.world.Get<game::components::UIText>(m_ui.clubSubNameEntities[i])) {
                sub->visible = isVisibleRow;
                if (isVisibleRow) { sub->y = rowY + 29.0f + jitterY; sub->x = nameBaseX; }
                sub->style.color = game::ui::kColorTextSub;
                sub->style.color.w = isActive ? 1.0f : 0.5f;
            }
        }

        // 選択フラグ（非表示エンティティのブックキーピング用途のみ）
        if (i < (int)m_ui.clubArrowEntities.size()) {
            if (auto* arrow = ctx.world.Get<game::components::UIText>(m_ui.clubArrowEntities[i])) {
                arrow->visible = isActive && isVisibleRow;
            }
        }
    }
}

// -------------------------------------------------------
// 下部情報パネル (ライ)
// 残り距離は目的地(ゴール記事)が多数あって単一の距離表示に意味が薄く、
// 選択中クラブは左のクラブ選択リストと重複するため、このパネルからは
// 削除した。地形(ライ)情報だけが実プレー中に必要な独自情報として残る。
// -------------------------------------------------------
void WikiGolfHUD::UpdateBottomInfoPanels(core::GameContext& ctx,
                                         game::components::TerrainMaterial lie)
{
    const float panelW = 240.0f;
    const float panelH = 84.0f;
    const float px = (1280.0f - panelW) * 0.5f;
    // 元々 612 だったが、左下の操作ヒント(kControlHintY=676〜700)と縦に
    // 重なり、ヒントの文字がライパネルの下段テキストを隠していたため
    // ヒントの上に確実に収まる位置まで引き上げる。
    const float py = 580.0f;
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

    // ライパネルの生成（単独の背景パネルとして中央に表示）
    if (m_ui.liePanelBgEntity == UINT32_MAX) {
        m_ui.liePanelBgEntity = ctx.world.CreateEntity();
        auto& bg = ctx.world.Add<game::components::UIText>(m_ui.liePanelBgEntity);
        bg.x = px; bg.y = py; bg.width = panelW; bg.height = panelH;
        ApplySurfaceStyle(bg.style);
        bg.layer = layer;
        bg.visible = true;

        createText(m_ui.lieLabelEntity, px + 18.0f, py + 12.0f, panelW - 36.0f, 18.0f, 11.0f, game::ui::kColorTextSub, graphics::TextAlign::Left);
        if (auto* t = ctx.world.Get<game::components::UIText>(m_ui.lieLabelEntity)) t->text = L"BALL LIE";
        createText(m_ui.lieValueEntity, px + 18.0f, py + 32.0f, panelW - 36.0f, 25.0f, 20.0f, game::ui::kColorSuccess, graphics::TextAlign::Left);
        createText(m_ui.lieCondValueEntity, px + 18.0f, py + 60.0f, panelW - 36.0f, 16.0f, 11.0f, game::ui::kColorTextSub, graphics::TextAlign::Left);
        // 「フェアウェイ」「抵抗 やや大」等の日本語なので丸ゴシックにする
        if (auto* t = ctx.world.Get<game::components::UIText>(m_ui.lieValueEntity)) t->style.fontFamily = "Kiwi Maru Medium";
        if (auto* t = ctx.world.Get<game::components::UIText>(m_ui.lieCondValueEntity)) t->style.fontFamily = "Kiwi Maru Medium";
    }

    // ライ
    std::wstring lieStr = L"フェアウェイ";
    std::wstring condStr = L"コンディション良好";
    DirectX::XMFLOAT4 lieColor = game::ui::kColorSuccess;

    switch (lie) {
        case game::components::TerrainMaterial::Rough:
            lieStr = L"ラフ"; condStr = L"抵抗 やや大"; lieColor = game::ui::kColorWarning; break;
        case game::components::TerrainMaterial::Bunker:
            lieStr = L"バンカー"; condStr = L"抵抗 大"; lieColor = game::ui::kColorWarning; break;
        case game::components::TerrainMaterial::Green:
            lieStr = L"グリーン"; condStr = L"高速な転がり"; lieColor = game::ui::kColorSuccess; break;
        case game::components::TerrainMaterial::Ice:
            lieStr = L"アイス"; condStr = L"非常に滑る"; lieColor = game::ui::kColorAccent; break;
        case game::components::TerrainMaterial::Water:
            lieStr = L"ウォーター"; condStr = L"OUT OF BOUNDS"; lieColor = game::ui::kColorError; break;
        case game::components::TerrainMaterial::Lava:
            lieStr = L"溶岩"; condStr = L"OUT OF BOUNDS"; lieColor = game::ui::kColorError; break;
        case game::components::TerrainMaterial::Stone:
            lieStr = L"ストーン"; condStr = L"強くバウンド"; lieColor = game::ui::kColorTextSub; break;
        default: break;
    }

    if (auto* t = ctx.world.Get<game::components::UIText>(m_ui.lieValueEntity)) {
        SetTextIfChanged(*t, lieStr);
        SetColorIfChanged(t->style.color, lieColor);
    }
    if (auto* t = ctx.world.Get<game::components::UIText>(m_ui.lieCondValueEntity)) {
        SetTextIfChanged(*t, condStr);
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
        gauge->confirmPulse = 0.0f;
        gauge->opacity = 1.0f;
    }
    m_gaugeDismissRemaining = 0.0f;

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
        SetTextIfChanged(*t, text);
        SetColorIfChanged(t->style.color, color);
    }
    if (m_ui.shotPanelAccuracyValueEntity != UINT32_MAX &&
        ctx.world.Has<game::components::UIText>(m_ui.shotPanelAccuracyValueEntity))
    {
        auto* t = ctx.world.Get<game::components::UIText>(m_ui.shotPanelAccuracyValueEntity);
        SetTextIfChanged(*t, text);
        SetColorIfChanged(t->style.color, color);
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
    setVis(m_ui.clubHeaderEntity,     !shotPhase);

    const bool bottomInfoVisible = !shotPhase;
    setVis(m_ui.liePanelBgEntity, bottomInfoVisible);
    setVis(m_ui.lieLabelEntity, bottomInfoVisible);
    setVis(m_ui.lieValueEntity, bottomInfoVisible);
    setVis(m_ui.lieCondValueEntity, bottomInfoVisible);

    // クラブリスト: ショット時は選択中だけ残して視界を整理。
    // 非ショット時に戻す行の可視性は m_clubRowWindowVisible
    // (UpdateClubSelectList が前後3行の窓として毎フレーム更新している
    // キャッシュ)からその場で決定的に復元する。以前はここを
    // 「shotPhase==true の時だけ触る」片方向の処理にしていたため、
    // ショット終了の瞬間は SetShotPhaseUIVisible(false) がこのフレームで
    // 呼ばれても何もせず、UpdateClubSelectList が次に間引き更新で走る
    // (最大0.1秒後)までショット中に隠した行が復元されない隙間があった。
    float clubAlpha = shotPhase ? 0.35f : 1.0f;
    for (size_t i = 0; i < m_ui.clubBgEntities.size(); ++i) {
        const bool selected = i < m_ui.clubArrowEntities.size() &&
                              ctx.world.Get<game::components::UIText>(m_ui.clubArrowEntities[i]) &&
                              ctx.world.Get<game::components::UIText>(m_ui.clubArrowEntities[i])->visible;
        const bool rowVisible = shotPhase
            ? selected
            : (i < m_clubRowWindowVisible.size() ? m_clubRowWindowVisible[i] : selected);

        setVis(m_ui.clubBgEntities[i], rowVisible);
        if (i < m_ui.clubNameEntities.size())    setVis(m_ui.clubNameEntities[i], rowVisible);
        if (i < m_ui.clubSubNameEntities.size()) setVis(m_ui.clubSubNameEntities[i], rowVisible);
        if (i < m_ui.clubIconEntities.size() && m_ui.clubIconEntities[i] != UINT32_MAX &&
            ctx.world.IsAlive(m_ui.clubIconEntities[i])) {
            if (auto* img = ctx.world.Get<game::components::UIImage>(m_ui.clubIconEntities[i])) {
                img->visible = rowVisible;
            }
        }

        if (i < m_ui.clubNameEntities.size())    setAlpha(m_ui.clubNameEntities[i], clubAlpha);
        if (i < m_ui.clubSubNameEntities.size()) setAlpha(m_ui.clubSubNameEntities[i], clubAlpha * 0.9f);
        if (i < m_ui.clubIconEntities.size() && m_ui.clubIconEntities[i] != UINT32_MAX &&
            ctx.world.IsAlive(m_ui.clubIconEntities[i])) {
            if (auto* img = ctx.world.Get<game::components::UIImage>(m_ui.clubIconEntities[i])) {
                img->alpha = shotPhase ? 0.35f : (img->alpha > 0.5f ? 1.0f : 0.5f);
            }
        }

        if (i < m_ui.clubBgEntities.size() && m_ui.clubBgEntities[i] != UINT32_MAX &&
            ctx.world.IsAlive(m_ui.clubBgEntities[i])) {
            if (auto* t = ctx.world.Get<game::components::UIText>(m_ui.clubBgEntities[i])) {
                t->style.bgColor.w = shotPhase ? 0.35f
                                                : (selected ? game::ui::kColorSurfaceRaised.w
                                                            : 0.55f);
            }
        }
    }
    setVis(m_ui.clubScrollUpEntity,   !shotPhase);
    setVis(m_ui.clubScrollDownEntity, !shotPhase);

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
    // ゲージ自体の可視性は UpdateShotPhasePanel（保持タイマー込み）が一元管理する。
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
    setVis(m_ui.browserCurrentLabelEntity, visible);
    setVis(m_ui.browserCurrentPageEntity, visible);
    setVis(m_ui.browserTargetLabelEntity, visible);
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
    setVis(m_ui.clubScrollUpEntity,   visible);
    setVis(m_ui.clubScrollDownEntity, visible);

    // ショットボタン
    setVis(m_ui.shotButtonBgEntity,   visible);
    setVis(m_ui.shotButtonTextEntity, visible);

    // 操作ヘルプ
    setVis(m_ui.controlHintEntity, visible);

    // ライパネル
    setVis(m_ui.liePanelBgEntity,     visible);
    setVis(m_ui.lieLabelEntity,        visible);
    setVis(m_ui.lieValueEntity,        visible);
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
