#pragma once
/**
 * @file UIConstants.h
 * @brief UI詳細設定の定数定義
 * 位置、サイズ、色、フォント、アニメーション、ゲームプレイパラメータを一括管理
 */

#include <DirectXMath.h>

namespace game::ui {

// =========================================================
// 共通カラーパレット（Wikipedia/MediaWikiのVectorスキンに準拠）
// すべてのパネルはここで定義した色だけを共有する。
// 個別パネルで独自のグラデーション/枠線色を作らないことで、
// 画面全体の統一感を保つ。
// =========================================================
const DirectX::XMFLOAT4 kColorWhite            = {1.000f, 1.000f, 1.000f, 1.000f};
const DirectX::XMFLOAT4 kColorTextPrimary     = {0.125f, 0.129f, 0.133f, 1.000f}; // #202122 本文色（白紙パネル上の主要文字用）
const DirectX::XMFLOAT4 kColorBgDark          = {0.973f, 0.976f, 0.980f, 0.950f}; // #f8f9fa 標準パネル面（単色・グラデーションなし、紙面調）
const DirectX::XMFLOAT4 kColorBgPanel         = {0.973f, 0.976f, 0.980f, 0.950f}; // kColorBgDark と同一。互換のため名称のみ維持
const DirectX::XMFLOAT4 kColorSurfaceRaised   = {0.918f, 0.949f, 1.000f, 0.980f}; // 選択中/強調状態の面（淡い水色ハイライト）
const DirectX::XMFLOAT4 kColorBorder          = {0.635f, 0.663f, 0.694f, 0.900f}; // #a2a9b1 全パネル共通の極細ハイライン
const DirectX::XMFLOAT4 kColorAccent          = {0.200f, 0.400f, 0.800f, 1.000f}; // #3366cc Wikipediaリンク青（操作・選択・照準で唯一使うアクセント）
const DirectX::XMFLOAT4 kColorTextSub         = {0.329f, 0.349f, 0.365f, 1.000f}; // #54595d 副情報（全パネル共通の1トーン）
const DirectX::XMFLOAT4 kColorSuccess         = {0.078f, 0.525f, 0.427f, 1.000f}; // #14866d 良好なライ・成功（意味用途限定）
const DirectX::XMFLOAT4 kColorWarning         = {0.871f, 0.400f, 0.000f, 1.000f}; // #de6600 警告・注意（意味用途限定）
const DirectX::XMFLOAT4 kColorError            = {0.867f, 0.200f, 0.200f, 1.000f}; // #dd3333 危険・OB（意味用途限定）
const DirectX::XMFLOAT4 kColorSpecial         = {0.624f, 0.518f, 0.204f, 1.000f}; // #9f8434 秀逸な記事の星色。目的地・最高評価専用（他の用途に流用しない）
const DirectX::XMFLOAT4 kColorClubSelected    = {0.918f, 0.949f, 1.000f, 0.980f}; // kColorSurfaceRaised と同値
const DirectX::XMFLOAT4 kColorClubNormal      = {0.973f, 0.976f, 0.980f, 0.950f}; // kColorBgDark と同値
const DirectX::XMFLOAT4 kColorShotBtn         = {1.000f, 1.000f, 1.000f, 1.000f}; // 実物のWikipediaボタン相当（白地+グレー枠の控えめな見た目）
const DirectX::XMFLOAT4 kColorShotBtnBorder   = {0.635f, 0.663f, 0.694f, 0.900f}; // kColorBorder と同値
const DirectX::XMFLOAT4 kColorGaugeTick       = {0.000f, 0.000f, 0.000f, 0.350f}; // パワーゲージの閾値目盛り線専用

// =========================================================
// 形状トークン
// 角丸半径・枠線幅・影は種類ごとに1値のみを使う。
// パネルの用途が変わっても見た目の"文法"は変えない。
// Wikipedia本来のフラットな見た目に寄せ、角丸・影ともに控えめにする。
// =========================================================
constexpr float kRadiusPanel       = 4.0f;  // 主要パネル（背景カード全般）
constexpr float kRadiusChip        = 3.0f;  // バッジ・行・ボタン
constexpr float kRadiusBar         = 3.0f;  // ゲージ・バー
constexpr float kBorderWidthThin   = 1.0f;  // 全パネル共通の枠線幅
const DirectX::XMFLOAT4 kShadowColor = {0.000f, 0.000f, 0.000f, 0.160f}; // 全パネル共通の影色（紙が浮いた程度の弱さ）
constexpr float kShadowOffsetY     = 1.5f;  // 全パネル共通の影オフセット

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
// 着弾点プレビューボタン (クラブ選択パネルの右横) - 1280x720固定
// =========================================================
constexpr float kLandingPreviewBtnX      = kClubPanelX + kClubItemW + 10.0f;
constexpr float kLandingPreviewBtnY      = kClubPanelY;
constexpr float kLandingPreviewBtnW      = 120.0f;
constexpr float kLandingPreviewBtnH      = kClubItemH;
constexpr float kLandingPreviewBtnFontSize = 13.0f;

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
constexpr float kJudgeTextY         = 100.0f; // 画面中央ではなく上部に表示する
constexpr float kJudgeFontSize      = 30.0f;
constexpr float kJudgeDisplayTime   = 1.1f;

// 打球判定スタンプ画像（Perfect/Great/Nice/Miss）の表示中心Y座標。
// 実際に表示されるのはこの画像であり、上の kJudgeTextY 系はテキストが
// 常に空文字のまま使われていないため、位置を変える場合はこちらを使う。
constexpr float kJudgeImageCenterY  = 150.0f; // 画面中央ではなく上部に表示する

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
