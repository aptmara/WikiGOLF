#pragma once

/**
 * @file TerrainLayoutRules.h
 * @brief 地形生成で共有する純粋なレイアウト計算を定義します。
 */

#include "TerrainGenerator.h"
#include "../../graphics/Mesh.h"
#include <DirectXMath.h>
#include <cstdint>
#include <string>
#include <vector>

namespace game::systems {

/** @brief 地形頂点グリッドの解像度です。 */
struct TerrainResolution {
    int x = 0;
    int z = 0;
};

/**
 * @brief 地形の座標変換とミニマップメッシュの規則を提供します。
 *
 * ECS、GPU、ファイル、乱数へアクセスしないため、入力だけで結果を再現できます。
 */
class TerrainLayoutRules {
public:
    /** @brief フィールド寸法から生成解像度を求めます。 */
    static TerrainResolution CalculateResolution(float width, float depth);

    /** @brief 地形データの補間色をUV座標から取得します。 */
    static DirectX::XMFLOAT3 SampleVisualMaterialColor(
        const TerrainData& data, float u, float v);

    /** @brief 本描画用頂点をミニマップ用に間引きます。 */
    static std::vector<graphics::Vertex> BuildMinimapTerrainGrid(
        const std::vector<graphics::Vertex>& source,
        int sourceResolutionX, int sourceResolutionZ,
        std::vector<std::uint32_t>& outIndices);

    /** @brief 記事テクスチャ用のミニマップ平面を生成します。 */
    static std::vector<graphics::Vertex> BuildMinimapOverlayQuad(
        float fieldWidth, float zTop, float zBottom, float flatY,
        std::vector<std::uint32_t>& outIndices);

    /** @brief 頂点列から最大Y座標を取得します。 */
    static float ComputeMaxVertexHeight(
        const std::vector<graphics::Vertex>& vertices);

    /** @brief 記事カテゴリとタイトルからバイオーム番号を決めます。 */
    static int DetermineBiome(const std::vector<std::string>& categories,
                              const std::string& pageTitle);
};

} // namespace game::systems
