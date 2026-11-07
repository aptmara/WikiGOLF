/**
 * @file GrassFieldVS.hlsl
 * @brief 頂点番号とタイルから芝の葉を組み立てる（頂点バッファなし）
 * @details 1インスタンスが見えるタイル1枚。葉の番号 = 頂点番号 / 1枚の頂点数。
 *          葉は3枚ずつシュート（茎）にまとまり、シュートは低食い違い列とタイルごとの乱数で並ぶ。
 *          シュートの番号は順位も兼ね、目標の密度を超える順位の葉は縮めて消す。
 */

#include "GrassFieldCommon.hlsli"
#include "GolfCupClip.hlsli"
#include "TemporalVelocity.hlsli"

Texture2D<float> g_Heights : register(t0);
Texture2D<float4> g_Field : register(t1);
Texture2D<float4> g_Wind : register(t2);
Texture2D<float4> g_PreviousWind : register(t3);
Texture2D<float4> g_Trample : register(t4);
StructuredBuffer<uint2> g_VisibleTiles : register(t5);
Texture2D<float4> g_PreviousTrample : register(t6);
SamplerState g_LinearClamp : register(s0);
SamplerState g_LinearWrap : register(s2);

struct VS_OUTPUT {
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float4 color : COLOR;          // a: 根本からの高さの割合
    float3 worldPos : TEXCOORD1;
    float materialClass : TEXCOORD3;
    float4 shadowPosition : TEXCOORD4;
    float4 prevClip : TEXCOORD5;
};

/** @brief CPU の SampleGrassFieldHeight と同じ双線形補間 */
float SampleGroundHeight(float2 xz) {
    float2 uv = saturate(GrassFieldUv(xz));
    int2 res = GrassHeightInfo.xy;
    float2 f = uv * float2(res - 1);
    int2 i = clamp(int2(f), int2(0, 0), res - 2);
    float2 d = f - float2(i);
    float h00 = g_Heights.Load(int3(i, 0));
    float h10 = g_Heights.Load(int3(i + int2(1, 0), 0));
    float h01 = g_Heights.Load(int3(i + int2(0, 1), 0));
    float h11 = g_Heights.Load(int3(i + int2(1, 1), 0));
    float h0 = lerp(h00, h10, d.x);
    float h1 = lerp(h01, h11, d.x);
    return lerp(h0, h1, d.y) + GrassFieldInfo.x;
}

/** @brief 葉1枚の形と色（時刻に依らない部分） */
struct Blade {
    bool alive;
    float3 root;
    float2 facing;      // 葉の面の向き（水平）
    float2 leanDir;     // 自然に倒れる向き
    float height;
    float width;
    float lean;         // 葉先の自然な倒れ[高さに対する割合]
    float flex;         // 風・踏み跡への反応のしやすさ
    float windPhase;
    float materialClass;
    float3 color;
};

/** @brief 葉の番号から形を決める */
Blade BuildBlade(int2 tile, uint flags, uint bladeIndex) {
    Blade blade = (Blade)0;
    uint layer = DrawLayer;
    float tileSize = GrassLayerA[layer].x;
    float fullDensityDistance = GrassLayerA[layer].y;
    float maxDistance = GrassLayerA[layer].z;
    float fadeWidth = GrassLayerA[layer].w;
    float baseShoots = GrassLayerB[layer].z;

    uint shoot = bladeIndex / GRASS_LEAVES_PER_SHOOT;
    uint leaf = bladeIndex - shoot * GRASS_LEAVES_PER_SHOOT;
    uint tileSeed = GrassTileSeed(tile, layer);
    uint shootSeed = GrassHash(tileSeed ^ (shoot * 2654435761u));

    // R2 列（タイルごとに始点をずらす）に、間隔の半分ほどの揺らぎを足す
    float2 start = float2(GrassHashToFloat(tileSeed), GrassHashToFloat(GrassHash(tileSeed + 1u)));
    float2 r2 = frac(start + float(shoot) * float2(0.7548776662f, 0.5698402910f));
    float spacing = rsqrt(max(baseShoots, 1.0f));
    float2 jitter = float2(GrassHashToFloat(shootSeed), GrassHashToFloat(GrassHash(shootSeed + 7u))) - 0.5f;
    float2 local = r2 + jitter * spacing * 1.1f;
    float2 shootXZ = GrassTileOrigin(tile, layer) + local * tileSize;

    // 境界がフィールドの格子に沿わないよう、読む位置を少しずらす
    uint fieldSeed = GrassHash(shootSeed + 13u);
    float2 fieldJitter = (float2(GrassHashToFloat(fieldSeed), GrassHashToFloat(GrassHash(fieldSeed))) - 0.5f) * 0.3f;
    float4 field = g_Field.SampleLevel(g_LinearClamp, GrassFieldUv(shootXZ + fieldJitter), 0);
    float density = layer == GRASS_LAYER_ROUGH ? GrassRoughDensity(field) : GrassTurfDensity(field);

    float shootGround = SampleGroundHeight(shootXZ);
    float cameraDistance = distance(GrassCamera.xyz, float3(shootXZ.x, shootGround, shootXZ.y));
    float target = GrassDensityFraction(cameraDistance, fullDensityDistance);
    float rank = (float(shoot) + 0.5f) / baseShoots;
    float keep = target * density;
    // 目標の密度の境目にある葉は、急に消えずに縮んでいく
    float survive = saturate((keep - rank) / max(keep * 0.2f, 1.0e-4f));
    float distanceFade = saturate((maxDistance - cameraDistance) / fadeWidth);
    float grow = survive * distanceFade;

    // 上から見ると立った葉は細い線になり地面が透けるので、見下ろすほど葉を広げて寝かせる
    float3 viewDirection = normalize(GrassCamera.xyz - float3(shootXZ.x, shootGround, shootXZ.y));
    float overhead = smoothstep(0.45f, 0.95f, abs(viewDirection.y));
    // グリーンは真上に近づくほど葉を縮めて消し、地形の芝の表現へ任せる
    float greenKeep = 1.0f;
    if (layer == GRASS_LAYER_TURF) {
        greenKeep = 1.0f - smoothstep(0.60f, 0.85f, abs(viewDirection.y)) * saturate(field.b);
    }
    if (grow <= 0.02f || greenKeep <= 0.03f) {
        return blade;
    }

    uint leafSeed = GrassHash(shootSeed ^ (leaf * 0x9e3779b9u + 101u));
    float leafRandom = GrassHashToFloat(leafSeed);
    float leafRandom2 = GrassHashToFloat(GrassHash(leafSeed + 3u));
    float leafRandom3 = GrassHashToFloat(GrassHash(leafSeed + 5u));
    float shootYaw = GrassHashToFloat(GrassHash(shootSeed + 17u)) * 6.2831853f;
    float leafYaw = shootYaw + float(leaf) * 2.0943951f + (leafRandom - 0.5f) * 1.2f;
    float2 outward = float2(cos(leafYaw), sin(leafYaw));

    // 遠くでは本数が減る分、葉を太くして覆う面積を保つ（GrassWidthScaleForDensity と同じ）
    float widthScale = clamp(pow(max(target, 1.0e-4f), -0.75f), 1.0f, 6.0f);
    widthScale *= lerp(1.0f, 2.4f, overhead);

    float3 baseColor;
    if (layer == GRASS_LAYER_ROUGH) {
        float roughness = saturate((field.r - 0.55f) / 0.45f);
        blade.height = lerp(0.11f, 0.19f, leafRandom2) * max(field.r, 0.25f);
        blade.width = lerp(0.006f, 0.010f, leafRandom3) * lerp(0.8f, 1.0f, roughness);
        blade.lean = lerp(0.15f, 0.55f, leafRandom3 * leafRandom2);
        blade.materialClass = lerp(0.6f, 0.88f, roughness);
        baseColor = lerp(float3(0.46f, 0.66f, 0.27f), float3(0.38f, 0.58f, 0.21f), roughness);
        blade.root = float3(shootXZ.x, 0.0f, shootXZ.y);
        blade.root.xz += outward * 0.006f;
    } else {
        float green = saturate(field.b);
        blade.height = lerp(lerp(0.031f, 0.039f, leafRandom2), lerp(0.010f, 0.013f, leafRandom2), green);
        // グリーンは葉が短いぶん上から地面が透けやすいので、幅を広げて面を埋める
        blade.width = lerp(0.0025f, 0.0040f, leafRandom3) * lerp(1.0f, 1.3f, green);
        blade.lean = lerp(0.08f, 0.35f, leafRandom3 * leafRandom2);
        blade.materialClass = lerp(0.14f, 0.06f, green);
        baseColor = lerp(float3(0.46f, 0.66f, 0.27f), float3(0.53f, 0.71f, 0.32f), green);
        blade.root = float3(shootXZ.x, 0.0f, shootXZ.y);
        blade.root.xz += outward * 0.002f;
    }
    blade.root.y = shootGround;
    if ((flags & GRASS_TILE_NEAR_CUP) != 0u && IsInsideGolfCupOpening(blade.root, 0.015f)) {
        return blade;
    }

    blade.alive = true;
    // バンカーや水の縁では葉を短く起こし、砂の上へ倒れかからないようにする
    float edge = saturate(field.a);
    blade.height *= lerp(0.35f, 1.0f, edge);
    blade.lean *= lerp(0.4f, 1.0f, edge);
    blade.lean = min(blade.lean * lerp(1.0f, 1.8f, overhead), 0.85f);
    blade.height *= lerp(0.35f, 1.0f, grow) * greenKeep;
    blade.width *= widthScale * lerp(0.6f, 1.0f, grow) * greenKeep;
    blade.facing = float2(-outward.y, outward.x);
    blade.leanDir = outward;
    blade.flex = saturate(blade.height / 0.17f) * 0.92f + 0.08f;
    blade.windPhase = GrassHashToFloat(GrassHash(shootSeed + 29u)) * 6.2831853f;

    // 葉ごとの明るさと、ところどころ枯れかけた葉、数 m 単位のまだら
    float patch = GrassHashToFloat(GrassHash(uint(floor(shootXZ.x / 3.0f) + 4096.0f) * 92837111u ^
                                             uint(floor(shootXZ.y / 3.0f) + 4096.0f) * 689287499u));
    float tint = (0.94f + leafRandom * 0.12f) * (0.95f + patch * 0.10f);
    float dry = step(0.93f, leafRandom2) * 0.55f;
    float3 dryColor = float3(0.62f, 0.62f, 0.32f);
    blade.color = lerp(baseColor, dryColor, dry) * tint;
    return blade;
}

/** @brief 輪の形（中心が radius、幅 width） */
float GrassRing(float distanceToCenter, float radius, float width) {
    float t = (distanceToCenter - radius) / width;
    return exp(-t * t);
}

/**
 * @brief ショット・着地・カップインで葉先が受けるずれ（しなやかさ 1 の葉）
 * @details 寿命は GrassImpulseRules.h の GrassImpulseDuration と合わせる。
 */
float2 GrassImpulseOffset(float2 xz, float time) {
    float2 offset = float2(0.0f, 0.0f);
    uint count = min((uint)GrassFieldInfo.w, (uint)GRASS_MAX_IMPULSES);
    [loop]
    for (uint i = 0u; i < count; ++i) {
        float4 a = GrassImpulseA[i];
        float4 b = GrassImpulseB[i];
        float age = time - a.z;
        if (age < 0.0f) {
            continue;
        }
        float2 relative = xz - a.xy;
        float d = length(relative);
        float2 radial = relative / max(d, 1.0e-3f);
        float strength = b.z;
        uint kind = (uint)(a.w + 0.5f);

        if (kind == GRASS_IMPULSE_SWING) {
            // 打った向きへ一気になびき、ばねのように揺り返して戻る
            float reach = lerp(1.6f, 3.2f, strength);
            float falloff = saturate(1.0f - d / reach);
            falloff *= falloff;
            float envelope = saturate(age / 0.06f) * exp(-age * 3.2f) * cos(age * 10.0f) *
                             saturate((1.4f - age) / 0.3f);
            offset += (b.xy * 0.20f + radial * 0.06f) * strength * falloff * envelope;
        } else if (kind == GRASS_IMPULSE_LANDING) {
            // 落ちた所から外へ押し倒す輪が広がり、通った後は少し揺り返す
            float radius = age * 6.5f;
            float reach = lerp(2.5f, 6.0f, strength);
            float fade = saturate(1.0f - d / reach) * saturate((1.6f - age) / 0.6f);
            float amplitude = 0.15f * strength;
            float push = GrassRing(d, radius, 0.55f + age * 0.35f);
            float behind = max(radius - d, 0.0f);
            float rebound = sin(behind * 2.6f) * exp(-behind * 1.2f) * 0.35f;
            float crater = exp(-d * d / 0.15f) * exp(-age * 2.0f) * 0.8f;
            offset += radial * amplitude * ((push - rebound) * fade + crater);
        } else {
            // カップから波が3重に広がる
            float fade = saturate((3.0f - age) / 0.8f) * saturate(1.0f - d / 9.0f);
            [unroll]
            for (int k = 0; k < 3; ++k) {
                float waveAge = age - float(k) * 0.45f;
                if (waveAge > 0.0f) {
                    offset += radial * (0.13f - 0.03f * float(k)) * strength * fade *
                              GrassRing(d, waveAge * 5.0f, 0.8f);
                }
            }
        }
    }
    return offset;
}

/** @brief 風と踏み跡による葉先のずれ */
struct BladeMotion {
    float2 tipOffset;
    float vortexVisual;
};

BladeMotion EvaluateMotion(Blade blade, Texture2D<float4> windTexture, float2 windOrigin,
                           Texture2D<float4> trampleTexture, float2 trampleOrigin,
                           float time) {
    BladeMotion motion;
    float2 windUv = GrassWindUv(blade.root.xz, windOrigin);
    float4 wind = float4(0.0f, 0.0f, 0.0f, 0.0f);
    if (all(windUv >= 0.0f) && all(windUv <= 1.0f)) {
        wind = windTexture.SampleLevel(g_LinearClamp, windUv, 0);
    }
    float2 windDir = GrassWindParams.xy;
    float2 crossWindDir = float2(-windDir.y, windDir.x);
    float windStrength = GrassWindParams.z;
    float windAmplitude = lerp(0.009f, 0.115f, windStrength);
    float gustEnvelope = wind.w;
    float travel = dot(blade.root.xz, windDir);

    float bladeFlutter =
        sin(travel * 1.55f + time * (1.70f + windStrength * 3.10f) + blade.windPhase) * 0.68f +
        sin(travel * 2.85f - time * (2.35f + windStrength * 4.20f) + blade.windPhase * 1.73f) * 0.32f;
    float crossFlutter =
        sin(travel * 1.10f + time * (1.25f + windStrength * 2.40f) + blade.windPhase * 1.31f);
    float2 offset = wind.xy;
    offset += windDir * bladeFlutter * windAmplitude * lerp(0.42f, 0.62f, gustEnvelope);
    offset += crossWindDir * crossFlutter * windAmplitude * 0.18f * (0.35f + gustEnvelope * 0.65f);
    offset *= blade.flex;
    // 短い芝もショットの衝撃では見えるほど動かす
    offset += GrassImpulseOffset(blade.root.xz, time) * (0.45f + 0.55f * blade.flex);

    float2 trampleRelative = blade.root.xz - trampleOrigin;
    float trampleSize = kTrampleTexelSize * kTrampleResolution;
    if (all(trampleRelative >= 0.0f) && all(trampleRelative < trampleSize)) {
        float4 trample = trampleTexture.SampleLevel(g_LinearWrap, blade.root.xz / trampleSize, 0);
        float amount = trample.b;
        float2 bendDirection = trample.rg * 2.0f - 1.0f;
        float flexibility = blade.flex * blade.flex;
        offset += bendDirection * lerp(0.01f, 0.30f, flexibility) * amount;
    }

    float maxOffset = blade.height * 0.9f;
    float offsetLength = length(offset);
    if (offsetLength > maxOffset) {
        offset *= maxOffset / offsetLength;
    }
    motion.tipOffset = offset;
    motion.vortexVisual = wind.z;
    return motion;
}

/**
 * @brief 葉の上の点のワールド座標
 * @param along 根本 0 〜 葉先 1
 * @param side -1 〜 1（葉の幅方向）
 */
float3 BladePoint(Blade blade, BladeMotion motion, float along, float side) {
    float curve = along * along;
    float2 lean = blade.leanDir * blade.lean * blade.height + motion.tipOffset;
    // 横へ倒れた分だけ葉先を下げ、葉の長さをおおよそ保つ
    float leanRatio = saturate(length(lean) / max(blade.height, 1.0e-4f));
    float heightScale = sqrt(saturate(1.0f - leanRatio * leanRatio * 0.8f));
    float halfWidth = blade.width * 0.5f * (1.0f - pow(along, 1.6f));
    float3 position = blade.root;
    position.y += blade.height * along * heightScale;
    position.xz += lean * curve + blade.facing * side * halfWidth;
    return position;
}

VS_OUTPUT main(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID) {
    VS_OUTPUT output = (VS_OUTPUT)0;
    uint2 entry = g_VisibleTiles[DrawRegionBase + instanceId];
    int2 tile = int2(entry.x & 0xffffu, entry.x >> 16u);
    uint bladeIndex = vertexId / DrawVertsPerBlade;
    uint localVertex = vertexId - bladeIndex * DrawVertsPerBlade;

    // タイルの上限より後ろの順位の葉は、形を作る前に捨てる
    uint shootLimit = entry.y >> 1u;
    if (bladeIndex / GRASS_LEAVES_PER_SHOOT >= shootLimit) {
        output.position = float4(2.0f, 2.0f, 2.0f, 1.0f);
        return output;
    }

    Blade blade = BuildBlade(tile, entry.y, bladeIndex);
    if (!blade.alive) {
        // 葉の頂点をすべて画面外の同じ点に置き、三角形ごと捨てる
        output.position = float4(2.0f, 2.0f, 2.0f, 1.0f);
        return output;
    }

    uint segments = (DrawVertsPerBlade - 1u) / 2u;
    bool isTip = localVertex == DrawVertsPerBlade - 1u;
    float along = isTip ? 1.0f : float(localVertex / 2u) / float(segments);
    float side = isTip ? 0.0f : ((localVertex & 1u) == 0u ? -1.0f : 1.0f);

    float time = GrassLight.w;
    BladeMotion motion = EvaluateMotion(blade, g_Wind, GrassWindOrigin.xy, g_Trample,
                                        GrassTrampleOrigin.xy, time);
    float3 worldPos = BladePoint(blade, motion, along, side);
    float windWeight = along * along * (3.0f - 2.0f * along);
    worldPos.y -= motion.vortexVisual * blade.flex * windWeight * 0.012f;

    output.position = mul(float4(worldPos, 1.0f), GrassViewProjection);
    output.prevClip = float4(0.0f, 0.0f, 0.0f, 0.0f);
    if (TemporalParams.y > 0.5f) {
        BladeMotion previous = EvaluateMotion(blade, g_PreviousWind, GrassWindOrigin.zw,
                                              g_PreviousTrample, GrassTrampleOrigin.zw,
                                              GrassFieldInfo.y);
        float3 prevPos = BladePoint(blade, previous, along, side);
        prevPos.y -= previous.vortexVisual * blade.flex * windWeight * 0.012f;
        output.prevClip = TemporalPrevClip(prevPos);
    }

    // 葉の面の法線を幅方向へ少し丸め、倒れた向きへ傾ける
    float2 leanTotal = blade.leanDir * blade.lean + motion.tipOffset / max(blade.height, 1.0e-3f);
    float3 faceNormal = float3(blade.facing.y, 0.0f, -blade.facing.x);
    float3 normal = faceNormal + float3(blade.facing.x, 0.0f, blade.facing.y) * side * 0.45f;
    normal.y += 0.35f + along * 0.25f;
    normal.xz -= leanTotal * 0.6f;
    output.normal = normalize(normal);

    float3 rootColor = float3(0.52f, 0.64f, 0.42f);
    float3 tipColor = float3(0.78f, 0.87f, 0.63f);
    float3 gradient = lerp(rootColor, tipColor, along);
    float3 color = blade.color * gradient;
    float highlight = motion.vortexVisual * windWeight;
    color = lerp(color, color * float3(1.16f, 1.30f, 0.92f), highlight * 0.78f);
    output.color = float4(color, along);
    output.worldPos = worldPos;
    output.materialClass = blade.materialClass;
    output.shadowPosition = mul(float4(worldPos, 1.0f), GrassShadowViewProjection);
    return output;
}
