#pragma once
/**
 * @file UIConstants.h
 * @brief UI詳細設定の定数定義
 * 位置、サイズ、色、フォント、アニメーション、ゲームプレイパラメータを一括管理
 */

#include <DirectXMath.h>

namespace game::ui {

// =========================================================
// 共通カラーパレット
// =========================================================
const DirectX::XMFLOAT4 kColorWhite            = {1.000f, 1.000f, 1.000f, 1.000f};
const DirectX::XMFLOAT4 kColorBgDark          = {0.035f, 0.055f, 0.090f, 0.900f};
const DirectX::XMFLOAT4 kColorBgPanel         = {0.045f, 0.070f, 0.110f, 0.840f};
const DirectX::XMFLOAT4 kColorBorder          = {0.300f, 0.520f, 0.780f, 0.450f};
const DirectX::XMFLOAT4 kColorAccent          = {0.180f, 0.600f, 1.000f, 1.000f}; // スカイブルー
const DirectX::XMFLOAT4 kColorTextSub         = {0.650f, 0.700f, 0.780f, 0.920f}; // グレー
const DirectX::XMFLOAT4 kColorSuccess         = {0.120f, 0.800f, 0.420f, 1.000f}; // グリーン
const DirectX::XMFLOAT4 kColorWarning         = {1.000f, 0.760f, 0.160f, 1.000f}; // イエロー
const DirectX::XMFLOAT4 kColorError           = {0.900f, 0.180f, 0.180f, 1.000f}; // レッド
const DirectX::XMFLOAT4 kColorSpecial         = {1.000f, 0.860f, 0.240f, 1.000f}; // 金色
const DirectX::XMFLOAT4 kColorClubSelected    = {0.100f, 0.380f, 0.750f, 0.900f}; // 選択中クラブ背景（青）
const DirectX::XMFLOAT4 kColorClubNormal      = {0.030f, 0.050f, 0.080f, 0.750f}; // 通常クラブ背景
const DirectX::XMFLOAT4 kColorShotBtn         = {0.050f, 0.250f, 0.700f, 0.950f}; // ショットボタン
const DirectX::XMFLOAT4 kColorShotBtnBorder   = {0.200f, 0.650f, 1.000f, 1.000f}; // ショットボタン枠

// =========================================================
// レイヤー設定（Z順）
// =========================================================
constexpr int kLayerBackground  = 0;
constexpr int kLayerBrowser     = 10;
constexpr int kLayerClubSelect  = 20;  // クラブ選択リスト
constexpr int kLayerShotPanel   = 90;
constexpr int kLayerMinimap     = 100;
constexpr int kLayerWind        = 110;
constexpr int kLayerMarker      = 101;
constexpr int kLayerShotButton  = 120; // ショットボタン
constexpr int kLayerControlHint = 125; // 操作ヘルプ
constexpr int kLayerJudge       = 130;
constexpr int kLayerOverlay     = 200;

// =========================================================
// ブラウザHUD (左上)
// =========================================================
constexpr float kBrowserHudX        = 16.0f;
constexpr float kBrowserHudY        = 16.0f;
constexpr float kBrowserIconWidth   = 42.0f;
constexpr float kBrowserUrlWidth    = 430.0f;
constexpr float kBrowserFontSize    = 19.0f;
constexpr float kBrowserGoalFontSize = 22.0f;
constexpr float kBrowserSubFontSize  = 14.0f;
constexpr float kBrowserLineSpacing  = 30.0f;

// =========================================================
// 風情報カード (右上)
// =========================================================
constexpr float kWindCardX          = 530.0f;
constexpr float kWindCardY          = 20.0f;
constexpr float kWindCardWidth      = 220.0f;
constexpr float kWindValueFontSize  = 24.0f;
constexpr float kWindLabelFontSize  = 15.0f;

// =========================================================
// ショットパネル & パワーゲージ (中央下)
// =========================================================
constexpr float kShotPanelX         = 190.0f;
constexpr float kShotPanelY         = 540.0f;
constexpr float kShotPanelWidth     = 780.0f;
constexpr float kShotLabelFontSize  = 15.0f;
constexpr float kShotValueFontSize  = 18.0f;

constexpr float kGaugeHeight        = 18.0f;
constexpr float kGaugeBorderWidth   = 1.5f;
constexpr float kImpactWidthGreat   = 0.04f; // 全体に対する割合
constexpr float kImpactWidthNice    = 0.12f;

// =========================================================
// クラブ選択パネル (左側) - 1280x720固定
// =========================================================
constexpr float kClubPanelX         = 16.0f;
constexpr float kClubPanelY         = 180.0f;
constexpr float kClubItemW          = 190.0f;
constexpr float kClubItemH          = 46.0f;
constexpr float kClubItemSpacing    = 2.0f;  // アイテム間のギャップ
constexpr float kClubNameFontSize   = 18.0f;
constexpr float kClubIconSize       = 28.0f;

// =========================================================
// ショットボタン (右下) - 1280x720固定
// =========================================================
constexpr float kShotBtnX           = 1090.0f;
constexpr float kShotBtnY           = 530.0f;
constexpr float kShotBtnW           = 170.0f;
constexpr float kShotBtnH           = 170.0f;
constexpr float kShotBtnFontSize    = 22.0f;

// =========================================================
// 操作ヘルプ (左下) - 1280x720固定
// =========================================================
constexpr float kControlHintX       = 16.0f;
constexpr float kControlHintY       = 675.0f;
constexpr float kControlHintW       = 600.0f;
constexpr float kControlHintH       = 28.0f;
constexpr float kControlHintFont    = 14.0f;

// =========================================================
// ミニマップ (右上) - 1280x720固定
// =========================================================
constexpr float kMinimapX           = 1048.0f;
constexpr float kMinimapY           = 20.0f;
constexpr float kMinimapWidth       = 170.0f;
constexpr float kMinimapHeight      = 170.0f;  // SRVは正方形(720x720)なので同一に
constexpr float kMinimapMarkerSize  = 18.0f;

// =========================================================
// マップビュー (全画面拡大)
// =========================================================
constexpr float kMapMinZoom         = 0.10f;
constexpr float kMapMaxZoom         = 6.0f;
constexpr float kMapPanSpeedFactor  = 0.0020f;
constexpr float kMapHelpPanelAlpha  = 0.88f;
constexpr float kMarkerPulseSpeed   = 3.2f;
constexpr float kMarkerPulseScale   = 0.16f;
constexpr float kMapOpenHintDuration = 3.0f; ///< マップ開始時の操作ヒント表示秒数
constexpr float kMapHelpPanelW       = 480.0f; ///< 操作ヘルプパネルの横幅
constexpr float kMapHelpPanelH       = 220.0f; ///< 操作ヘルプパネルの縦幅


// =========================================================
// 判定テキスト & ロジック
// =========================================================
constexpr float kJudgeTextX         = 540.0f;
constexpr float kJudgeTextY         = 250.0f;
constexpr float kJudgeFontSize      = 30.0f;
constexpr float kJudgeDisplayTime   = 1.1f;

// インパクト精度しきい値 (0.5が中心)
constexpr float kThresholdSpecial   = 0.015f;
constexpr float kThresholdGreat     = 0.040f;
constexpr float kThresholdNice      = 0.120f;

// =========================================================
// アニメーション速度
// =========================================================
constexpr float kFadeSpeed              = 7.0f;
constexpr float kLerpSpeedCamera        = 8.0f;
constexpr float kLerpSpeedMinimap       = 10.0f;
constexpr float kClubSelectLerpSpeed    = 8.0f;  // クラブ切替時の補間速度

} // namespace game::ui
