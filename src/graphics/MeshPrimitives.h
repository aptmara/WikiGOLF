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

  /// @brief 円柱を生成
  static Mesh CreateCylinder(ID3D11Device *device, int segments = 16);

  /// @brief 装飾用の不規則な岩形状を生成
  static Mesh CreateRock(ID3D11Device *device, int rings = 12,
                         int sectors = 18);

  /// @brief 複数の細い葉を交差させた芝クランプを生成
  static Mesh CreateGrassClump(ID3D11Device *device);

  /// @brief 地表を連続して覆うため、葉を面状に分散した芝パッチを生成
  /// @param variantSeed 個体差を出すためのバリアントID（0以上、値ごとに異なる配置になる）
  static Mesh CreateGrassPatch(ID3D11Device *device, uint32_t variantSeed = 0);

  /// @brief フェアウェイ/グリーン用の刈り込み芝パッチを生成。
  ///        ラフ用と異なり株の向きのばらつきを小さく抑え、配置側で
  ///        パッチ全体をまとめて回転させることで刈り跡のように揃って見せる。
  /// @param variantSeed 個体差を出すためのバリアントID（0以上、値ごとに異なる配置になる）
  static Mesh CreateTurfPatch(ID3D11Device *device, uint32_t variantSeed = 0);

  /// @brief フェアウェイ/グリーンの近距離LOD用高密度芝パッチを生成
  /// @param variantSeed 個体差を出すためのバリアントID（0以上、値ごとに異なる配置になる）
  static Mesh CreateDenseTurfPatch(ID3D11Device *device,
                                   uint32_t variantSeed = 0);

  /// @brief 中央の窪みと盛り上がった縁を持つ砂の衝突跡を生成
  static Mesh CreateSandCrater(ID3D11Device *device, int segments = 32);

  /// @brief 平面を生成（UV座標付き、テクスチャ用）
  static Mesh CreatePlane(ID3D11Device *device, float width = 1.0f,
                          float depth = 1.0f);

  /// @brief スクリーン正面向きの四角形を生成。フェードやUI用。
  static Mesh CreateQuad(ID3D11Device *device);
};

} // namespace graphics
