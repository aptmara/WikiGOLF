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
  /** @brief 三角形を生成 */
  static Mesh CreateTriangle(ID3D11Device *device);

  /** @brief 立方体を生成 */
  static Mesh CreateCube(ID3D11Device *device);

  /** @brief 球体を生成 */
  static Mesh CreateSphere(ID3D11Device *device, int segments = 16);

  /** @brief 円柱を生成 */
  static Mesh CreateCylinder(ID3D11Device *device, int segments = 16);

  /**
   * @brief ゴルフカップの内壁と底を生成
   * @details 半径0.5、上端 y=0・底 y=-1。面は内側（上から覗き込む方向）を向き、
   *          上端付近は白いカップ、奥ほど暗くなる頂点カラーを持つ。
   */
  static Mesh CreateGolfCup(ID3D11Device *device, int segments = 40);

  /** @brief 装飾用の不規則な岩形状を生成 */
  static Mesh CreateRock(ID3D11Device *device, int rings = 12,
                         int sectors = 18);

  /** @brief 中央の窪みと盛り上がった縁を持つ砂の衝突跡を生成 */
  static Mesh CreateSandCrater(ID3D11Device *device, int segments = 32);

  /** @brief 平面を生成（UV座標付き、テクスチャ用） */
  static Mesh CreatePlane(ID3D11Device *device, float width = 1.0f,
                          float depth = 1.0f);

  /** @brief スクリーン正面向きの四角形を生成。フェードやUI用。 */
  static Mesh CreateQuad(ID3D11Device *device);
};

} // namespace graphics
