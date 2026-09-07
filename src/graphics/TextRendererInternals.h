/**
 * @file TextRendererInternals.h
 * @brief TextRenderer 内部で共有するハッシュ計算処理
 */
#pragma once

#include "TextStyle.h"
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>

namespace graphics::text_renderer_detail {

/// @brief float のビット表現をキャッシュキーへ変換します。
inline uint32_t FloatBits(float value) {
  uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

/// @brief FNV-1a 風の結合でハッシュ値を拡張します。
inline size_t HashCombine(size_t seed, size_t value) {
  return seed * 1099511628211ull ^ value;
}

/// @brief RGBA 色の全成分をハッシュ化します。
inline size_t HashColor(const DirectX::XMFLOAT4 &color) {
  size_t hash = 1469598103934665603ull;
  hash = HashCombine(hash, FloatBits(color.x));
  hash = HashCombine(hash, FloatBits(color.y));
  hash = HashCombine(hash, FloatBits(color.z));
  hash = HashCombine(hash, FloatBits(color.w));
  return hash;
}

/// @brief テキスト描画スタイルをキャッシュキー用にハッシュ化します。
inline size_t HashStyle(const TextStyle &style) {
  size_t hash = 1469598103934665603ull;
  hash = HashCombine(hash, std::hash<std::string>{}(style.fontFamily));
  hash = HashCombine(hash, FloatBits(style.fontSize));
  hash = HashCombine(hash, HashColor(style.color));
  hash = HashCombine(hash, static_cast<size_t>(style.align));
  hash = HashCombine(hash, static_cast<size_t>(style.valign));
  hash = HashCombine(hash, static_cast<size_t>(style.hasShadow));
  hash = HashCombine(hash, HashColor(style.shadowColor));
  hash = HashCombine(hash, FloatBits(style.shadowOffsetX));
  hash = HashCombine(hash, FloatBits(style.shadowOffsetY));
  hash = HashCombine(hash, HashColor(style.bgColor));
  hash = HashCombine(hash, FloatBits(style.cornerRadius));
  hash = HashCombine(hash, FloatBits(style.borderWidth));
  hash = HashCombine(hash, HashColor(style.borderColor));
  hash = HashCombine(hash, static_cast<size_t>(style.useGradient));
  hash = HashCombine(hash, HashColor(style.bgGradientEnd));
  hash = HashCombine(hash, static_cast<size_t>(style.hasOutline));
  hash = HashCombine(hash, HashColor(style.outlineColor));
  hash = HashCombine(hash, FloatBits(style.outlineWidth));
  return hash;
}

} // namespace graphics::text_renderer_detail

