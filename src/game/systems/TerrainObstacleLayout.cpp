/**
 * @file TerrainObstacleLayout.cpp
 * @brief 壁と画像障害物の座標計算を実装します。
*/

#include "TerrainObstacleLayout.h"

namespace game::systems {

std::vector<WallLayout> TerrainObstacleLayout::BuildWalls(
    float fieldWidth, float fieldDepth) {
    constexpr float wallHeight = 100.0f;
    constexpr float wallThickness = 6.0f;
    const float halfWidth = fieldWidth * 0.5f;
    const float halfDepth = fieldDepth * 0.5f;

    std::vector<WallLayout> walls;
    walls.reserve(4);
    walls.push_back({
        {-halfWidth - wallThickness * 0.5f, wallHeight * 0.5f, 0.0f},
        {wallThickness, wallHeight, fieldDepth},
        {0.0f, 0.0f, 0.0f, 1.0f},
        {1.0f, 1.0f, 1.0f}});
    walls.push_back({
        {halfWidth + wallThickness * 0.5f, wallHeight * 0.5f, 0.0f},
        {wallThickness, wallHeight, fieldDepth},
        {0.0f, 0.0f, 0.0f, 1.0f},
        {1.0f, 1.0f, 1.0f}});
    walls.push_back({
        {0.0f, wallHeight * 0.5f, halfDepth + wallThickness * 0.5f},
        {fieldWidth, wallHeight, wallThickness},
        {0.0f, 0.0f, 0.0f, 1.0f},
        {1.0f, 1.0f, 1.0f}});
    walls.push_back({
        {0.0f, wallHeight * 0.5f, -halfDepth - wallThickness * 0.5f},
        {fieldWidth, wallHeight, wallThickness},
        {0.0f, 0.0f, 0.0f, 1.0f},
        {1.0f, 1.0f, 1.0f}});
    return walls;
}

std::vector<ImageObstacleLayout> TerrainObstacleLayout::BuildImageObstacles(
    const graphics::WikiTextureResult& texture,
    float fieldWidth, float fieldDepth) {
    if (texture.width == 0 || texture.height == 0) {
        return {};
    }

    const float textureWidth = static_cast<float>(texture.width);
    const float textureHeight = static_cast<float>(texture.height);
    std::vector<ImageObstacleLayout> obstacles;
    obstacles.reserve(texture.images.size());
    for (const auto& image : texture.images) {
        const float centerX = image.x + image.width * 0.5f;
        const float centerY = image.y + image.height * 0.5f;
        const float worldX =
            (centerX / textureWidth - 0.5f) * fieldWidth;
        const float worldZ =
            (0.5f - centerY / textureHeight) * fieldDepth;
        const float worldWidth = image.width / textureWidth * fieldWidth;
        const float worldDepth = image.height / textureHeight * fieldDepth;
        obstacles.push_back({{worldX, 1.0f, worldZ},
                             {worldWidth, 2.0f, worldDepth}});
    }
    return obstacles;
}

} // namespace game::systems
