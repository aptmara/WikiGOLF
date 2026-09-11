/**
 * @file TutorialCourseLayout.cpp
 * @brief チュートリアル専用コースの固定配置を実装します。
*/

#include "TutorialCourseLayout.h"
#include <algorithm>
#include <iterator>

namespace game::scenes {
namespace {

struct TutorialLinkPlacement {
    const char* page;
    float worldX;
    float worldZ;
};

constexpr TutorialLinkPlacement kLinkPlacements[] = {
    {"フェアウェイ", 0.0f, -28.0f},
    {"ラフ", -30.0f, -12.0f},
    {"バンカー", 24.0f, -3.0f},
    {"ウォーターハザード", -24.0f, 20.0f},
    {"グリーン", 12.0f, 42.0f},
    {"ゴール", 0.0f, 56.0f},
};

} // namespace

bool TutorialCourseLayout::IsPresetPage(const std::string& pageName) const {
    return pageName == "チュートリアル" || pageName == "フェアウェイ";
}

std::vector<graphics::LinkRegion> TutorialCourseLayout::BuildGameplayLinks(
    const graphics::WikiTextureResult& texture,
    const std::string& targetPage) const {
    std::vector<graphics::LinkRegion> links;
    links.reserve(std::size(kLinkPlacements));

    for (const auto& placement : kLinkPlacements) {
        graphics::LinkRegion link;
        const auto sourceLink = std::find_if(
            texture.links.begin(), texture.links.end(),
            [&placement](const graphics::LinkRegion& candidate) {
                return candidate.targetPage == placement.page;
            });
        if (sourceLink != texture.links.end()) {
            link = *sourceLink;
        } else {
            link.targetPage = placement.page;
        }

        link.isTarget = link.targetPage == targetPage;
        PlaceAtWorld(link, texture, placement.worldX, placement.worldZ);
        links.push_back(link);
    }
    return links;
}

void TutorialCourseLayout::PlaceAtWorld(
    graphics::LinkRegion& link,
    const graphics::WikiTextureResult& texture,
    float worldX, float worldZ) const {
    if (texture.width == 0 || texture.height == 0) {
        return;
    }

    const float textureWidth = static_cast<float>(texture.width);
    const float textureHeight = static_cast<float>(texture.height);
    const float centerX =
        (worldX / GetFieldWidth() + 0.5f) * textureWidth;
    const float centerY =
        (0.5f - worldZ / GetFieldDepth()) * textureHeight;
    const float maxCenteredWidth =
        std::max(1.0f, std::min(centerX, textureWidth - centerX) * 2.0f);
    const float maxCenteredHeight =
        std::max(1.0f, std::min(centerY, textureHeight - centerY) * 2.0f);

    link.width = std::min(std::min(260.0f, textureWidth * 0.18f),
                          maxCenteredWidth);
    link.height = std::min(72.0f, maxCenteredHeight);
    link.x = std::clamp(centerX - link.width * 0.5f, 0.0f,
                        std::max(0.0f, textureWidth - link.width));
    link.y = std::clamp(centerY - link.height * 0.5f, 0.0f,
                        std::max(0.0f, textureHeight - link.height));
}

} // namespace game::scenes
