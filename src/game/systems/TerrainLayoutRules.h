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

/** @brief 地形頂点グリッドの解像度です。*/
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
    /** @brief フィールド寸法から生成解像度を求めます。*/
    static TerrainResolution CalculateResolution(float width, float depth);

    /** @brief 地形データの補間色をUV座標から取得します。*/
    static DirectX::XMFLOAT3 SampleVisualMaterialColor(
        const TerrainData& data, float u, float v);

    /** @brief 本描画用頂点をミニマップ用に間引きます。*/
    static std::vector<graphics::Vertex> BuildMinimapTerrainGrid(
        const std::vector<graphics::Vertex>& source,
        int sourceResolutionX, int sourceResolutionZ,
        std::vector<std::uint32_t>& outIndices);

    /** @brief 記事テクスチャ用のミニマップ平面を生成します。*/
    static std::vector<graphics::Vertex> BuildMinimapOverlayQuad(
        float fieldWidth, float zTop, float zBottom, float flatY,
        std::vector<std::uint32_t>& outIndices);

    /**
     * @brief 地形タイルのワールド座標から記事テクスチャ UV を求める係数を返します。
     * @details u = x * X + 0.5、v = z * Y + Z（W は 0）。地形シェーダーで記事を重ねるときに使う。
     *          タイルの頂点に付けていた UV（u: 横 0..1、v: タイル内 0..1）と同じ値になる。
     * @param fieldWidth フィールドの幅
     * @param fieldDepth フィールドの奥行き
     * @param vStart タイルの開始位置（記事全体の高さに対する割合）
     * @param vEnd タイルの終了位置（同上）
     */
    static DirectX::XMFLOAT4 ComputeArticleOverlayUv(float fieldWidth,
                                                     float fieldDepth,
                                                     float vStart, float vEnd) {
        const float vSpan = vEnd - vStart;
        if (fieldWidth <= 0.0f || fieldDepth <= 0.0f || vSpan <= 0.0f) {
            return {0.0f, 0.0f, 0.0f, 0.0f};
        }
        return {1.0f / fieldWidth, -1.0f / (fieldDepth * vSpan),
                (0.5f - vStart) / vSpan, 0.0f};
    }

    /** @brief 頂点列から最大Y座標を取得します。*/
    static float ComputeMaxVertexHeight(
        const std::vector<graphics::Vertex>& vertices);

    /** @brief 記事カテゴリとタイトルからバイオーム番号を決めます。*/
    static int DetermineBiome(const std::vector<std::string>& categories,
                              const std::string& pageTitle);
};

} // namespace game::systems
