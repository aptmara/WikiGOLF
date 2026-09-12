/**
 * @file ParticlePS.hlsl
 * @brief パーティクル専用ピクセルシェーダー
 * 
 * テクスチャなしでもソフトな円形パーティクルを描画し、
 * 発光感（Emissive）のある表現をサポートします。
 */

Texture2D diffuseTexture : register(t0);
SamplerState texSampler : register(s0);

cbuffer ConstantBuffer : register(b0) {
    matrix World_unused;
    matrix View_unused;
    matrix Projection_unused;
    float4 MaterialColor_unused;
    float4 MaterialFlags_unused;
};

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
 */
float4 main(PS_INPUT input) : SV_TARGET {
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
        float coarse = frac(sin(dot(floor(input.texCoord * 9.0),
                                    float2(12.9898, 78.233))) * 43758.5453);
        float fleck = frac(sin(dot(input.texCoord * 41.0,
                                   float2(39.3468, 11.135))) * 24634.6345);
        float grain = lerp(0.72, 1.18, coarse * 0.65 + fleck * 0.35);
        alpha = saturate(max(alpha * 1.42, core * 0.62) * grain);
        if (variant > 0.5) {
            alpha = saturate(max(alpha, core * 0.82));
        }
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
    // variant: 星・グリント用の十字形。
    } else if (variant > 0.5) {
        float2 p = abs(centerDist * 2.0);
        float diamond = saturate(1.0 - (p.x + p.y));
        float cross = saturate(1.0 - min(p.x, p.y) * 5.2) * saturate(1.0 - max(p.x, p.y) * 1.1);
        float diagonal = saturate(1.0 - abs(p.x - p.y) * 5.5) * saturate(1.0 - max(p.x, p.y) * 1.35);
        float core = saturate(1.0 - d * 3.0);
        alpha = saturate(pow(max(diamond, max(cross * 0.72, diagonal * 0.45)), 1.25) + core * 0.85);
    }

    return float4(finalColor.rgb, finalColor.a * alpha);
}
