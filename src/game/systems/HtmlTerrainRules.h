#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace game::systems {

/** @brief 記事要素の種別。地形の高さ・地面種別の割り当てに使う。 */
enum class HtmlRegionKind {
    Body,    ///< 通常の本文相当（画像など）: ラフの小さな起伏
    Heading, ///< 見出し: 区切りを示す緩やかな尾根
    Hazard,  ///< 表: バンカー（ハザード）として扱う
};

struct HtmlTerrainRegion { float u, v, width, height; HtmlRegionKind kind = HtmlRegionKind::Body; };
struct HtmlTerrainPoint { float x, z; };

namespace detail {
/** @brief 決定論的なハッシュから[-1,1]の疑似乱数値を作ります。 */
inline float HtmlHash01(std::uint32_t n) {
    n = (n << 13u) ^ n;
    n = n * (n * n * 15731u + 789221u) + 1376312589u;
    return 1.0f - static_cast<float>(n & 0x7fffffffu) / 1073741824.0f;
}
/** @brief 軽量なバリューノイズ。外部ライブラリなしでコース全体に緩やかな起伏を敷くために使う。 */
inline float HtmlValueNoise(float x, float z, std::uint32_t seed) {
    const int ix = static_cast<int>(std::floor(x));
    const int iz = static_cast<int>(std::floor(z));
    const float fx = x - static_cast<float>(ix);
    const float fz = z - static_cast<float>(iz);
    auto at = [&](int cx, int cz) {
        const std::uint32_t n = static_cast<std::uint32_t>(cx * 374761393 + cz * 668265263) ^ seed;
        return HtmlHash01(n);
    };
    const float v00 = at(ix, iz), v10 = at(ix + 1, iz);
    const float v01 = at(ix, iz + 1), v11 = at(ix + 1, iz + 1);
    const float sx = fx * fx * (3 - 2 * fx);
    const float sz = fz * fz * (3 - 2 * fz);
    const float top = v00 + (v10 - v00) * sx;
    const float bottom = v01 + (v11 - v01) * sx;
    return top + (bottom - top) * sz;
}
}

/**
 * @brief 記事領域から通行可能な低い起伏と、ホール周辺の平坦面を作ります。
 * @param heightScale バイオームごとの起伏の強さ（TerrainConfig::heightScale相当）。
 */
inline void BuildHtmlTerrain(int resX, int resZ, float worldWidth, float worldDepth,
    const std::vector<HtmlTerrainRegion>& regions, const std::vector<HtmlTerrainPoint>& holes,
    std::vector<float>& heights, std::vector<std::uint8_t>& materials,
    float heightScale = 1.0f) {
    if (resX < 2 || resZ < 2 || worldWidth <= 0 || worldDepth <= 0) return;
    const std::size_t cellCount = static_cast<std::size_t>(resX) * resZ;
    heights.assign(cellCount, 0.0f);
    materials.assign(cellCount, 0);

    // 本文の地の文（<p>など）は見出し・画像・表と違って一切起伏を持たないため、
    // 何もない区間が完全に平坦になってしまう。コース全体にごく緩やかな
    // アンビエントの起伏を敷いておき、歩いていて単調にならないようにする。
    const float ambientAmplitude = 0.07f * heightScale;
    constexpr float kAmbientFrequency = 1.0f / 11.0f;
    const std::uint32_t ambientSeed =
        (static_cast<std::uint32_t>(worldWidth * 977.0f) ^ static_cast<std::uint32_t>(worldDepth * 653.0f)) |
        1u;
    for (int z = 0; z < resZ; ++z) {
        const float wz = (0.5f - float(z) / (resZ - 1)) * worldDepth;
        for (int x = 0; x < resX; ++x) {
            const float wx = (float(x) / (resX - 1) - 0.5f) * worldWidth;
            const float n = detail::HtmlValueNoise(wx * kAmbientFrequency, wz * kAmbientFrequency, ambientSeed);
            heights[static_cast<std::size_t>(z) * resX + x] = std::max(0.0f, n) * ambientAmplitude;
        }
    }

    for (const auto& r : regions) {
        // 起伏の高さはheightScaleで拡大される一方、フェザー幅（滑らかに
        // 立ち上がる距離）が固定のままだと急勾配になり過ぎてボールの
        // 転がりが破綻する。フェザー幅もheightScaleに応じて広げ、
        // バイオームが変わっても勾配の急さがおおむね一定になるようにする。
        const float featherWorld = std::max(1.0f, heightScale) * 6.0f;
        const float featherU=std::max(featherWorld/worldWidth, 2.0f/(resX-1));
        const float featherV=std::max(featherWorld/worldDepth, 2.0f/(resZ-1));
        const int left=std::clamp(int((r.u-featherU)*(resX-1)),0,resX-1);
        const int right=std::clamp(int(std::ceil((r.u+r.width+featherU)*(resX-1))),0,resX-1);
        const int top=std::clamp(int((r.v-featherV)*(resZ-1)),0,resZ-1);
        const int bottom=std::clamp(int(std::ceil((r.v+r.height+featherV)*(resZ-1))),0,resZ-1);
        const bool isHeading = r.kind == HtmlRegionKind::Heading;
        const float peakHeight = (isHeading ? .22f : .45f) * heightScale;
        const std::uint8_t material = r.kind == HtmlRegionKind::Hazard ? 2 : 1; // Hazard=Bunker, 他=Rough
        for(int z=top;z<=bottom;++z) for(int x=left;x<=right;++x) {
            const float u=float(x)/(resX-1), v=float(z)/(resZ-1);
            const float du=std::max({r.u-u,0.f,u-r.u-r.width})/featherU;
            const float dv=std::max({r.v-v,0.f,v-r.v-r.height})/featherV;
            float t=1-std::clamp(std::max(du,dv),0.f,1.f); t=t*t*(3-2*t);
            const auto i=static_cast<std::size_t>(z)*resX+x;
            heights[i]=std::max(heights[i], peakHeight*t);
            if(du==0 && dv==0) materials[i]=material;
        }
    }

    // ホール／ティー周辺の平坦化半径は、グリッド1マスの大きさに連動させると
    // 記事が長くて解像度が粗いコースでは半径が異常に肥大化してしまう
    // （逆に密なグリッドでは半径が縮みすぎる）。実寸ベースの固定値にして、
    // コースの規模に関わらず一定サイズのグリーン／ティーになるようにする。
    // 遷移帯の幅もheightScaleに応じて広げ、周囲の起伏が高いバイオームでも
    // グリーン／ティーへ向けて急に落ち込まないようにする（coreそのものの
    // サイズはバイオームに関わらず一定に保つ）。
    const float core = 3.0f;
    const float radius = core + 4.0f * std::max(1.0f, heightScale);
    const float cellX=worldWidth/(resX-1), cellZ=worldDepth/(resZ-1);
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
