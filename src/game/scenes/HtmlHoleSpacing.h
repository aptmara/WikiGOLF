#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <numeric>
#include <utility>
#include <vector>

namespace game::scenes {
/** @brief 目標リンクを優先し、表示位置を移動せずカップの重複を除きます。 */
template<class Link>
std::vector<Link> SelectSpacedHtmlLinks(const std::vector<Link>& links,
    float textureWidth, float textureHeight, float fieldWidth, float fieldDepth,
    float spacing = 4.25f) {
    if(textureWidth<=0 || textureHeight<=0 || spacing<=0) return {};
    std::vector<std::size_t> order(links.size());
    std::iota(order.begin(),order.end(),0);
    std::stable_sort(order.begin(),order.end(),[&](auto a,auto b) { return links[a].isTarget>links[b].isTarget; });
    std::map<std::pair<int,int>, std::vector<std::pair<float,float>>> cells;
    std::vector<std::size_t> selected;
    for(auto i:order) {
        const auto& l=links[i];
        const float x=(l.x+l.width*.5f)/textureWidth*fieldWidth;
        const float z=(l.y+l.height*.5f)/textureHeight*fieldDepth;
        const int cx=static_cast<int>(std::floor(x/spacing)), cz=static_cast<int>(std::floor(z/spacing));
        bool overlap=false;
        for(int dz=-1;dz<=1;++dz) for(int dx=-1;dx<=1;++dx) {
            auto it=cells.find({cx+dx,cz+dz});
            if(it==cells.end()) continue;
            for(const auto& p:it->second) if((x-p.first)*(x-p.first)+(z-p.second)*(z-p.second)<spacing*spacing) overlap=true;
        }
        if(!overlap) { selected.push_back(i); cells[{cx,cz}].push_back({x,z}); }
    }
    std::sort(selected.begin(),selected.end());
    std::vector<Link> out; out.reserve(selected.size());
    for(auto i:selected) out.push_back(links[i]);
    return out;
}
}
