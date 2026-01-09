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
