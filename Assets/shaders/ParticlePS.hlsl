/**
 * @file ParticlePS.hlsl
 * @brief パーティクル専用ピクセルシェーダー
 * 
 * テクスチャなしでもソフトな円形パーティクルを描画し、
 * 発光感（Emissive）のある表現をサポートします。
 */

Texture2D diffuseTexture : register(t0);
SamplerState texSampler : register(s0);

float CloudNoise(float2 uv) {
    float2 cell = floor(uv);
    float2 f = frac(uv);
    f = f * f * (3.0 - 2.0 * f);
    float a = frac(sin(dot(cell, float2(12.9898, 78.233))) * 43758.5453);
    float b = frac(sin(dot(cell + float2(1, 0), float2(12.9898, 78.233))) * 43758.5453);
    float c = frac(sin(dot(cell + float2(0, 1), float2(12.9898, 78.233))) * 43758.5453);
    float e = frac(sin(dot(cell + float2(1, 1), float2(12.9898, 78.233))) * 43758.5453);
    return lerp(lerp(a, b, f.x), lerp(c, e, f.x), f.y);
}

cbuffer ConstantBuffer : register(b0) {
    matrix World_unused;
    matrix View_unused;
    matrix Projection_unused;
    float4 MaterialColor_unused;
    float4 MaterialFlags_unused;
};

#include "TemporalVelocity.hlsli"

/**
 * @struct PS_INPUT
 * @brief ピクセルシェーダー入力
 */
struct PS_INPUT {
    float4 position : SV_POSITION;   /**< 射影座標 */
    float3 normal : NORMAL;          /**< 法線ベクトル */
    float2 texCoord : TEXCOORD;      /**< UV座標 */
    float4 color : COLOR;            /**< 頂点カラー */
    float4 materialFlags : TEXCOORD4;/**< マテリアルフラグ */
};

/**
 * @brief パーティクルピクセルシェーダーメインエントリ
 * @param input ピクセル入力情報
 * @return 円形・星型減衰適用後のアルファ合成カラー
 *         （半透明のため速度はカメラ移動のみ扱い。半透明描画では書き込み自体も無効）
 */
SceneOutput main(PS_INPUT input) {
    // 頂点カラー * マテリアルカラー
    float4 finalColor = input.color;

    // UV座標から中心(0.5, 0.5)への距離を計算
    // これにより、テクスチャがなくても円形のソフトパーティクルが作れる
    float2 centerDist = input.texCoord - float2(0.5, 0.5);
    float d = length(centerDist) * 2.0; // 0.0(中心) ~ 1.0(端)
    
    float alpha = pow(saturate(1.0 - d), 2.0);
    const float shapeMode = input.materialFlags.z;
    const float variant = input.materialFlags.w;

    // 1: 砂煙・冷気・岩粉・水煙・黒煙。低周波と細粒ノイズを重ねる。
    if (shapeMode > 0.5 && shapeMode < 1.5) {
        float core = pow(saturate(1.0 - d * 0.78), 1.35);
        float coarse = CloudNoise(input.texCoord * 5.0);
        float fleck = CloudNoise(input.texCoord * 13.0 + 7.2);
        float grain = lerp(0.72, 1.18, coarse * 0.65 + fleck * 0.35);
        float edgeFade = 1.0 - smoothstep(0.62, 1.0, d);
        alpha = saturate(max(alpha * 1.42, core * 0.62) * grain);
        if (variant > 0.5) {
            alpha = saturate(max(alpha, core * 0.82));
        }
        alpha *= edgeFade;
    // 2: 氷片。細長い菱形と中心の鋭いハイライト。
    } else if (shapeMode > 1.5 && shapeMode < 2.5) {
        float2 p = abs(centerDist * 2.0);
        float shard = saturate(1.0 - (p.x * 2.8 + p.y * 0.72));
        float edge = saturate(1.0 - abs(p.x * 3.5 - p.y * 0.35));
        alpha = pow(shard, 0.68) * (0.72 + edge * 0.28);
        finalColor.rgb *= 0.82 + edge * 0.52;
    // 3: 水滴。縁に薄い反射、中心は透明感を残す。
    } else if (shapeMode > 2.5 && shapeMode < 3.5) {
        float body = smoothstep(1.0, 0.12, d);
        float rim = smoothstep(0.24, 0.0, abs(d - 0.68));
        float highlight = smoothstep(0.34, 0.0,
                                     length(centerDist - float2(-0.14, -0.16)));
        alpha = saturate(body * 0.58 + rim * 0.42 + highlight * 0.32);
        finalColor.rgb *= 0.86 + rim * 0.42 + highlight * 0.55;
    // 4: 火の粉。高輝度の芯を持つ縦長の火花。
    } else if (shapeMode > 3.5 && shapeMode < 4.5) {
        float2 p = abs(centerDist * 2.0);
        float spark = saturate(1.0 - (p.x * 3.4 + p.y * 0.82));
        float hotCore = pow(saturate(1.0 - d * 2.8), 2.0);
        alpha = saturate(pow(spark, 0.58) + hotCore);
        finalColor.rgb *= 1.0 + hotCore * 0.85;
    // 5: 接地リング。円柱側面を消し、上面に細い同心円を描く。
    } else if (shapeMode > 4.5 && shapeMode < 5.5) {
        float radial = length(centerDist) * 2.0;
        float ringWidth = lerp(0.075, 0.035, saturate(variant));
        float ring = 1.0 - smoothstep(ringWidth, ringWidth * 2.1,
                                     abs(radial - 0.78));
        float innerEcho = (1.0 - smoothstep(ringWidth * 0.55,
                                            ringWidth * 1.65,
                                            abs(radial - 0.52))) * 0.34;
        alpha = saturate(ring + innerEcho) *
                (1.0 - smoothstep(0.86, 1.0, radial));
    // 6: 芝の葉。先端を尖らせ、中央に控えめな葉脈を残す。
    } else if (shapeMode > 5.5 && shapeMode < 6.5) {
        float2 p = centerDist * 2.0;
        float leafWidth = 0.55 * pow(saturate(1.0 - p.x * p.x), 0.8);
        float edge = abs(p.y - 0.14 * sin(p.x * 3.14159));
        alpha = (1.0 - smoothstep(leafWidth * 0.65, leafWidth + 0.015, edge)) *
                (1.0 - smoothstep(0.82, 1.0, abs(p.x)));
        finalColor.rgb *= 0.82 + 0.28 * (1.0 - smoothstep(0.0, 0.08, edge));
    // 7: 打撃の光輪。中心は開けてボールを隠さない。
    } else if (shapeMode > 6.5 && shapeMode < 7.5) {
        alpha = exp(-pow((d - 0.65) / 0.085, 2.0)) *
                (1.0 - smoothstep(0.88, 1.0, d));
    // 8: 軌跡。横は柔らかく、縦は隣の区間へつなぐ。
    } else if (shapeMode > 7.5 && shapeMode < 8.5) {
        float2 p = abs(centerDist * 2.0);
        alpha = pow(saturate(1.0 - p.x * p.x), 2.5) *
                (1.0 - smoothstep(0.65, 1.0, p.y));
    // variant: 星・グリント用の十字形。
    } else if (variant > 0.5) {
        float2 p = abs(centerDist * 2.0);
        float diamond = saturate(1.0 - (p.x + p.y));
        float cross = saturate(1.0 - min(p.x, p.y) * 5.2) * saturate(1.0 - max(p.x, p.y) * 1.1);
        float diagonal = saturate(1.0 - abs(p.x - p.y) * 5.5) * saturate(1.0 - max(p.x, p.y) * 1.35);
        float core = saturate(1.0 - d * 3.0);
        alpha = saturate(pow(max(diamond, max(cross * 0.72, diagonal * 0.45)), 1.25) + core * 0.85);
    }

    float2 edgeDistance = abs(centerDist * 2.0);
    alpha *= 1.0 - smoothstep(0.8, 1.0, max(edgeDistance.x, edgeDistance.y));
    return MakeSceneOutput(float4(finalColor.rgb, finalColor.a * alpha),
                           kCameraOnlyVelocity);
}
