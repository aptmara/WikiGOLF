#pragma once
/**
 * @file MeshPrimitives.h
 * @brief プリミティブメッシュ生成ファクトリ
 */

#include "Mesh.h"
#include <d3d11.h>
#include <vector>

namespace graphics {

/**
 * @brief 基本的な形状のメッシュを生成するクラス
 */
class MeshPrimitives {
public:
  /// @brief 三角形を生成
  static Mesh CreateTriangle(ID3D11Device *device);

  /// @brief 立方体を生成
  static Mesh CreateCube(ID3D11Device *device);

  /// @brief 球体を生成
  static Mesh CreateSphere(ID3D11Device *device, int segments = 16);

  /// @brief 平面を生成（UV座標付き、テクスチャ用）
  static Mesh CreatePlane(ID3D11Device *device, float width = 1.0f,
                          float depth = 1.0f);
};

} // namespace graphics
