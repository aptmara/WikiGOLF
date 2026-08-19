#pragma once
/**
 * @file TextStyle.h
 * @brief テキスト描画スタイル定義
 */

#include <DirectXMath.h>
#include <string>

namespace graphics {

/// @brief テキストの水平アラインメント
enum class TextAlign { Left, Center, Right };

/// @brief テキストの垂直アラインメント
enum class TextVAlign { Top, Middle, Bottom };

/// @brief テキスト描画スタイル
struct TextStyle {
  // 既定値は汎用サンセリフ。用途に応じて各プリセット関数側で
  // Barlow Condensed(ラベル/本文)・Share Tech Mono(数値)・
  // Kiwi Maru(日本語文章)・Mamelon 5 Hi(装飾見出し)を使い分ける。
  std::string fontFamily = "Barlow Condensed";
  float fontSize = 24.0f;
  DirectX::XMFLOAT4 color = {0.1f, 0.1f, 0.1f, 1.0f}; // 黒/ダークグレー基調

  TextAlign align = TextAlign::Left;
  TextVAlign valign = TextVAlign::Top;

  // 影効果
  bool hasShadow = false;
  DirectX::XMFLOAT4 shadowColor = {0.0f, 0.0f, 0.0f, 0.5f};
  float shadowOffsetX = 2.0f;
  float shadowOffsetY = 2.0f;

  // 背景色 (0が透明)
  DirectX::XMFLOAT4 bgColor = {0.0f, 0.0f, 0.0f, 0.0f};
  float cornerRadius = 0.0f; // 角丸半径

  // 枠線
  float borderWidth = 0.0f;
  DirectX::XMFLOAT4 borderColor = {0.0f, 0.0f, 0.0f, 1.0f};

  // グラデーション背景 (bgColorが単色として使用され、これらが設定されていればグラデーション優先)
  bool useGradient = false;
  DirectX::XMFLOAT4 bgGradientEnd = {0.0f, 0.0f, 0.0f, 0.0f};

  // アウトライン効果
  bool hasOutline = false;
  DirectX::XMFLOAT4 outlineColor = {1.0f, 1.0f, 1.0f,
                                    1.0f}; // 白アウトライン（黒文字用）
  float outlineWidth = 1.0f;

  /// @brief 描画に関わる全フィールドの値比較（テキストのラスタキャッシュ/安定度判定用）
  bool operator==(const TextStyle &o) const {
    auto colorEq = [](const DirectX::XMFLOAT4 &a, const DirectX::XMFLOAT4 &b) {
      return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
    };
    return fontFamily == o.fontFamily && fontSize == o.fontSize &&
           colorEq(color, o.color) && align == o.align && valign == o.valign &&
           hasShadow == o.hasShadow && colorEq(shadowColor, o.shadowColor) &&
           shadowOffsetX == o.shadowOffsetX &&
           shadowOffsetY == o.shadowOffsetY && colorEq(bgColor, o.bgColor) &&
           cornerRadius == o.cornerRadius && borderWidth == o.borderWidth &&
           colorEq(borderColor, o.borderColor) &&
           useGradient == o.useGradient &&
           colorEq(bgGradientEnd, o.bgGradientEnd) &&
           hasOutline == o.hasOutline && colorEq(outlineColor, o.outlineColor) &&
           outlineWidth == o.outlineWidth;
  }
  bool operator!=(const TextStyle &o) const { return !(*this == o); }

  /// @brief デフォルトスタイル（黒、24pt、左揃え）
  static TextStyle Default() { return TextStyle{}; }

  /// @brief モダンブラック（明瞭な黒、白縁取り付き）
  static TextStyle ModernBlack() {
    TextStyle s;
    s.fontFamily = "Barlow Condensed SemiBold";
    s.color = {0.0f, 0.0f, 0.0f, 1.0f};
    s.hasOutline = true;
    s.outlineColor = {1.0f, 1.0f, 1.0f, 0.8f};
    s.outlineWidth = 1.5f;
    return s;
  }

  /// @brief FPS表示用スタイル（黄色、影付き）
  static TextStyle FPS() {
    TextStyle s;
    s.fontFamily = "Share Tech Mono";
    s.fontSize = 28.0f;
    s.color = {1.0f, 1.0f, 0.0f, 1.0f};
    s.hasShadow = true;
    s.shadowColor = {0.0f, 0.0f, 0.0f, 0.8f};
    s.shadowOffsetX = 1.5f;
    s.shadowOffsetY = 1.5f;
    return s;
  }

  /// @brief タイトルシーン用：豪華なタイトル
  static TextStyle LuxuryTitle() {
    TextStyle s;
    s.fontFamily = "Times New Roman";
    s.fontSize = 110.0f;
    s.color = {1.0f, 0.95f, 0.7f, 1.0f}; // プラチナゴールド
    s.align = TextAlign::Center;

    s.hasOutline = true;
    s.outlineColor = {0.4f, 0.3f, 0.1f, 0.9f}; // ブロンズ
    s.outlineWidth = 3.0f;

    s.hasShadow = true;
    s.shadowColor = {0.0f, 0.0f, 0.0f, 0.6f};
    s.shadowOffsetX = 6.0f;
    s.shadowOffsetY = 6.0f;
    return s;
  }

  /// @brief タイトルシーン用：スタートボタン
  static TextStyle LuxuryButton() {
    TextStyle s;
    s.fontFamily = "Barlow Condensed SemiBold";
    s.fontSize = 42.0f;
    s.color = {1.0f, 1.0f, 1.0f, 1.0f};
    s.align = TextAlign::Center;

    s.hasOutline = true;
    s.outlineColor = {0.0f, 0.2f, 0.3f, 0.8f};
    s.outlineWidth = 1.0f;

    s.hasShadow = true;
    s.shadowColor = {0.0f, 0.0f, 0.0f, 0.5f};
    return s;
  }

  /// @brief ガイド表示用（白文字 + 太黒縁 + 影） - どんな背景でも読める最強設定
  static TextStyle Guide() {
    TextStyle s;
    s.fontFamily = "Kiwi Maru Medium"; // 操作ガイド等は日本語主体のため丸ゴシック
    s.fontSize = 24.0f; // 読みやすく小さめに
    s.color = {1.0f, 1.0f, 1.0f, 1.0f};
    s.align = TextAlign::Center;

    // 強力なアウトライン
    s.hasOutline = true;
    s.outlineColor = {0.0f, 0.0f, 0.0f, 1.0f};
    s.outlineWidth = 2.0f;

    // ドロップシャドウ
    s.hasShadow = true;
    s.shadowColor = {0.0f, 0.0f, 0.0f, 0.8f};
    s.shadowOffsetX = 2.0f;
    s.shadowOffsetY = 2.0f;

    return s;
  }

  /// @brief ステータス表示用（特大白文字 + 影）
  static TextStyle Status() {
    TextStyle s;
    s.fontFamily = "Barlow Condensed Black";
    s.fontSize = 28.0f; // 適度なサイズ
    s.color = {1.0f, 1.0f, 1.0f, 1.0f};

    s.hasShadow = true;
    s.shadowColor = {0.0f, 0.0f, 0.0f, 0.8f};
    s.shadowOffsetX = 3.0f;
    s.shadowOffsetY = 3.0f;

    return s;
  }

  /// @brief タイトル用スタイル（大きめ、中央揃え）
  static TextStyle Title() {
    TextStyle s;
    s.fontFamily = "Mamelon 5 Hi";
    s.fontSize = 48.0f;
    s.align = TextAlign::Center;
    s.valign = TextVAlign::Middle;
    s.hasShadow = true;
    s.hasOutline = true;
    return s;
  }

  // -----------------------------------------------------------------
  // ゲームHUD用タイポスケール
  // 背景パネル側で面・枠線・影を統一して持たせるため、以下のテキスト
  // スタイルはフォントサイズ・色・揃えのみを定義し、独自の背景/枠線は
  // 持たない。影は可読性のための最小限の一種類だけを共通で使う。
  // -----------------------------------------------------------------
  /// @brief HUDの見出しテキスト: 現在ページ名・パネルタイトル用
  static TextStyle BrowserURL() {
    TextStyle s;
    s.fontFamily = "Kiwi Maru Medium";
    s.fontSize = 20.0f;
    s.color = {0.960f, 0.965f, 0.975f, 1.0f};
    s.align = TextAlign::Left;
    s.hasShadow = true;
    s.shadowColor = {0.0f, 0.0f, 0.0f, 0.35f};
    s.shadowOffsetX = 0.0f;
    s.shadowOffsetY = 1.0f;
    return s;
  }

  /// @brief HUD副情報: 打数/Par・経路履歴・クラブ種別等（小さめ・淡色）
  static TextStyle BrowserSub() {
    TextStyle s;
    s.fontFamily = "Barlow Condensed Medium";
    s.fontSize = 12.0f;
    s.color = {0.620f, 0.660f, 0.700f, 1.0f};
    s.align = TextAlign::Left;
    s.hasShadow = true;
    s.shadowColor = {0.0f, 0.0f, 0.0f, 0.35f};
    s.shadowOffsetX = 0.0f;
    s.shadowOffsetY = 1.0f;
    return s;
  }

  /// @brief カードラベル: WIND / TO TARGET 等の見出し（小・全パネル共通の淡色）
  static TextStyle CardLabel() {
    TextStyle s;
    s.fontFamily = "Barlow Condensed SemiBold";
    s.fontSize = 12.0f;
    s.color = {0.620f, 0.660f, 0.700f, 1.0f};
    s.align = TextAlign::Left;
    s.hasShadow = false;
    return s;
  }

  /// @brief カード数値: 風速・距離等の主要数値（大・白）
  static TextStyle CardValue() {
    TextStyle s;
    s.fontFamily = "Share Tech Mono";
    s.fontSize = 26.0f;
    s.color = {0.960f, 0.965f, 0.975f, 1.0f};
    s.align = TextAlign::Left;
    s.hasShadow = true;
    s.shadowColor = {0.0f, 0.0f, 0.0f, 0.35f};
    s.shadowOffsetX = 0.0f;
    s.shadowOffsetY = 1.0f;
    return s;
  }

  /// @brief 目的地強調: ターゲットページ名専用（金色はこの用途にのみ使う）
  static TextStyle GoalHighlight() {
    TextStyle s;
    s.fontFamily = "Mamelon 5 Hi";
    s.fontSize = 20.0f;
    s.color = {0.980f, 0.780f, 0.260f, 1.0f};
    s.align = TextAlign::Left;
    s.hasShadow = true;
    s.shadowColor = {0.0f, 0.0f, 0.0f, 0.35f};
    s.shadowOffsetX = 0.0f;
    s.shadowOffsetY = 1.0f;
    return s;
  }

  /// @brief ショットパネルラベル: POWER/ACCURACY等の見出し
  static TextStyle ShotPanelLabel() {
    TextStyle s;
    s.fontFamily = "Barlow Condensed SemiBold";
    s.fontSize = 12.0f;
    s.color = {0.620f, 0.660f, 0.700f, 1.0f};
    s.align = TextAlign::Left;
    s.hasShadow = false;
    return s;
  }

  /// @brief ショットパネル値: パーセント等の主要数値
  static TextStyle ShotPanelValue() {
    TextStyle s;
    s.fontFamily = "Share Tech Mono";
    s.fontSize = 22.0f;
    s.color = {0.960f, 0.965f, 0.975f, 1.0f};
    s.align = TextAlign::Right;
    s.hasShadow = true;
    s.shadowColor = {0.0f, 0.0f, 0.0f, 0.35f};
    s.shadowOffsetX = 0.0f;
    s.shadowOffsetY = 1.0f;
    return s;
  }

  /// @brief クラブ名テキスト（カード内・白・中央揃え）
  static TextStyle ClubName() {
    TextStyle s;
    s.fontFamily = "Kiwi Maru Medium";
    s.fontSize = 16.0f;
    s.color = {0.960f, 0.965f, 0.975f, 1.0f};
    s.align = TextAlign::Center;
    s.hasShadow = true;
    s.shadowColor = {0.0f, 0.0f, 0.0f, 0.35f};
    s.shadowOffsetX = 0.0f;
    s.shadowOffsetY = 1.0f;
    return s;
  }
};

} // namespace graphics
