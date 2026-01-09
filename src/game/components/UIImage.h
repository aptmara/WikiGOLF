#pragma once
/**
 * @file UIImage.h
 * @brief UI画像コンポーネント
 */

#include <DirectXMath.h>
#include <d3d11.h>
#include <string>

namespace game::components {

/// @brief UI画像コンポーネント
struct UIImage {
  std::string texturePath; ///< 画像パス（resourcesフォルダからの相対パス）
  ID3D11ShaderResourceView *textureSRV =
      nullptr; ///< 動的テクスチャ用 (Raw Pointer)

  float x = 0.0f;      ///< X座標
  float y = 0.0f;      ///< Y座標
  float width = 0.0f;  ///< 幅（0 = 元サイズ）
  float height = 0.0f; ///< 高さ（0 = 元サイズ）

  float rotation = 0.0f; ///< 回転角（ラジアン）
  float alpha = 1.0f;    ///< 透明度（0.0-1.0）

  bool visible = true; ///< 可視性
  int layer = 0;       ///< レイヤー（大きいほど手前）

  /// @brief 簡易コンストラクタ
  static UIImage Create(const std::string &path, float x, float y) {
    UIImage ui;
    ui.texturePath = path;
    ui.x = x;
    ui.y = y;
    return ui;
  }
};

} // namespace game::components
