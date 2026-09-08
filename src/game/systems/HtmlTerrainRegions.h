#pragma once
#include "HtmlTerrainRules.h"
#include "../../graphics/WikiTextureGenerator.h"

namespace game::systems {
/** @brief テクスチャ全体の領域を正規化座標へ変換します。 */
inline std::vector<HtmlTerrainRegion> HtmlRegions(const graphics::WikiTextureResult& texture) {
    std::vector<HtmlTerrainRegion> out;
    if(!texture.width || !texture.height || !texture.layoutWidth) return out;
    auto add=[&](const auto& r,bool heading) {
        out.push_back({r.x/texture.width,r.y/texture.height,r.width/texture.width,r.height/texture.height,heading});
    };
    for(const auto& r:texture.headings) add(r,true);
    for(const auto& r:texture.images) add(r,false);
    for(const auto& r:texture.tables) add(r,false);
    return out;
}
}
