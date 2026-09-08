#include "src/game/systems/TerrainLayoutRules.h"
#include "src/game/systems/TerrainObstacleLayout.h"
#include <cmath>
#include <cstdlib>
#include <iostream>

#define CHECK(condition, message)                                             \
    do {                                                                       \
        if (!(condition)) {                                                    \
            std::cerr << "[FAIL] " << message << "\n";                       \
            std::exit(1);                                                      \
        }                                                                      \
        std::cout << "[PASS] " << message << "\n";                           \
    } while (false)

int main() {
    using game::systems::TerrainLayoutRules;

    const auto minimum = TerrainLayoutRules::CalculateResolution(1.0f, 1.0f);
    CHECK(minimum.x == 96 && minimum.z == 96,
          "小さいフィールドの解像度を下限へ制限する");

    const auto maximum = TerrainLayoutRules::CalculateResolution(1000.0f, 1000.0f);
    CHECK(maximum.x == 160 && maximum.z == 320,
          "大きいフィールドの解像度を軸ごとの上限へ制限する");

    CHECK(TerrainLayoutRules::DetermineBiome({"科学技術"}, "記事") == 2,
          "科学カテゴリをバイオーム2へ分類する");
    CHECK(TerrainLayoutRules::DetermineBiome({"歴史"}, "記事") == 1,
          "歴史カテゴリをバイオーム1へ分類する");

    game::systems::TerrainData data;
    data.config.resolutionX = 2;
    data.config.resolutionZ = 2;
    data.visualMaterialColors = {
        {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
    const auto center = TerrainLayoutRules::SampleVisualMaterialColor(data, 0.5f, 0.5f);
    CHECK(std::fabs(center.x - 0.25f) < 0.0001f &&
              std::fabs(center.y - 0.25f) < 0.0001f &&
              std::fabs(center.z - 0.25f) < 0.0001f,
          "材質色を4隅から双線形補間する");

    std::vector<graphics::Vertex> source(16);
    source[0].position.y = 1.0f;
    source[15].position.y = 9.0f;
    std::vector<std::uint32_t> indices;
    const auto grid = TerrainLayoutRules::BuildMinimapTerrainGrid(
        source, 4, 4, indices);
    CHECK(grid.size() == 16 && indices.size() == 54,
          "ミニマップ用グリッドの頂点数と三角形数を保つ");
    CHECK(grid.front().normal.y == 1.0f && grid.front().tangent.x == 1.0f,
          "ミニマップ頂点へ既定の法線と接線を設定する");

    const auto quad = TerrainLayoutRules::BuildMinimapOverlayQuad(
        20.0f, 5.0f, -5.0f, 3.0f, indices);
    CHECK(quad.size() == 4 && indices.size() == 6,
          "オーバーレイ平面を4頂点6インデックスで生成する");
    CHECK(std::fabs(TerrainLayoutRules::ComputeMaxVertexHeight(source) - 9.0f) <
              0.0001f,
          "頂点列の最大高さを取得する");

    const auto walls = game::systems::TerrainObstacleLayout::BuildWalls(
        80.0f, 120.0f);
    CHECK(walls.size() == 4 &&
              std::fabs(walls[0].position.x + 43.0f) < 0.0001f &&
              std::fabs(walls[2].position.z - 63.0f) < 0.0001f,
          "フィールド外周へ4枚の壁を配置する");
    CHECK(std::fabs(walls[0].position.x + walls[0].scale.x * 0.5f +
                    40.0f) < 0.0001f &&
              std::fabs(walls[1].position.x - walls[1].scale.x * 0.5f -
                        40.0f) < 0.0001f &&
              std::fabs(walls[2].position.z - walls[2].scale.z * 0.5f -
                        60.0f) < 0.0001f &&
              std::fabs(walls[3].position.z + walls[3].scale.z * 0.5f +
                        60.0f) < 0.0001f,
          "壁の内側面をフィールド外周へ一致させる");
    CHECK(std::fabs(walls[0].scale.x - 6.0f) < 0.0001f &&
              std::fabs(walls[0].scale.y - 100.0f) < 0.0001f &&
              std::fabs(walls[0].scale.z - 120.0f) < 0.0001f &&
              std::fabs(walls[2].scale.x - 80.0f) < 0.0001f &&
              std::fabs(walls[2].scale.y - 100.0f) < 0.0001f &&
              std::fabs(walls[2].scale.z - 6.0f) < 0.0001f,
          "壁の表示寸法をコライダー寸法と一致させる");

    graphics::ImageRegion image;
    image.x = 100.0f;
    image.y = 200.0f;
    image.width = 200.0f;
    image.height = 100.0f;
    graphics::WikiTextureResult texture;
    texture.width = 1000;
    texture.height = 1000;
    texture.images.push_back(image);
    const auto obstacles =
        game::systems::TerrainObstacleLayout::BuildImageObstacles(
            texture, 80.0f, 120.0f);
    CHECK(obstacles.size() == 1 &&
              std::fabs(obstacles[0].position.x + 24.0f) < 0.0001f &&
              std::fabs(obstacles[0].position.z - 30.0f) < 0.0001f &&
              std::fabs(obstacles[0].scale.x - 16.0f) < 0.0001f &&
              std::fabs(obstacles[0].scale.z - 12.0f) < 0.0001f,
          "記事画像をワールド上の障害物へ変換する");

    std::cout << "All terrain layout rules tests passed!\n";
    return 0;
}
