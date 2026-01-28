#pragma once
/**
 * @file Skybox.h
 * @brief スカイボックスコンポーネント
 */

#include <DirectXMath.h>
#include <d3d11.h>
#include <wrl/client.h>


namespace game::components {

/**
 * @brief スカイボックスコンポーネント
 *
 * キューブマップテクスチャを使用した背景スカイボックス
 */
struct Skybox {
  /// @brief キューブマップのShaderResourceView
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> cubemapSRV;

  /// @brief 描画の有効/無効
  bool isVisible = true;

  /// @brief 明度調整 (0.0 = 暗い, 1.0 = 標準, 2.0 = 明るい)
  float brightness = 0.7f; // 床の文字を見やすくするため控えめ

  /// @brief 彩度調整 (0.0 = グレースケール, 1.0 = 標準)
  float saturation = 0.8f; // やや彩度を抑えめ

  /// @brief ティント色（追加の色調整用）
  DirectX::XMFLOAT4 tintColor = {1.0f, 1.0f, 1.0f, 1.0f};
};

} // namespace game::components
