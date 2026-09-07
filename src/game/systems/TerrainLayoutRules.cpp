/**
 * @file TerrainLayoutRules.cpp
 * @brief 地形レイアウト計算を実装します。
 */

#include "TerrainLayoutRules.h"
#include <algorithm>
#include <cmath>
#include <functional>

namespace game::systems {
namespace {

constexpr float kTerrainVertexSpacing = 0.8f;
constexpr int kMinimapTileGridRes = 8;

float LerpValue(float left, float right, float amount) {
    return left + (right - left) * amount;
}

} // namespace

TerrainResolution TerrainLayoutRules::CalculateResolution(
    float width, float depth) {
    TerrainResolution resolution;
    resolution.x = std::clamp(
        static_cast<int>(std::ceil(width / kTerrainVertexSpacing)) + 1,
        96, 160);
    resolution.z = std::clamp(
        static_cast<int>(std::ceil(depth / kTerrainVertexSpacing)) + 1,
        96, 320);
    return resolution;
}

DirectX::XMFLOAT3 TerrainLayoutRules::SampleVisualMaterialColor(
    const TerrainData& data, float u, float v) {
    const int resolutionX = data.config.resolutionX;
    const int resolutionZ = data.config.resolutionZ;
    const float fx = std::clamp(u, 0.0f, 1.0f) *
                     static_cast<float>(resolutionX - 1);
    const float fz = std::clamp(v, 0.0f, 1.0f) *
                     static_cast<float>(resolutionZ - 1);
    const int x0 = std::clamp(static_cast<int>(std::floor(fx)), 0,
                              resolutionX - 1);
    const int z0 = std::clamp(static_cast<int>(std::floor(fz)), 0,
                              resolutionZ - 1);
    const int x1 = std::min(x0 + 1, resolutionX - 1);
    const int z1 = std::min(z0 + 1, resolutionZ - 1);
    const float tx = fx - static_cast<float>(x0);
    const float tz = fz - static_cast<float>(z0);

    const auto& c00 = data.visualMaterialColors[z0 * resolutionX + x0];
    const auto& c10 = data.visualMaterialColors[z0 * resolutionX + x1];
    const auto& c01 = data.visualMaterialColors[z1 * resolutionX + x0];
    const auto& c11 = data.visualMaterialColors[z1 * resolutionX + x1];
    DirectX::XMFLOAT3 result;
    result.x = LerpValue(LerpValue(c00.x, c10.x, tx),
                         LerpValue(c01.x, c11.x, tx), tz);
    result.y = LerpValue(LerpValue(c00.y, c10.y, tx),
                         LerpValue(c01.y, c11.y, tx), tz);
    result.z = LerpValue(LerpValue(c00.z, c10.z, tx),
                         LerpValue(c01.z, c11.z, tx), tz);
    return result;
}

std::vector<graphics::Vertex> TerrainLayoutRules::BuildMinimapTerrainGrid(
    const std::vector<graphics::Vertex>& source,
    int sourceResolutionX, int sourceResolutionZ,
    std::vector<std::uint32_t>& outIndices) {
    const int reducedX = std::max(2, std::min(sourceResolutionX,
                                              kMinimapTileGridRes));
    const int reducedZ = std::max(2, std::min(sourceResolutionZ,
                                              kMinimapTileGridRes));
    std::vector<graphics::Vertex> output;
    output.reserve(static_cast<std::size_t>(reducedX) * reducedZ);

    for (int z = 0; z < reducedZ; ++z) {
        int sourceZ = static_cast<int>(std::lround(
            static_cast<float>(z) * static_cast<float>(sourceResolutionZ - 1) /
            static_cast<float>(reducedZ - 1)));
        sourceZ = std::clamp(sourceZ, 0, sourceResolutionZ - 1);
        for (int x = 0; x < reducedX; ++x) {
            int sourceX = static_cast<int>(std::lround(
                static_cast<float>(x) * static_cast<float>(sourceResolutionX - 1) /
                static_cast<float>(reducedX - 1)));
            sourceX = std::clamp(sourceX, 0, sourceResolutionX - 1);
            graphics::Vertex vertex =
                source[sourceZ * sourceResolutionX + sourceX];
            vertex.normal = {0.0f, 1.0f, 0.0f};
            vertex.tangent = {1.0f, 0.0f, 0.0f};
            vertex.bitangent = {0.0f, 0.0f, 1.0f};
            output.push_back(vertex);
        }
    }

    outIndices.clear();
    outIndices.reserve(static_cast<std::size_t>(reducedX - 1) *
                       (reducedZ - 1) * 6);
    for (int z = 0; z < reducedZ - 1; ++z) {
        for (int x = 0; x < reducedX - 1; ++x) {
            const std::uint32_t i0 = static_cast<std::uint32_t>(z * reducedX + x);
            const std::uint32_t i1 = static_cast<std::uint32_t>(z * reducedX + x + 1);
            const std::uint32_t i2 = static_cast<std::uint32_t>((z + 1) * reducedX + x);
            const std::uint32_t i3 = static_cast<std::uint32_t>((z + 1) * reducedX + x + 1);
            outIndices.insert(outIndices.end(), {i0, i1, i2, i2, i1, i3});
        }
    }
    return output;
}

std::vector<graphics::Vertex> TerrainLayoutRules::BuildMinimapOverlayQuad(
    float fieldWidth, float zTop, float zBottom, float flatY,
    std::vector<std::uint32_t>& outIndices) {
    const float halfWidth = fieldWidth * 0.5f;
    const auto makeVertex = [](float x, float y, float z, float u, float v) {
        graphics::Vertex vertex;
        vertex.position = {x, y, z};
        vertex.normal = {0.0f, 1.0f, 0.0f};
        vertex.texCoord = {u, v};
        vertex.color = {1.0f, 1.0f, 1.0f, 1.0f};
        vertex.tangent = {1.0f, 0.0f, 0.0f};
        vertex.bitangent = {0.0f, 0.0f, 1.0f};
        return vertex;
    };

    std::vector<graphics::Vertex> output;
    output.reserve(4);
    output.push_back(makeVertex(-halfWidth, flatY, zTop, 0.0f, 0.0f));
    output.push_back(makeVertex(halfWidth, flatY, zTop, 1.0f, 0.0f));
    output.push_back(makeVertex(-halfWidth, flatY, zBottom, 0.0f, 1.0f));
    output.push_back(makeVertex(halfWidth, flatY, zBottom, 1.0f, 1.0f));
    outIndices = {0, 1, 2, 2, 1, 3};
    return output;
}

float TerrainLayoutRules::ComputeMaxVertexHeight(
    const std::vector<graphics::Vertex>& vertices) {
    float maxHeight = 0.0f;
    bool hasVertex = false;
    for (const auto& vertex : vertices) {
        if (!hasVertex || vertex.position.y > maxHeight) {
            maxHeight = vertex.position.y;
            hasVertex = true;
        }
    }
    return maxHeight;
}

int TerrainLayoutRules::DetermineBiome(
    const std::vector<std::string>& categories,
    const std::string& pageTitle) {
    for (const auto& category : categories) {
        if (category.find("歴史") != std::string::npos ||
            category.find("戦争") != std::string::npos ||
            category.find("事件") != std::string::npos ||
            category.find("政治") != std::string::npos ||
            category.find("古代") != std::string::npos) {
            return 1;
        }
        if (category.find("科学") != std::string::npos ||
            category.find("技術") != std::string::npos ||
            category.find("数学") != std::string::npos ||
            category.find("物理") != std::string::npos ||
            category.find("コンピュータ") != std::string::npos ||
            category.find("宇宙") != std::string::npos) {
            return 2;
        }
        if (category.find("地理") != std::string::npos ||
            category.find("地形") != std::string::npos ||
            category.find("生物") != std::string::npos ||
            category.find("植物") != std::string::npos ||
            category.find("動物") != std::string::npos ||
            category.find("山") != std::string::npos) {
            return 3;
        }
    }

    std::hash<std::string> hasher;
    return static_cast<int>(hasher(pageTitle) % 4);
}

} // namespace game::systems
