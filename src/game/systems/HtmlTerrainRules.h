#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace game::systems {
struct HtmlTerrainRegion { float u, v, width, height; bool heading = false; };
struct HtmlTerrainPoint { float x, z; };
/** @brief 記事領域から通行可能な低い起伏と、ホール周辺の平坦面を作ります。 */
inline void BuildHtmlTerrain(int resX, int resZ, float worldWidth, float worldDepth,
    const std::vector<HtmlTerrainRegion>& regions, const std::vector<HtmlTerrainPoint>& holes,
    std::vector<float>& heights, std::vector<std::uint8_t>& materials) {
    if (resX < 2 || resZ < 2 || worldWidth <= 0 || worldDepth <= 0) return;
    heights.assign(static_cast<std::size_t>(resX)*resZ, 0);
    materials.assign(heights.size(), 0);
    for (const auto& r : regions) {
        const float featherU=std::max(4.0f/worldWidth, 2.0f/(resX-1));
        const float featherV=std::max(4.0f/worldDepth, 2.0f/(resZ-1));
        const int left=std::clamp(int((r.u-featherU)*(resX-1)),0,resX-1);
        const int right=std::clamp(int(std::ceil((r.u+r.width+featherU)*(resX-1))),0,resX-1);
        const int top=std::clamp(int((r.v-featherV)*(resZ-1)),0,resZ-1);
        const int bottom=std::clamp(int(std::ceil((r.v+r.height+featherV)*(resZ-1))),0,resZ-1);
        for(int z=top;z<=bottom;++z) for(int x=left;x<=right;++x) {
            const float u=float(x)/(resX-1), v=float(z)/(resZ-1);
            const float du=std::max({r.u-u,0.f,u-r.u-r.width})/featherU;
            const float dv=std::max({r.v-v,0.f,v-r.v-r.height})/featherV;
            float t=1-std::clamp(std::max(du,dv),0.f,1.f); t=t*t*(3-2*t);
            const auto i=static_cast<std::size_t>(z)*resX+x;
            heights[i]=std::max(heights[i],(r.heading?.18f:.45f)*t);
            if(!r.heading && du==0 && dv==0) materials[i]=1;
        }
    }
    const float cellX=worldWidth/(resX-1), cellZ=worldDepth/(resZ-1);
    const float core=std::max(2.f,2.f*std::max(cellX,cellZ));
    const float radius=core+4.f;
    auto flatten=[&](HtmlTerrainPoint p, bool green) {
        int cx=int((p.x/worldWidth+.5f)*(resX-1)), cz=int((.5f-p.z/worldDepth)*(resZ-1));
        int rx=int(std::ceil(radius/cellX))+1, rz=int(std::ceil(radius/cellZ))+1;
        for(int z=std::max(0,cz-rz);z<=std::min(resZ-1,cz+rz);++z)
            for(int x=std::max(0,cx-rx);x<=std::min(resX-1,cx+rx);++x) {
                float dx=x*cellX-worldWidth*.5f-p.x, dz=worldDepth*.5f-z*cellZ-p.z;
                float d=std::sqrt(dx*dx+dz*dz);
                if(d>radius) continue;
                float t=std::clamp((d-core)/(radius-core),0.f,1.f); t=t*t*(3-2*t);
                auto i=static_cast<std::size_t>(z)*resX+x;
                heights[i]*=t;
                if(d<=core) materials[i]=green?3:0;
            }
    };
    for(const auto& p:holes) flatten(p,true);
    flatten({0,-worldDepth*.4f},false);
}
}
