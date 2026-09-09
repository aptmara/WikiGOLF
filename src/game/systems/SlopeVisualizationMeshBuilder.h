#pragma once
/**
 * @file SlopeVisualizationMeshBuilder.h
 * @brief 地形の傾斜（高低差）を可視化するオーバーレイ用グリッドメッシュを
 *        純粋に構築します。ECS/GPUには一切依存しません。
*/

#include "../../graphics/Mesh.h"
#include <DirectXMath.h>
#include <cstdint>
#include <vector>

namespace game::systems {

class WikiTerrainSystem;

/** @brief 傾斜可視化メッシュの生成パラメータ*/
struct SlopeOverlayConfig {
  float radius = 9.0f;         /**< ボール中心からの表示半径(m)*/
  float cellSize = 0.5f;       /**< グリッドセル間隔(m)*/
  float sampleOffset = 0.35f;  /**< 勾配サンプリング用の隣接オフセット(m)*/
  float maxSlope = 0.12f;      /**< 色が最大(赤)になる傾斜(高さ差/水平距離)*/
  float heightOffset = 0.05f;  /**< 地表からの浮き上がり量(m)*/
};

/**
 * @brief 地形の傾斜を可視化するグリッドクアッドメッシュを構築します。
 * @details 各頂点の色(rgb)に傾斜量に応じた青→赤のグラデーション、
 *          色のアルファ成分に正規化済みの傾斜強度[0,1]、
 *          tangentに傾斜が下る方向(ワールドXZ)を格納します。
 *          実際の縞の流れ表現はピクセルシェーダー側(SlopeOverlayPS.hlsl)で行います。
*/
class SlopeVisualizationMeshBuilder {
public:
  /** @brief 構築結果*/
  struct BuildResult {
    std::vector<graphics::Vertex> vertices;
    std::vector<std::uint32_t> indices;

    bool IsEmpty() const { return vertices.empty() || indices.empty(); }
  };

  /**
   * @brief 中心座標を基準に正方形グリッドの傾斜可視化メッシュを構築します。
   * @param terrain 高さ取得に使う地形システム
   * @param center 表示範囲の中心（通常はボール位置）
   * @param config 生成設定
   * @return 構築済みの頂点・インデックス列
  */
  static BuildResult Build(const WikiTerrainSystem &terrain,
                           const DirectX::XMFLOAT3 &center,
                           const SlopeOverlayConfig &config);

private:
  /** @brief 1点でサンプリングした高さ・傾斜方向・傾斜強度*/
  struct SampledPoint {
    float height = 0.0f;
    DirectX::XMFLOAT2 downhill = {0.0f, 1.0f}; /**< 正規化済み、下る方向(XZ)*/
    float slope01 = 0.0f;                       /**< 正規化済みの傾斜強度[0,1]*/
  };

  static SampledPoint SamplePoint(const WikiTerrainSystem &terrain,
                                  float worldX, float worldZ,
                                  const SlopeOverlayConfig &config);

  /** @brief 傾斜強度[0,1]を青(緩やか)→赤(急)のカラーへ変換します。*/
  static DirectX::XMFLOAT4 SlopeToColor(float slope01);
};

} // namespace game::systems
