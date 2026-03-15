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
  std::string fontFamily = "Mamelon 5 Hi-Regular";
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

  /// @brief デフォルトスタイル（黒、24pt、左揃え）
  static TextStyle Default() { return TextStyle{}; }

  /// @brief モダンブラック（明瞭な黒、白縁取り付き）
  static TextStyle ModernBlack() {
    TextStyle s;
    s.color = {0.0f, 0.0f, 0.0f, 1.0f};
    s.hasOutline = true;
    s.outlineColor = {1.0f, 1.0f, 1.0f, 0.8f};
    s.outlineWidth = 1.5f;
    return s;
  }

  /// @brief FPS表示用スタイル（黄色、影付き）
  static TextStyle FPS() {
    TextStyle s;
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
    s.fontSize = 48.0f;
    s.align = TextAlign::Center;
    s.valign = TextVAlign::Middle;
    s.hasShadow = true;
    s.hasOutline = true;
    return s;
  }

  /// @brief ブラウザURLバー風: 現在ページ表示用（太字・白・左揃え）
  static TextStyle BrowserURL() {
    TextStyle s;
    s.fontSize = 22.0f;
    s.color = {0.973f, 0.980f, 0.988f, 1.0f}; // #F8FAFC
    s.align = TextAlign::Left;

    // 半透明ダーク背景パネル（角丸）
    s.bgColor = {0.059f, 0.090f, 0.165f, 0.85f}; // #0F172A
    s.cornerRadius = 8.0f;

    // 薄い枠線（グラスモーフィズム）
    s.borderWidth = 1.0f;
    s.borderColor = {0.220f, 0.380f, 0.600f, 0.5f}; // 青みがかった枠

    // ドロップシャドウ
    s.hasShadow = true;
    s.shadowColor = {0.0f, 0.0f, 0.0f, 0.6f};
    s.shadowOffsetX = 0.0f;
    s.shadowOffsetY = 2.0f;

    return s;
  }

  /// @brief ブラウザ副情報: 打数/Par・経路履歴用（小さめ・薄白）
  static TextStyle BrowserSub() {
    TextStyle s;
    s.fontSize = 18.0f;
    s.color = {0.792f, 0.835f, 0.886f, 1.0f}; // #CBD5E1
    s.align = TextAlign::Left;

    s.hasShadow = true;
    s.shadowColor = {0.0f, 0.0f, 0.0f, 0.5f};
    s.shadowOffsetX = 1.0f;
    s.shadowOffsetY = 1.0f;

    return s;
  }

  /// @brief カードラベル: WIND / CLUB等の見出し用（小・大文字感・スカイブルー）
  static TextStyle CardLabel() {
    TextStyle s;
    s.fontSize = 13.0f;
    s.color = {0.220f, 0.745f, 0.973f, 1.0f}; // #38BDF8
    s.align = TextAlign::Left;

    s.hasShadow = false;
    s.hasOutline = false;

    return s;
  }

  /// @brief カード数値: 風速等の大きい数値用（大・白・太字）
  static TextStyle CardValue() {
    TextStyle s;
    s.fontSize = 28.0f;
    s.color = {0.973f, 0.980f, 0.988f, 1.0f}; // #F8FAFC
    s.align = TextAlign::Left;

    s.hasShadow = true;
    s.shadowColor = {0.0f, 0.0f, 0.0f, 0.4f};
    s.shadowOffsetX = 1.0f;
    s.shadowOffsetY = 1.0f;

    return s;
  }

  /// @brief ゴール強調: ターゲットページ名（金色・大・中央揃え）
  static TextStyle GoalHighlight() {
    TextStyle s;
    s.fontSize = 22.0f;
    s.color = {0.980f, 0.800f, 0.082f, 1.0f}; // #FACC15
    s.align = TextAlign::Left;

    s.hasShadow = true;
    s.shadowColor = {0.2f, 0.1f, 0.0f, 0.7f};
    s.shadowOffsetX = 1.0f;
    s.shadowOffsetY = 2.0f;

    return s;
  }

  /// @brief ショットパネルラベル: Power/Accuracy表示用（小・スカイブルー）
  static TextStyle ShotPanelLabel() {
    TextStyle s;
    s.fontSize = 15.0f;
    s.color = {0.220f, 0.745f, 0.973f, 0.9f}; // #38BDF8
    s.align = TextAlign::Left;
    s.hasShadow = false;
    return s;
  }

  /// @brief ショットパネル値: パーセント等（白・やや大）
  static TextStyle ShotPanelValue() {
    TextStyle s;
    s.fontSize = 20.0f;
    s.color = {0.973f, 0.980f, 0.988f, 1.0f}; // #F8FAFC
    s.align = TextAlign::Right;
    s.hasShadow = true;
    s.shadowColor = {0.0f, 0.0f, 0.0f, 0.5f};
    s.shadowOffsetX = 1.0f;
    s.shadowOffsetY = 1.0f;
    return s;
  }

  /// @brief クラブ名テキスト（カード内・白・太）
  static TextStyle ClubName() {
    TextStyle s;
    s.fontSize = 16.0f;
    s.color = {0.973f, 0.980f, 0.988f, 1.0f}; // #F8FAFC
    s.align = TextAlign::Center;
    s.hasShadow = true;
    s.shadowColor = {0.0f, 0.0f, 0.0f, 0.6f};
    s.shadowOffsetX = 1.0f;
    s.shadowOffsetY = 1.0f;
    return s;
  }
};

} // namespace graphics
