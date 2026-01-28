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
};

} // namespace graphics
