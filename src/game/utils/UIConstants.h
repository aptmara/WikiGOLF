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
// すべてのパネルはここで定義した色だけを共有する。
// 個別パネルで独自のグラデーション/枠線色を作らないことで、
// 画面全体の統一感を保つ。
// =========================================================
const DirectX::XMFLOAT4 kColorWhite            = {1.000f, 1.000f, 1.000f, 1.000f};
const DirectX::XMFLOAT4 kColorBgDark          = {0.055f, 0.063f, 0.075f, 0.940f}; // 標準パネル面（単色・グラデーションなし）
const DirectX::XMFLOAT4 kColorBgPanel         = {0.055f, 0.063f, 0.075f, 0.940f}; // kColorBgDark と同一。互換のため名称のみ維持
const DirectX::XMFLOAT4 kColorSurfaceRaised   = {0.098f, 0.112f, 0.132f, 0.970f}; // 選択中/強調状態の面
const DirectX::XMFLOAT4 kColorBorder          = {1.000f, 1.000f, 1.000f, 0.080f}; // 全パネル共通の極細ハイライン
const DirectX::XMFLOAT4 kColorAccent          = {0.250f, 0.780f, 0.820f, 1.000f}; // 操作・選択・照準（インタラクティブ要素で唯一使うアクセント）
const DirectX::XMFLOAT4 kColorTextSub         = {0.620f, 0.660f, 0.700f, 1.000f}; // 副情報（全パネル共通の1トーン）
const DirectX::XMFLOAT4 kColorSuccess         = {0.320f, 0.820f, 0.480f, 1.000f}; // 良好なライ・成功（意味用途限定）
const DirectX::XMFLOAT4 kColorWarning         = {1.000f, 0.700f, 0.220f, 1.000f}; // 警告・注意（意味用途限定）
const DirectX::XMFLOAT4 kColorError           = {0.940f, 0.280f, 0.280f, 1.000f}; // 危険・OB（意味用途限定）
const DirectX::XMFLOAT4 kColorSpecial         = {0.980f, 0.780f, 0.260f, 1.000f}; // 目的地・最高評価専用（他の用途に流用しない）
const DirectX::XMFLOAT4 kColorClubSelected    = {0.098f, 0.112f, 0.132f, 0.970f}; // kColorSurfaceRaised と同値
const DirectX::XMFLOAT4 kColorClubNormal      = {0.055f, 0.063f, 0.075f, 0.940f}; // kColorBgDark と同値
const DirectX::XMFLOAT4 kColorShotBtn         = {0.070f, 0.340f, 0.380f, 0.970f};
const DirectX::XMFLOAT4 kColorShotBtnBorder   = {0.250f, 0.780f, 0.820f, 0.950f}; // kColorAccent と同値
const DirectX::XMFLOAT4 kColorGaugeTick       = {1.000f, 1.000f, 1.000f, 0.300f}; // パワーゲージの閾値目盛り線専用

// =========================================================
// 形状トークン
// 角丸半径・枠線幅・影は種類ごとに1値のみを使う。
// パネルの用途が変わっても見た目の"文法"は変えない。
// =========================================================
constexpr float kRadiusPanel       = 12.0f; // 主要パネル（背景カード全般）
constexpr float kRadiusChip        = 8.0f;  // バッジ・行・ボタン
constexpr float kRadiusBar         = 6.0f;  // ゲージ・バー
constexpr float kBorderWidthThin   = 1.0f;  // 全パネル共通の枠線幅
const DirectX::XMFLOAT4 kShadowColor = {0.000f, 0.000f, 0.000f, 0.450f}; // 全パネル共通の影色
constexpr float kShadowOffsetY     = 3.0f;  // 全パネル共通の影オフセット

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
constexpr float kBrowserHudX         = 20.0f;
constexpr float kBrowserHudY         = 20.0f;
constexpr float kBrowserIconWidth    = 48.0f;
constexpr float kBrowserUrlWidth     = 280.0f;
constexpr float kBrowserFontSize     = 18.0f;
constexpr float kBrowserGoalFontSize = 19.0f;
constexpr float kBrowserSubFontSize  = 12.0f;
constexpr float kBrowserLineSpacing  = 28.0f;

// =========================================================
// 風情報カード (右上)
// =========================================================
constexpr float kWindCardX          = 884.0f;
constexpr float kWindCardY          = 20.0f;
constexpr float kWindCardWidth      = 146.0f;
constexpr float kWindValueFontSize  = 27.0f;
constexpr float kWindLabelFontSize  = 11.0f;

// =========================================================
// ショットパネル & パワーゲージ (中央下)
// =========================================================
constexpr float kShotPanelX         = 238.0f;
constexpr float kShotPanelY         = 542.0f;
constexpr float kShotPanelWidth     = 804.0f;
constexpr float kShotPanelBgX       = 214.0f;
constexpr float kShotPanelBgY       = 498.0f;
constexpr float kShotPanelBgWidth   = 852.0f;
constexpr float kShotPanelBgHeight  = 178.0f;
constexpr float kShotStepWidth      = 68.0f;
constexpr float kShotLabelFontSize  = 15.0f;
constexpr float kShotValueFontSize  = 18.0f;
constexpr float kShotTitleFontSize  = 22.0f;
constexpr float kShotHintFontSize   = 14.0f;

constexpr float kGaugeHeight        = 30.0f;
constexpr float kGaugeBorderWidth   = 1.0f;
constexpr float kImpactWidthGreat   = 0.04f; // 全体に対する割合
constexpr float kImpactWidthNice    = 0.12f;

// ゲージのゾーン透明度（インパクトゲージ）
constexpr float kGaugeZoneAlphaNice    = 0.22f;
constexpr float kGaugeZoneAlphaGreat   = 0.40f;
constexpr float kGaugeZoneAlphaSpecial = 0.85f;

// ゲージのリアクション演出
constexpr float kGaugeConfirmPulseDuration = 0.18f; // 確定パルスの基準時間（秒）
constexpr float kGaugeHoldDuration         = 0.45f; // インパクト確定後、判定色つきで保持表示する時間（秒）
constexpr float kGaugeFadeDuration         = 0.35f; // 保持表示の後、フェードアウトする時間（秒）
constexpr float kGaugeFadeDriftY           = 8.0f;  // フェードアウト中にわずかに上へ抜ける距離（px）
constexpr float kGaugeMarkerPulseScale     = 0.35f; // 確定パルス時のマーカー拡大率
constexpr float kShotValuePunchFontDelta   = 6.0f;  // パーセント数値のパンチ演出量（pt）

// =========================================================
// クラブ選択パネル (左側) - 1280x720固定
// =========================================================
constexpr float kClubPanelX         = 20.0f;
constexpr float kClubPanelY         = 468.0f;
constexpr float kClubItemW          = 236.0f;
constexpr float kClubItemH          = 56.0f;
constexpr float kClubItemSpacing    = 8.0f;
constexpr float kClubNameFontSize   = 17.0f;
constexpr float kClubIconSize       = 44.0f;

// =========================================================
// ショットボタン (右下) - 1280x720固定
// =========================================================
constexpr float kShotBtnX           = 1030.0f;
constexpr float kShotBtnY           = 622.0f;
constexpr float kShotBtnW           = 230.0f;
constexpr float kShotBtnH           = 74.0f;
constexpr float kShotBtnFontSize    = 20.0f;

// =========================================================
// 操作ヘルプ (左下) - 1280x720固定
// =========================================================
constexpr float kControlHintX       = 20.0f;
constexpr float kControlHintY       = 676.0f;
constexpr float kControlHintW       = 660.0f;
constexpr float kControlHintH       = 24.0f;
constexpr float kControlHintFont    = 12.0f;

// =========================================================
// ミニマップ (右上) - 1280x720固定
// =========================================================
constexpr float kMinimapX           = 1050.0f;
constexpr float kMinimapY           = 20.0f;
constexpr float kMinimapWidth       = 180.0f;
constexpr float kMinimapHeight      = 180.0f;  // SRVは正方形(720x720)なので同一に
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
