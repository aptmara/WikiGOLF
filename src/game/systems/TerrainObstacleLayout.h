#pragma once

/**
 * @file TerrainObstacleLayout.h
 * @brief 壁と画像障害物の座標計算を定義します。
 */

#include "../../graphics/WikiTextureGenerator.h"
#include <DirectXMath.h>
#include <vector>

namespace game::systems {

/** @brief 外周壁1枚の配置情報です。 */
struct WallLayout {
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 scale;
    DirectX::XMFLOAT4 rotation;
    DirectX::XMFLOAT3 colliderSize;
};

/** @brief 記事画像1枚に対応する障害物の配置情報です。 */
struct ImageObstacleLayout {
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 scale;
};

/**
 * @brief 地形障害物の純粋な配置計算を提供します。
 */
class TerrainObstacleLayout {
public:
    /** @brief フィールド外周4枚の壁配置を返します。 */
    static std::vector<WallLayout> BuildWalls(float fieldWidth,
                                              float fieldDepth);

    /** @brief 記事画像から障害物配置を返します。 */
    static std::vector<ImageObstacleLayout> BuildImageObstacles(
        const graphics::WikiTextureResult& texture,
        float fieldWidth, float fieldDepth);
};

} // namespace game::systems
