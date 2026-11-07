/**
 * @file GrassCullCS.hlsl
 * @brief カメラ周りのタイルを1スレッド1タイルで判定し、見えるタイルを LOD ごとの区画へ並べる
 * @details 区画に入れた数は、そのまま間接描画の引数（InstanceCount）になる。
 *          見えるタイルには、目標の密度に入りうるシュートの数の上限も添える。
 */

#include "GrassFieldCommon.hlsli"
#include "GolfCupClip.hlsli"

Texture2D<float4> g_TileInfo : register(t0); // x: 最低の高さ、y: 最高の高さ、z: 層の最大密度
RWByteAddressBuffer g_DrawArgs : register(u0);
RWStructuredBuffer<uint2> g_VisibleTiles : register(u1);

/** @brief AABB が視錐台の外側にあれば真 */
bool IsOutsideFrustum(float3 boxMin, float3 boxMax) {
    [unroll]
    for (int i = 0; i < 6; ++i) {
        float4 plane = GrassFrustum[i];
        float3 positive = float3(plane.x >= 0.0f ? boxMax.x : boxMin.x,
                                 plane.y >= 0.0f ? boxMax.y : boxMin.y,
                                 plane.z >= 0.0f ? boxMax.z : boxMin.z);
        if (dot(plane.xyz, positive) + plane.w < 0.0f) {
            return true;
        }
    }
    return false;
}

/** @brief GrassLodForDistance（GrassFieldRules.h）と同じ */
uint LodForDistance(float nearestDistance, float fullDensityDistance, uint lodCount) {
    if (nearestDistance <= fullDensityDistance) {
        return 0u;
    }
    float ratio = nearestDistance / fullDensityDistance;
    int lod = (int)floor(2.0f * log2(ratio));
    return (uint)clamp(lod, 0, (int)lodCount - 1);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint layer = DrawLayer;
    int4 window = GrassLayerWindow[layer];
    if (id.x >= (uint)window.z || id.y >= (uint)window.w) {
        return;
    }
    int2 tile = window.xy + int2(id.xy);
    int2 grid = GrassLayerGrid[layer].xy;
    if (any(tile < 0) || any(tile >= grid)) {
        return;
    }
    float4 info = g_TileInfo.Load(int3(tile, 0));
    if (info.z <= 0.0f) {
        return;
    }

    float tileSize = GrassLayerA[layer].x;
    float fullDensityDistance = GrassLayerA[layer].y;
    float maxDistance = GrassLayerA[layer].z;
    float maxBladeHeight = GrassLayerB[layer].x;
    uint lodCount = (uint)GrassLayerB[layer].y;

    // 風と踏み跡で葉先が横へずれる分を広げる
    float lean = maxBladeHeight * 0.9f + 0.05f;
    float2 origin = GrassTileOrigin(tile, layer);
    float3 boxMin = float3(origin.x - lean, info.x - 0.02f, origin.y - lean);
    float3 boxMax = float3(origin.x + tileSize + lean, info.y + maxBladeHeight,
                           origin.y + tileSize + lean);

    float3 nearest = clamp(GrassCamera.xyz, boxMin, boxMax);
    float nearestDistance = distance(nearest, GrassCamera.xyz);
    if (nearestDistance > maxDistance) {
        return;
    }
    if (IsOutsideFrustum(boxMin, boxMax)) {
        return;
    }

    uint flags = 0u;
    int cupCount = min((int)GolfCupInfo.x, GOLF_CUP_MAX_COUNT);
    [loop]
    for (int i = 0; i < cupCount; ++i) {
        float2 closest = clamp(GolfCups[i].xz, boxMin.xz, boxMax.xz);
        float2 delta = closest - GolfCups[i].xz;
        float radius = GolfCups[i].w + 0.1f;
        if (dot(delta, delta) < radius * radius) {
            flags |= GRASS_TILE_NEAR_CUP;
        }
    }

    // 葉の根本はどれも箱の中なので、最も近い点の目標密度と層の最大密度が上限になる
    float keepLimit = GrassDensityFraction(nearestDistance, fullDensityDistance) * saturate(info.z);
    uint shootLimit = (uint)ceil(keepLimit * GrassLayerB[layer].z);
    if (shootLimit == 0u) {
        return;
    }

    uint lod = LodForDistance(nearestDistance, fullDensityDistance, lodCount);
    uint region = layer * GRASS_MAX_LOD_COUNT + lod;
    uint slot;
    // 引数は5要素（IndexCountPerInstance, InstanceCount, ...）
    g_DrawArgs.InterlockedAdd(region * 20u + 4u, 1u, slot);
    uint capacity = (uint)GrassLayerGrid[layer].z;
    if (slot < capacity) {
        uint packed = (uint(tile.x) & 0xffffu) | (uint(tile.y) << 16u);
        g_VisibleTiles[region * capacity + slot] = uint2(packed, flags | (shootLimit << 1u));
    }
}
