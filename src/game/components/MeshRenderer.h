#pragma once
/**
 * @file MeshRenderer.h
 * @brief MeshRenderer Component
 */

#include "../../resources/ResourceManager.h"
#include <d3d11.h>
#include <wrl/client.h>

namespace game::components {

/// @brief 描画時のブレンドモード
enum class BlendMode {
    Opaque,     ///< 不透明
    Alpha,      ///< 標準的なアルファブレンド (SrcAlpha, InvSrcAlpha)
    Multiply,   ///< 乗算 (DestColor * SrcColor)
    Add         ///< 加算 (DestColor + SrcColor)
};

/// @brief ミニマップ（俯瞰オフスクリーン）描画時の扱い
/// @details デフォルトは除外。ミニマップに映すオブジェクトは地形タイルと
///          記事オーバーレイタイルのみに限定するための明示フラグ。
enum class MinimapRenderMode {
    None,         ///< ミニマップには描画しない（デフォルト）
    VertexColor,  ///< 頂点カラーのみで描画（Texture2DArrayはサンプルしない。地形ベースメッシュ用）
    Textured,     ///< Texture2Dをサンプルして描画（記事オーバーレイタイル用）
};

struct MeshRenderer {
  resources::MeshHandle mesh;
  // ミニマップ専用の簡略化メッシュ（既定は無効＝ミニマップ描画対象から除外）。
  // 本描画には一切使用しない。地形/オーバーレイタイル生成時にのみ設定される。
  resources::MeshHandle minimapMesh = resources::MeshHandle::Invalid();
  resources::ShaderHandle shader;
  bool isVisible = true;
  DirectX::XMFLOAT4 color = {1.0f, 1.0f, 1.0f, 1.0f}; // マテリアルカラー

  // テクスチャ（オプション）
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> textureSRV;
  bool hasTexture = false;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> normalMapSRV;
  bool hasNormalMap = false;

  // 追加フラグ（シェーダー用）
  DirectX::XMFLOAT4 customFlags = {0, 0, 0, 0};
  bool isTransparent = false;
  BlendMode blendMode = BlendMode::Opaque;

  // 0以下なら距離LODを無効化する。フラスタムカリングは常に適用される。
  float maxDrawDistance = 0.0f;
  float boundsScale = 1.0f;

  // ミニマップ描画対象への登録（デフォルトは除外）
  MinimapRenderMode minimapMode = MinimapRenderMode::None;
};

} // namespace game::components
