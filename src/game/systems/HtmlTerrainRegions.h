#pragma once
#include "HtmlTerrainRules.h"
#include "../../graphics/WikiTextureGenerator.h"

namespace game::systems {
/** @brief テクスチャ全体の領域を正規化座標へ変換します。 */
inline std::vector<HtmlTerrainRegion> HtmlRegions(const graphics::WikiTextureResult& texture) {
    std::vector<HtmlTerrainRegion> out;
    if(!texture.width || !texture.height || !texture.layoutWidth) return out;
    auto add=[&](const auto& r,HtmlRegionKind kind) {
        out.push_back({r.x/texture.width,r.y/texture.height,r.width/texture.width,r.height/texture.height,kind});
    };
    for(const auto& r:texture.headings) add(r,HtmlRegionKind::Heading);
    for(const auto& r:texture.images) add(r,HtmlRegionKind::Body);
    // 表は縦長になりやすいため、他の要素と見分けが付くようバンカー（ハザード）として扱う。
    for(const auto& r:texture.tables) add(r,HtmlRegionKind::Hazard);
    return out;
}
}
